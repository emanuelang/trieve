# Plan del módulo de monitoreo del sistema de archivos

## 1. Objetivo y frontera

Construir un servicio local y multiplataforma que descubra archivos y detecte cambios dentro de raíces configuradas. Su salida es una secuencia confiable de eventos normalizados que se entrega al orquestador.

Este módulo **no realiza la indexación semántica**. Los extractores, limpieza, chunking, embeddings, índice vectorial y grafo pertenecen a otros módulos coordinados por el orquestador.

### Responsabilidades propias

- Escanear inicialmente las raíces configuradas.
- Observar creación, modificación, eliminación, movimiento y renombre.
- Ocultar las diferencias entre Windows, Linux y macOS.
- Normalizar rutas, eventos y metadatos básicos.
- Aplicar inclusiones, exclusiones y límites configurados.
- Agrupar notificaciones repetidas y esperar estabilidad básica del archivo.
- Detectar desbordamientos o pérdida de eventos.
- Reconciliar periódicamente el estado observado con el disco.
- Publicar cambios al orquestador con entrega confiable.
- Sobrevivir reinicios sin perder eventos ya aceptados.

### Responsabilidades ajenas

Este módulo no debe:

- seleccionar ni ejecutar extractores;
- ejecutar OCR o transcripción;
- limpiar contenido ni generar chunks;
- crear embeddings;
- actualizar el índice vectorial o el grafo;
- decidir cómo se indexa semánticamente un documento;
- invalidar directamente chunks, vectores o nodos derivados.

El orquestador recibe el evento y decide qué pipeline ejecutar.

## 2. Observer y daemon

La idea de un observer que opere como daemon tiene sentido, pero no debería depender exclusivamente de sondeo ni ejecutar el pipeline semántico desde el callback del sistema operativo.

La solución recomendada es híbrida:

1. Un escaneo inicial descubre el estado existente.
2. Un watcher nativo recibe cambios con baja latencia.
3. Un normalizador convierte notificaciones del SO en eventos de dominio.
4. Un coalescer agrupa ráfagas y versiones transitorias.
5. Una outbox persistente conserva eventos hasta que el orquestador los acepte.
6. Un reconciliador recupera periódicamente eventos perdidos.

Observer es útil para publicar eventos dentro de la aplicación. La integración con cada SO debe modelarse mediante Adapter/Strategy: `WindowsFileWatcher` implementa `IFileWatcher` usando la API de Windows. Windows no hereda de Observer ni representa una entidad del dominio.

Primero conviene desarrollar un ejecutable foreground controlable desde CLI. Cuando sea estable se empaqueta como Windows Service, servicio de systemd o LaunchDaemon.

## 3. Relación con el orquestador

```text
Sistema de archivos
        |
        v
+-------------------------------+
| Módulo de monitoreo            |
| scan + watch + normalize       |
| filter + coalesce + reconcile  |
+---------------+---------------+
                | FileChange
                v
+-------------------------------+
| IFileChangeSink                |
| llamada directa o outbox       |
+---------------+---------------+
                v
+-------------------------------+
| Orquestador                    |
| decide y coordina el pipeline  |
+----+----------+---------+------+
     |          |         |
     v          v         v
 Extractor   Chunking  Embeddings
     |          |         |
     +----------+---------+
                v
       Persistencia / grafo
```

La dependencia apunta hacia un puerto abstracto. El watcher no conoce la clase concreta del orquestador ni los módulos que éste coordina.

```cpp
class IFileChangeSink {
public:
    virtual ~IFileChangeSink() = default;
    virtual PublishResult publish(FileChange change) = 0;
};
```

Una implementación puede llamar al orquestador en el mismo proceso. Otra puede escribir una outbox SQLite o publicar en un bus en el futuro, sin modificar los watchers.

### Qué significa “dar el archivo”

Por defecto, el módulo no copia ni envía todos los bytes. Entrega una referencia: ruta, identidad nativa opcional, metadatos y generación observada. El orquestador abre el archivo al procesarlo.

Si se necesitara una instantánea inmutable, puede incorporarse un servicio de snapshots separado. No corresponde añadir esa complejidad al MVP sin un caso concreto.

## 4. Arquitectura interna

```text
                  +-----------------------+
                  | WatchConfig           |
                  | raíces y políticas    |
                  +-----------+-----------+
                              |
           +------------------+------------------+
           |                                     |
 +---------v----------+                +---------v----------+
 | InitialScanner     |                | IFileWatcher       |
 | recorrido inicial  |                | adaptador del SO   |
 +---------+----------+                +---------+----------+
           |                                     |
           +------------------+------------------+
                              |
                    +---------v----------+
                    | EventNormalizer    |
                    +---------+----------+
                              |
                    +---------v----------+
                    | PolicyFilter       |
                    +---------+----------+
                              |
                    +---------v----------+
                    | EventCoalescer     |
                    | + StabilityProbe   |
                    +---------+----------+
                              |
                    +---------v----------+
                    | EventOutbox        |
                    | entrega >= 1 vez   |
                    +---------+----------+
                              |
                    +---------v----------+
                    | IFileChangeSink    |
                    | -> Orquestador     |
                    +--------------------+

 InitialScanner + ObservationCatalog
                 ^
                 |
       ReconciliationService
```

### Componentes

- `InitialScanner`: recorre raíces y emite archivos descubiertos.
- `IFileWatcher`: contrato portable para iniciar y detener observación.
- `WindowsFileWatcher`, `LinuxFileWatcher`, `MacOsFileWatcher`: adaptadores nativos.
- `EventNormalizer`: traduce notificaciones nativas a `FileChange`.
- `PolicyFilter`: aplica raíces, exclusiones, extensiones y tamaño.
- `EventCoalescer`: reduce eventos redundantes preservando la última generación.
- `FileStabilityProbe`: evita publicar una versión todavía en escritura.
- `ObservationCatalog`: estado mínimo para comparación y reconciliación.
- `EventOutbox`: conserva entregas pendientes y reintentos.
- `ReconciliationService`: compara disco y catálogo tras reinicios, overflow o por intervalo.
- `FileMonitorService`: controla el ciclo de vida; no coordina indexación.

## 5. Contrato de salida

```cpp
enum class FileChangeKind {
    Discovered,
    Created,
    Modified,
    Removed,
    Renamed
};

struct FileChange {
    std::string eventId;
    FileChangeKind kind;
    std::filesystem::path path;
    std::optional<std::filesystem::path> previousPath;

    std::optional<std::uintmax_t> size;
    std::optional<std::filesystem::file_time_type> modifiedAt;
    std::optional<std::string> nativeFileId;

    std::uint64_t generation;
    std::chrono::system_clock::time_point observedAt;
    std::string source; // watcher, initial_scan, reconciliation
};

class IFileWatcher {
public:
    virtual ~IFileWatcher() = default;
    virtual void start(const WatchConfig&, NativeEventSink) = 0;
    virtual void stop() noexcept = 0;
};
```

### Reglas

- `eventId` permite deduplicar reintentos.
- `generation` representa el orden lógico conocido para un archivo.
- `previousPath` sólo se completa si el renombre pudo correlacionarse.
- `nativeFileId` ayuda con renombres, pero no es portable ni eterno.
- `source` permite distinguir watcher, escaneo inicial y reconciliación.
- `Removed` no requiere que la ruta todavía exista.
- Los eventos describen observaciones; no garantizan que esa versión siga en disco.
- El contrato debe versionarse si cruza límites de proceso.

## 6. Flujo por tipo de cambio

### Descubrimiento, creación o modificación

1. Scanner o watcher observa una ruta.
2. Se normaliza y valida que permanezca dentro de una raíz autorizada.
3. Se aplican filtros y exclusiones.
4. Se agrupan notificaciones repetidas.
5. Se espera estabilidad mediante tamaño y `mtime` durante una ventana configurable.
6. Se leen metadatos sin interpretar el contenido.
7. Se asignan `eventId` y `generation`.
8. El evento se persiste en la outbox.
9. Se publica mediante `IFileChangeSink`.
10. Se marca entregado sólo cuando el receptor confirma recepción.

Después del ACK, el orquestador controla extracción, chunking, embeddings y persistencia semántica.

### Eliminación

El módulo publica `Removed` con la última identidad y ruta conocidas. El orquestador decide cómo invalidar derivados. El catálogo de observación puede conservar un tombstone temporal para reconciliación y correlación.

### Renombre o movimiento

Si el SO entrega ambas rutas, se publica un `Renamed`. Si sólo se observan `Removed + Created`, el módulo puede correlacionarlos por identidad nativa y proximidad temporal. Si no es seguro, publica ambos; el orquestador debe tolerarlo.

Mover hacia fuera de una raíz equivale a eliminación; mover hacia dentro equivale a descubrimiento o creación.

### Overflow

`Overflow` es una señal interna de control, no un trabajo semántico. Programa reconciliación de la raíz. Las diferencias se publican como `FileChange` con `source = "reconciliation"`.

## 7. Consistencia con el procesamiento

Existe una carrera inevitable:

```text
Se observa versión A
        |
El archivo cambia a versión B
        |
El orquestador abre la ruta
```

Por eso se publican generación y metadatos. El orquestador verifica la versión antes y, si hace falta, después de procesar. Si cambió, descarta el resultado obsoleto o programa la versión nueva.

La garantía realista es entrega **al menos una vez**:

- el monitor puede reenviar un mismo `eventId`;
- el orquestador deduplica por `eventId`;
- una generación menor no puede sobrescribir una mayor;
- la reconciliación garantiza convergencia;
- confirmar recepción no significa confirmar indexación.

`PublishResult` podría distinguir `Accepted`, `Duplicate`, `RetryableFailure` y `Rejected`. Sólo `Accepted` y `Duplicate` cierran la entrega.

## 8. Decisiones por sistema operativo

| Plataforma | Mecanismo | Consideraciones |
|---|---|---|
| Windows | `ReadDirectoryChangesW` asíncrono | Renombres en pares, overflow, rutas largas, archivos bloqueados y junctions. |
| Linux | `inotify` | Watch por directorio, límites del kernel, overflow y movimientos de árboles. |
| macOS | FSEvents | Eventos coalescidos de árbol; requiere reexaminar el disco y manejar cursor. |

Una biblioteca multiplataforma puede acelerar el MVP, pero debe quedar detrás de `IFileWatcher`. Ninguna abstracción elimina la reconciliación.

Los headers comunes no deben incluir `windows.h`, `sys/inotify.h` ni APIs de macOS. La selección se realiza mediante factory y compilación condicional (`WIN32`, `APPLE`, `UNIX`).

## 9. Consideraciones previas

### Alcance

- Plataformas del MVP; recomendación: Windows primero con contrato portable.
- Raíces configurables; nunca todo el disco por defecto.
- Extensiones admitidas según la capacidad del orquestador.
- Exclusiones: `.git`, builds, cachés, temporales, modelos y datos de la aplicación.
- Política para ocultos, remotos, tamaño máximo y tipos desconocidos.
- Symlinks/junctions; recomendación inicial: no seguirlos.
- Movimientos entre raíces y retención de tombstones.

### Identidad y rutas

- No usar sólo la ruta como identidad.
- Guardar ruta normalizada para comparar y una ruta mostrable.
- Windows suele comparar sin distinguir mayúsculas; Linux sí las distingue.
- Considerar Unicode, rutas largas y volúmenes diferentes.
- Usar volumen + file ID/inode cuando exista, sin tratarlo como universal.
- No resolver enlaces de modo que se escape de raíces autorizadas.

### Eventos y estabilidad

- Los eventos pueden duplicarse, perderse o llegar fuera de orden.
- Los editores suelen guardar mediante temporal + rename.
- `Created` no implica que la escritura haya terminado.
- Debounce y estabilidad deben ser configurables, no sleeps bloqueantes.
- Un archivo que no se estabiliza debe producir reintentos limitados y diagnóstico.
- El hash completo normalmente pertenece al orquestador; añadirlo aquí sólo si se acuerda como parte del contrato de identidad.

### Rendimiento

- La outbox debe tener cuotas o límites.
- El callback nativo hace trabajo mínimo y nunca espera extracción.
- Scanner y reconciliación soportan cancelación y recorrido incremental.
- No se carga contenido completo en memoria.
- Medir eventos recibidos/coalescidos, outbox, latencia, overflows y errores.
- Aplicar backoff si el orquestador no está disponible.

### Seguridad y privacidad

- Ejecutar con mínimo privilegio.
- No atravesar raíces autorizadas ni seguir enlaces fuera de ellas.
- No registrar contenido; permitir redactar rutas sensibles.
- Validar rutas nativas antes de publicarlas.
- Proteger catálogo y outbox porque revelan ubicaciones.
- Excluir archivos generados por la aplicación para evitar bucles.

### Operación

- Cierre ordenado mediante stop token o señal.
- Una instancia por perfil o conjunto de raíces.
- Configuración y esquema versionados.
- Logs estructurados con rotación.
- Estado: watchers activos, última reconciliación, outbox y último error.
- Al reiniciar, recuperar entregas pendientes sin inventar ACKs.

## 10. Persistencia exclusiva del módulo

El módulo puede necesitar persistencia operativa, pero no debe apropiarse del almacenamiento semántico.

### `watch_roots`

- `id`, ruta mostrable y normalizada;
- recursividad y seguimiento de enlaces;
- filtros, exclusiones y estado;
- cursor nativo opcional y última reconciliación.

### `observed_files`

- `observation_id`, `root_id`;
- `display_path`, `normalized_path`;
- `native_file_id` opcional, tamaño y `mtime`;
- última generación, `last_seen_at` y tombstone opcional.

Este catálogo representa lo observado, no el estado de indexación.

### `event_outbox`

- `event_id`, payload y versión del contrato;
- `state`, `attempts`, `available_at`, `lease_until`;
- `created_at`, `delivered_at`, `last_error`;
- clave de deduplicación por observación/generación/tipo.

SQLite en modo WAL es apropiado para un daemon local. Las tablas de archivos indexados, chunks, vectores, entidades y grafo pertenecen a la persistencia semántica del orquestador.

## 11. Encaje con el repositorio

El repositorio ya separa extractores, chunking, embeddings, storage y search. Este módulo debe permanecer en `scanner/` o renombrarse a `monitoring/`. No debe incorporar un `IndexCoordinator`.

```text
Backend/include/semantic_fs/scanner/
  file_change.h
  watch_config.h
  i_file_watcher.h
  i_file_change_sink.h
  initial_scanner.h
  event_normalizer.h
  policy_filter.h
  event_coalescer.h
  file_stability_probe.h
  observation_catalog.h
  event_outbox.h
  reconciliation_service.h
  file_monitor_service.h
  file_watcher_factory.h
  windows_file_watcher.h
  linux_file_watcher.h

Backend/src/scanner/
  initial_scanner.cpp
  event_normalizer.cpp
  policy_filter.cpp
  event_coalescer.cpp
  file_stability_probe.cpp
  observation_catalog.cpp
  event_outbox.cpp
  reconciliation_service.cpp
  file_monitor_service.cpp
  file_watcher_factory.cpp
  windows_file_watcher.cpp
  linux_file_watcher.cpp
```

El orquestador implementa o recibe una implementación de `IFileChangeSink`. Esa es la frontera entre módulos:

```text
scanner -> IFileChangeSink <- orchestrator
                              |
                              +-> extractors
                              +-> processing
                              +-> chunking
                              +-> embeddings
                              +-> storage/graph
```

## 12. Lista de tareas

### Fase 0 — Frontera y contrato

- [ ] Confirmar que el módulo termina al entregar `FileChange` al orquestador.
- [ ] Definir plataformas, raíces, inclusiones, exclusiones, tamaño y symlinks.
- [ ] Crear `FileChangeKind`, `FileChange`, `WatchConfig` e `IFileWatcher`.
- [ ] Crear `IFileChangeSink` y resultados de confirmación/reintento.
- [ ] Definir `eventId`, `generation`, renombre y eliminación.
- [ ] Documentar entrega al menos una vez y deduplicación del orquestador.
- [ ] Acordar rutas/metadatos frente a snapshots; usar rutas para el MVP.

**Criterio de salida:** el contrato funciona con un sink falso sin enlazar extractores, chunking ni storage semántico.

### Fase 1 — Escaneo y políticas

- [ ] Implementar normalización de rutas por plataforma.
- [ ] Validar pertenencia a raíces autorizadas.
- [ ] Implementar `PolicyFilter` y exclusiones de archivos de la aplicación.
- [ ] Implementar `InitialScanner` recursivo, cancelable y tolerante a errores.
- [ ] Definir identidad de observación y generación.
- [ ] Probar Unicode, rutas largas, permisos y enlaces.

**Criterio de salida:** se publican exactamente los archivos admitidos sin interpretar contenido ni escapar de las raíces.

### Fase 2 — Entrega confiable

- [ ] Añadir SQLite y migraciones al build/vcpkg.
- [ ] Implementar `watch_roots`, `observed_files` y `event_outbox`.
- [ ] Configurar WAL, busy timeout y migraciones.
- [ ] Implementar publicación con ACK y deduplicación por `eventId`.
- [ ] Implementar lease, reintentos y backoff.
- [ ] Recuperar eventos pendientes después de reiniciar.
- [ ] Probar sink directo y sink respaldado por outbox.

**Criterio de salida:** un reinicio o fallo del orquestador no pierde eventos ni los marca como indexados.

### Fase 3 — Coalescing y estabilidad

- [ ] Implementar `EventCoalescer` por identidad y generación.
- [ ] Implementar `FileStabilityProbe` con tamaño y `mtime`.
- [ ] Hacer configurables debounce, timeout y reintentos.
- [ ] Modelar guardados atómicos y temporales.
- [ ] Impedir que una generación vieja se publique después de una nueva.
- [ ] Probar escrituras lentas y ráfagas.

**Criterio de salida:** notificaciones transitorias producen una entrega útil de la última versión observada.

### Fase 4 — Watcher de Windows (MVP)

- [ ] Implementar `WindowsFileWatcher` con `ReadDirectoryChangesW` asíncrono.
- [ ] Observar subdirectorios y soportar cancelación limpia.
- [ ] Correlacionar pares de renombre.
- [ ] Detectar overflow y programar reconciliación.
- [ ] Tratar rutas largas, bloqueos y junctions según política.
- [ ] Añadir pruebas para Created/Modified/Removed/Renamed.

**Criterio de salida:** los cambios se convierten en `FileChange` portables sin invocar módulos semánticos.

### Fase 5 — Reconciliación y robustez

- [ ] Implementar `ObservationCatalog` con snapshot mínimo.
- [ ] Reconciliar al inicio, por intervalo y después de overflow.
- [ ] Publicar diferencias con `source = reconciliation`.
- [ ] Gestionar tombstones y movimientos entre raíces.
- [ ] Añadir cuotas y backpressure.
- [ ] Añadir métricas, logs y comando de estado.
- [ ] Simular crashes y pérdida de eventos.

**Criterio de salida:** tras overflow, tiempo offline o crash, el módulo converge con el disco y entrega diferencias.

### Fase 6 — Integración con el orquestador

- [ ] Implementar el adaptador `IFileChangeSink -> Orchestrator`.
- [ ] Verificar deduplicación de `eventId` en el orquestador.
- [ ] Verificar rechazo de generaciones obsoletas.
- [ ] Definir respuesta ante ruta desaparecida, versión distinta o inaccesible.
- [ ] Probar los cinco tipos de cambio de extremo a extremo.
- [ ] Confirmar que scanner no importa headers de extractores, chunking, embeddings o grafo.

**Criterio de salida:** el orquestador recibe cambios confiables y conserva el control exclusivo del pipeline semántico.

### Fase 7 — Multiplataforma y daemon

- [ ] Implementar `LinuxFileWatcher` con inotify.
- [ ] Implementar `MacOsFileWatcher` con FSEvents si entra en alcance.
- [ ] Ejecutar las mismas pruebas de contrato en cada plataforma.
- [ ] Añadir CLI: `scan`, `watch`, `reconcile` y `status`.
- [ ] Empaquetar como Windows Service/systemd/launchd.
- [ ] Documentar instalación, recuperación y desinstalación.

**Criterio de salida:** el servicio inicia automáticamente, se detiene limpiamente y conserva entregas pendientes.

## 13. Pruebas imprescindibles

- El escaneo inicial entrega todos los archivos admitidos.
- Dos reconciliaciones sin cambios no generan nuevas versiones.
- Una escritura lenta no publica prematuramente una versión transitoria.
- Diez modificaciones rápidas dejan prevalecer la última generación.
- Un guardado `temp -> rename` identifica el archivo final.
- Un renombre publica `Renamed` cuando puede correlacionarse.
- Un movimiento hacia fuera/dentro publica eliminación/descubrimiento.
- Un reinicio antes del ACK reenvía el mismo `eventId`.
- Con el orquestador caído, la outbox conserva y reintenta.
- Un overflow simulado dispara reconciliación.
- Un archivo sin permisos no detiene otros eventos.
- Un symlink/junction cíclico no escapa de la raíz ni genera bucles.
- El módulo compila con un sink falso sin dependencias semánticas.

## 14. Primer incremento recomendado

El primer incremento valida únicamente la frontera filesystem-orquestador en Windows:

1. Definir `FileChange`, `IFileWatcher` e `IFileChangeSink`.
2. Escanear una raíz con filtros y un sink falso.
3. Implementar SQLite para catálogo de observación y outbox.
4. Implementar `WindowsFileWatcher` alimentando la misma ruta de eventos.
5. Añadir coalescing, estabilidad y reconciliación manual.
6. Conectar un adaptador mínimo al orquestador.
7. Probar entrega, deduplicación y generaciones obsoletas.

El éxito del módulo se mide por si describe de forma confiable lo ocurrido en el filesystem, no por si extrae o busca contenido. Esa responsabilidad comienza después de `IFileChangeSink` y pertenece al orquestador.
