# Plan implementable del módulo de monitoreo de archivos

## Veredicto y objetivo

El módulo tiene sentido si se construye como una pieza autónoma y testeable cuya responsabilidad termina en `IFileChangeSink`. Debe observar raíces configuradas, normalizar cambios, conservarlos de forma durable y entregarlos **al menos una vez**. No debe conocer ni anticipar el diseño interno de un orquestador semántico.

El desarrollo será Windows-first, manteniendo contratos portables. La solución conserva cuatro decisiones fundamentales: watcher nativo más reconciliación, callbacks livianos, outbox durable y separación entre persistencia operativa y semántica.

### Dentro del alcance

- Escaneo inicial y reconciliación de raíces configuradas.
- Observación de altas, modificaciones, eliminaciones y renombres.
- Normalización de rutas, metadatos y eventos entre plataformas.
- Filtros, coalescing y comprobación básica de estabilidad.
- Catálogo operativo y outbox SQLite.
- Recuperación tras reinicio, overflow y fallos temporales de entrega.
- Entrega por el puerto `IFileChangeSink`.
- Biblioteca reutilizable, ejecutable fino y pruebas automatizadas.

### Fuera del alcance

- Diseñar o implementar el orquestador futuro.
- Extracción, OCR, limpieza, chunking, embeddings, búsqueda, grafo o indexación.
- Confirmar que un archivo fue indexado.
- Invalidar derivados semánticos.
- Copiar el contenido o producir snapshots inmutables en el MVP.
- Implementar ahora el adaptador real hacia el orquestador.

Durante el escaneo inicial, `FakeFileObservationSink` representa al coordinador pre-durable: recibe `FileObservation` sin IDs durables ni generación. `IFileChangeSink` conserva el límite post-outbox para cambios durables; el scanner no lo invoca.

## Ruta de implementación

1. Separar una biblioteca testeable y habilitar CTest.
2. Definir contratos y completar un scanner one-shot con `IFileObservationSink` falso; reservar `IFileChangeSink` para el publicador post-outbox.
3. Implementar el watcher de Windows y el protocolo de arranque seguro.
4. Incorporar catálogo y outbox en una única frontera transaccional.
5. Añadir recovery, reconciliación, overflow y operación observable.
6. Dejar documentado, pero no implementar, el adaptador al orquestador futuro.

Cada paso debe cerrar con pruebas verificables antes de agregar el siguiente subsistema.

## Arquitectura y frontera

```text
Sistema de archivos
        |
        v
+-------------------------------+
| InitialScanner + IFileWatcher |
+---------------+---------------+
                |
                v
+-------------------------------+
| normalize + filter + coalesce |
| stability + reconcile         |
+---------------+---------------+
                |
                v
+-------------------------------+
| ObservationCatalog + Outbox   |
| transacción SQLite única       |
+---------------+---------------+
                |
                v
+-------------------------------+
| IFileChangeSink               |  <- fin del módulo
+-------------------------------+
```

### Componentes

| Componente | Responsabilidad |
|---|---|
| `InitialScanner` | Recorrer una raíz de forma cancelable y tolerante a errores. |
| `IFileWatcher` | Iniciar, detener y reportar notificaciones nativas u overflow. |
| `EventNormalizer` | Convertir eventos nativos en observaciones portables. |
| `PolicyFilter` | Aplicar raíces, exclusiones, extensiones, tamaño y política de enlaces. |
| `EventCoalescer` | Reducir ráfagas sin invertir el orden lógico aceptado. |
| `FileStabilityProbe` | Evitar entregas prematuras durante escrituras en curso. |
| `ObservationCatalog` | Conservar el estado operativo observado y tombstones. |
| `EventOutbox` | Conservar entregas pendientes, leases, reintentos y estados terminales. |
| `ReconciliationService` | Comparar catálogo y disco al iniciar, periódicamente y tras overflow. |
| `FileMonitorService` | Ser dueño del ciclo de vida y coordinar los componentes anteriores. |
| `IFileChangeSink` | Puerto de salida; no expone capacidades semánticas. |

Los adaptadores nativos quedan detrás de `IFileWatcher`. Los headers comunes no incluyen `windows.h`, `sys/inotify.h` ni APIs de macOS.

## Invariantes del dominio

Estas invariantes deben estar documentadas y cubiertas por pruebas antes de implementar persistencia.

### Raíces, rutas e identidad

1. Cada raíz tiene un `rootId` durable, independiente de su posición en la configuración.
2. En el MVP se rechazan raíces superpuestas. Así, cada ruta aceptada tiene un único propietario y no genera eventos duplicados entre raíces.
3. Una observación tiene un `observationId` opaco y durable. La ruta no es su identidad.
4. `nativeFileId` puede correlacionar un renombre dentro del mismo volumen, pero es opcional, puede reciclarse y nunca es la única prueba de identidad.
5. Un renombre correlacionado dentro de la misma raíz conserva `observationId` e incrementa `generation`.
6. Sin correlación confiable, un renombre se representa como `Removed` más `Created`; no se inventa una relación.
7. Un movimiento entre raíces se representa como `Removed` en la raíz de origen y `Discovered` en la raíz de destino.
8. `Removed` cierra la encarnación y conserva un tombstone. Un archivo recreado en la misma ruta recibe un nuevo `observationId` y comienza en `generation = 1`.
9. Los reinicios recuperan `rootId`, `observationId`, tombstones y última generación desde SQLite; nunca reconstruyen esas identidades desde el orden del escaneo.

### Generación y evento

1. `generation` es un contador monotónico por `observationId`, no un reloj ni una versión de contenido.
2. Sólo se asigna una generación cuando una observación aceptada cambia el estado durable.
3. Dos reconciliaciones equivalentes no incrementan la generación.
4. `eventId` se genera una sola vez al insertar el evento en la outbox, tiene restricción única y permanece idéntico en todos los reintentos.
5. `eventId` no depende únicamente de ruta o timestamp. Puede ser un UUID persistido o una clave derivada de `rootId + observationId + generation + kind` con namespace de instalación.
6. El orden de generaciones se serializa en el escritor SQLite. Ningún callback nativo asigna generaciones.

## Protocolo de arranque sin ventana de pérdida

El orden `snapshot -> watcher` es inválido: un cambio entre ambos pasos podría no aparecer en ninguno. El arranque debe seguir este protocolo:

1. Abrir el catálogo/outbox y recuperar entregas pendientes.
2. Iniciar el watcher de cada raíz en modo bufferizado.
3. Registrar un marcador de inicio y mantener los callbacks limitados a encolar notificaciones.
4. Ejecutar el snapshot inicial mientras los eventos continúan acumulándose.
5. Enviar snapshot y buffer al coordinador serializado; un overflow marca la raíz como `dirty`.
6. Ejecutar una reconciliación de cutover contra el estado actual del disco.
7. Drenar los eventos acumulados durante esa reconciliación hasta una barrera del watcher.
8. Pasar a operación normal sólo cuando snapshot, reconciliación y barrera se hayan procesado.

El snapshot no pretende ser una fotografía atómica del sistema de archivos. La garantía es que todo cambio posterior al inicio del watcher queda representado por una notificación o por una marca `dirty` que obliga a reconciliar. La ruta serializada impide que un resultado tardío del scanner sobrescriba una observación más reciente.

Si el buffer se satura o el SO reporta overflow, el servicio no declara el arranque saludable: conserva la raíz como `dirty`, completa una nueva reconciliación y sólo entonces habilita el estado normal.

## Frontera transaccional SQLite

Para cada cambio durable, una sola transacción debe:

1. leer y validar el estado actual de la observación;
2. crear o actualizar el catálogo/tombstone;
3. asignar la siguiente `generation`;
4. crear el `eventId`;
5. insertar el payload versionado en `event_outbox`.

El commit hace visibles catálogo y evento juntos. Está prohibido avanzar `observed_files` sin insertar su evento, o insertar un evento cuya generación no coincida con el catálogo.

La entrega ocurre después del commit. Un ACK sólo cambia el estado de la fila de outbox a entregada; significa que el sink aceptó la responsabilidad del mensaje, **no** que el archivo fue indexado. La persistencia del monitor nunca contiene chunks, vectores, entidades ni estado semántico.

SQLite en modo WAL, con migraciones y `busy_timeout`, es apropiado para este daemon local. Debe existir un único escritor lógico para catálogo y outbox; los lectores y el publicador no asignan generaciones.

## Contrato de salida y estados de entrega

El payload persistente incluye como mínimo:

- `schemaVersion`;
- `eventId`, `rootId`, `observationId` y `generation`;
- `kind`: `Discovered`, `Created`, `Modified`, `Removed` o `Renamed`;
- ruta actual y `previousPath` opcional;
- tamaño, modificación y `nativeFileId` opcionales;
- `observedAt` y `source`: `watcher`, `initial_scan` o `reconciliation`.

`IFileChangeSink::publish` devuelve uno de estos estados cerrados:

| Resultado | Efecto en la outbox |
|---|---|
| `Accepted` | Marcar entregado. No implica indexación. |
| `Duplicate` | Marcar entregado: el consumidor ya posee el mismo `eventId`. |
| `RetryableFailure` | Conservar pendiente y reintentar con backoff y jitter. |
| `Rejected` | No reintentar automáticamente; mover a dead-letter o estado terminal con causa observable. |

Los reintentos tienen límite de frecuencia, no de durabilidad: un fallo temporal no elimina el evento. La operación debe permitir inspeccionar y reactivar manualmente un dead-letter después de corregir su causa.

### Outbox llena

- Una cuota blanda activa backpressure: prioriza entrega, pausa scanner/reconciliación y reduce trabajo nuevo.
- Al alcanzar la cuota dura, no se actualiza el catálogo si el evento correspondiente no puede insertarse en la misma transacción.
- El watcher mantiene una marca `dirty` por raíz; no intenta conservar indefinidamente cada notificación en memoria.
- El servicio pasa a estado `degraded/saturated`, expone la causa y reconcilia cuando vuelve a existir capacidad.
- Nunca se descartan filas pendientes ni se inventan ACKs para recuperar espacio.

## Concurrencia, ownership y cierre

| Recurso | Propietario y regla |
|---|---|
| Handle del watcher | Adaptador de plataforma; `start/stop` son idempotentes y el cierre espera callbacks en curso. |
| Cola nativa | Acotada; el callback sólo copia datos mínimos o marca overflow/dirty. |
| Coordinador | Un consumidor serializa normalización final, catálogo, generaciones y outbox. |
| Scanner/reconciliador | Trabajo cancelable; entrega observaciones al coordinador, no escribe SQLite directamente. |
| Publicador | Worker separado que toma leases de outbox y llama al sink fuera de transacciones SQLite. |
| Relojes | `IClock` inyectable: reloj monotónico para debounce/backoff y reloj UTC para timestamps persistentes. |

El shutdown sigue este orden: dejar de aceptar trabajo de control, detener watchers, cancelar escaneos, drenar hasta un límite configurado, devolver leases no confirmados a estado pendiente, hacer checkpoint si corresponde y cerrar SQLite. Al reiniciar, todo evento sin ACK vuelve a ser elegible con el mismo `eventId`.

La saturación de la cola nativa nunca bloquea el callback del SO. Se marca la raíz `dirty` y se usa reconciliación para converger.

## Contratos persistentes e interoperabilidad

### Rutas

- La ruta mostrable conserva Unicode y se persiste como UTF-8.
- La clave de comparación se obtiene de una ruta absoluta, léxicamente normalizada y relativa a su raíz.
- Windows aplica su semántica de comparación sin distinguir mayúsculas; Linux mantiene distinción. No se impone una regla universal.
- La validación comprueba nuevamente la pertenencia a la raíz antes de leer metadatos y antes de publicar, reduciendo carreras TOCTOU.
- En el MVP no se siguen symlinks, junctions ni reparse points. Se registran y omiten sin atravesarlos.
- Errores de ACL, desapariciones durante el recorrido y archivos bloqueados afectan a esa ruta, no detienen la raíz completa.

### Timestamps, payloads y enums

- Los timestamps persistentes son enteros UTC con unidad explícita, por ejemplo microsegundos desde Unix epoch.
- `std::filesystem::file_time_type` no cruza SQLite ni el puerto de salida.
- El payload tiene `schemaVersion` y migraciones compatibles; una versión no soportada se rechaza explícitamente.
- Los enums persistidos usan valores definidos y cerrados. Un valor desconocido no se convierte silenciosamente a un default.
- `observedAt` describe cuándo se observó el estado; `modifiedAt` es metadato del archivo y puede faltar.

### Degradación por plataforma

| Plataforma | Adaptador | Semántica esperada |
|---|---|---|
| Windows | `ReadDirectoryChangesW` asíncrono | MVP; pares de rename cuando estén disponibles, overflow, rutas largas y reparse points. |
| Linux | `inotify` | Requiere watches por directorio; los límites y `IN_Q_OVERFLOW` fuerzan reconciliación. |
| macOS | FSEvents | Entrega cambios coalescidos por árbol, no equivalentes uno a uno; cursor y flags sólo indican qué debe reexaminarse. |

No se promete equivalencia de notificaciones nativas. El contrato portable describe el estado inferido después de reexaminar el disco. Linux y macOS quedan como fases posteriores al MVP de Windows.

## Fundación: biblioteca testeable

El CMake actual reúne `src/*.cpp` en un único ejecutable. Antes del monitor se debe separar el código reutilizable:

1. Crear el target de biblioteca `semantic_fs_core` con las fuentes reutilizables y sin `main.cpp`.
2. Publicar `Backend/include` mediante `target_include_directories(semantic_fs_core PUBLIC ...)`.
3. Enlazar en la biblioteca sólo sus dependencias reales; SQLite se incorpora cuando comienza la fase durable.
4. Mantener `semantic_fs_backend` como ejecutable fino: contiene composición/configuración y enlaza `semantic_fs_core`.
5. Sustituir el glob indiscriminado por listas de fuentes explícitas o, como transición, excluir claramente `main.cpp` de la biblioteca.
6. Habilitar `include(CTest)`/`enable_testing()` y añadir Catch2 v3 mediante vcpkg para obtener discovery y tests aislados.
7. Crear `semantic_fs_core_tests`, enlazado a la biblioteca, y registrar casos con CTest.
8. Separar pruebas unitarias, de contrato de sinks/watchers e integración SQLite; las pruebas nativas de Windows se etiquetan por plataforma.

Esta estructura permite probar scanner, normalización, identidad, transacciones y recovery sin iniciar el backend ni enlazar el futuro orquestador. El cambio de CMake y dependencias pertenece a la primera fase de implementación; este documento no modifica el build.

## Persistencia operativa mínima

| Tabla | Datos esenciales |
|---|---|
| `watch_roots` | `root_id`, rutas mostrable/comparable, políticas, estado dirty y última reconciliación. |
| `observed_files` | `observation_id`, `root_id`, rutas, identidad nativa opcional, metadatos, generación y tombstone. |
| `event_outbox` | `event_id`, versión/payload, estado, intentos, lease, disponibilidad, errores y timestamps. |
| `dead_letters` o estado terminal | Evento rechazado, causa, fecha y datos necesarios para inspección/reactivación. |

Una restricción única protege `event_id`; otra protege la combinación lógica elegida para observación/generación/tipo. Las migraciones se prueban desde una base vacía y desde la versión anterior.

## Fases pequeñas y criterios de salida

### Fase 0 — Build y pruebas

- [ ] Crear `semantic_fs_core` y mantener `semantic_fs_backend` como composición fina.
- [ ] Habilitar CTest, framework y target de pruebas.
- [ ] Añadir un smoke test que enlace la biblioteca.

**Salida verificable:** CMake configura; el ejecutable enlaza la biblioteca; CTest descubre y ejecuta al menos un test.

### Fase 1 — Contratos y scanner one-shot

- [x] Definir tipos persistibles, `IFileWatcher`, `IFileObservationSink`, `IFileChangeSink`, `IClock` y resultados cerrados.
- [x] Implementar normalización, pertenencia a raíces y `PolicyFilter`.
- [x] Implementar `InitialScanner` cancelable con `FakeFileObservationSink`; `IFileChangeSink` permanece post-outbox.
- [x] Probar scanner determinista con ACL, desaparición TOCTOU, enlaces/reparse points, cancelación y backpressure; Unicode y rutas largas continúan como cobertura de adaptador nativo.

**Evidencia final revalidada (2026-09-01):** el build Debug finalizó correctamente; `ctest -R monitoring` pasó 8/8 pruebas y `make -C Backend test` pasó 10/10. La validación de identificadores dispone de una ruta pura y sin asignaciones; la configuración rechaza raíces solapadas; la semántica nativa cubre rutas Windows legacy-long. La fixture prueba orden determinista, errores acotados por ruta relativa, retención de observaciones previas al cancelar y continuidad de errores hasta una posterior parada del sink. El scanner sólo produce `FileObservation` pre-durables: no asigna identidad durable, generación ni eventos, y no invoca `IFileChangeSink`.

**Límites:** esta fase no implementa watcher, cutover, SQLite/catálogo/outbox, reconciliación, reintentos, orquestación, OCR ni indexación, ni promete un snapshot atómico. Los adaptadores nativos siguen limitados a Windows; no existe sustituto POSIX.

**Salida verificable:** una raíz de fixture entrega sólo `FileObservation` admitidos al fake pre-durable, sin IDs durables, generación ni dependencias semánticas.
### Fase 2 — Watcher Windows y arranque seguro

- [ ] Implementar `WindowsFileWatcher` asíncrono con cola acotada y cancelación.
- [ ] Implementar buffer, barreras, snapshot, cutover y marca dirty.
- [ ] Correlacionar renombres sólo con evidencia suficiente.
- [ ] Probar cambios durante el snapshot, ráfagas, overflow y shutdown.

**Salida verificable:** ningún cambio inyectado en las ventanas del arranque queda sin evento o reconciliación pendiente.

### Fase 3 — Catálogo/outbox transaccionales y recovery

- [ ] Añadir SQLite, migraciones, WAL y escritor único.
- [ ] Implementar la transacción catálogo-generación-evento.
- [ ] Implementar leases, ACK, backoff, dead-letter y cuota.
- [ ] Probar crash antes/después del commit y antes/después del ACK.

**Salida verificable:** tras cada crash simulado, catálogo y outbox son coherentes; un evento pendiente conserva su `eventId`.

### Fase 4 — Reconciliación y robustez operativa

- [ ] Implementar coalescing y estabilidad con reloj inyectable.
- [ ] Reconciliar al iniciar, por intervalo, tras overflow y tras saturación.
- [ ] Implementar tombstones, delete+recreate y movimientos entre raíces.
- [ ] Exponer métricas, health y status verificables.

**Salida verificable:** después de pérdida simulada de eventos, el catálogo converge con el disco y cada diferencia durable tiene una fila de outbox.

### Fase posterior — Adaptador al orquestador

No pertenece al alcance actual. Cuando exista un contrato real del orquestador, se implementará un adaptador de `IFileChangeSink` y se ejecutarán pruebas de contrato. El módulo no debe cambiar su watcher ni su persistencia para acomodar detalles semánticos.

## Observabilidad y operación

Las siguientes señales deben ser consultables y tener criterios objetivos:

| Señal | Criterio |
|---|---|
| Health de raíz | `healthy` sólo si watcher activo, arranque completado y raíz no dirty/saturated. |
| Outbox | Conteos por estado, edad del pendiente más antiguo y uso de cuota. |
| Reconciliación | Último inicio/fin, duración, resultado y motivo. |
| Watcher | Eventos recibidos/coalescidos, overflows y profundidad máxima de cola. |
| Entrega | Latencia, Accepted/Duplicate/Retryable/Rejected y próximo reintento. |
| Error | Último error por componente sin registrar contenido ni rutas sensibles completas. |

`status` debe indicar explícitamente si el servicio sigue convergiendo, está saturado o requiere intervención por dead-letters. Un proceso vivo no equivale a un monitor saludable.

## Pruebas de aceptación imprescindibles

- [ ] El watcher empieza antes del snapshot y los cambios durante el arranque convergen.
- [ ] Dos reconciliaciones sin cambios no crean generaciones ni eventos nuevos.
- [ ] Catálogo, generación y outbox se confirman o revierten juntos.
- [ ] Un reinicio antes del ACK reenvía el mismo `eventId`.
- [ ] `Accepted` y `Duplicate` cierran la entrega; `RetryableFailure` reintenta; `Rejected` termina en dead-letter.
- [ ] Una outbox llena no avanza el catálogo sin evento y deja la raíz dirty/saturated.
- [ ] Una escritura lenta no publica una versión transitoria.
- [ ] Un guardado `temp -> rename` converge al archivo final.
- [ ] Delete+recreate crea otra identidad; rename correlacionado conserva identidad.
- [ ] Un movimiento entre raíces produce salida en cada raíz sin duplicación.
- [ ] Overflow, caída del sink y crash convergen después de recovery.
- [ ] ACL, archivos bloqueados, TOCTOU y reparse points no detienen otras rutas ni escapan de la raíz.
- [ ] El scanner compila y se prueba con `FakeFileObservationSink`; `IFileChangeSink` sólo se prueba como puerto post-outbox, sin headers ni bibliotecas semánticas.

## Decisiones de arquitectura pendientes

Antes de cada fase se debe registrar una ADR breve; no es necesario resolver decisiones del orquestador.

| ADR | Decisión que debe cerrar |
|---|---|
| Identidad y generación | `rootId`, `observationId`, tombstones, recreación, correlación de rename y formato de `eventId`. |
| Protocolo de arranque | Buffer, barreras, cutover, overflow y condición para declarar una raíz saludable. |
| Concurrencia y ownership | Hilos, capacidad de colas, escritor SQLite, leases, cancelación y shutdown. |
| Transacción catálogo-outbox | Límites de la transacción, restricciones únicas, ACK, recovery y comportamiento de cuota. |

## Checklist de límite arquitectónico

- [ ] El código reusable vive en `semantic_fs_core` y no depende de `main.cpp`.
- [ ] `InitialScanner` termina en `IFileObservationSink`; el módulo durable termina en `IFileChangeSink` después del outbox.
- [ ] Las pruebas usan fake/contract sinks; no simulan un orquestador semántico.
- [ ] Los callbacks nativos no realizan I/O de SQLite ni procesamiento pesado.
- [ ] Toda observación durable y su evento se escriben atómicamente.
- [ ] La entrega es al menos una vez y un ACK no significa indexación.
- [ ] Reconciliación es obligatoria; ninguna plataforma depende sólo de su watcher.
- [ ] La persistencia operativa no contiene datos semánticos.
- [ ] Linux/macOS se agregan detrás del mismo contrato después del MVP Windows.

El éxito del módulo se mide por describir y entregar de forma durable lo ocurrido en las raíces observadas. Todo procesamiento del contenido comienza después de `IFileChangeSink` y queda deliberadamente fuera de este plan.
