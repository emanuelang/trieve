# Equipo de agentes — Módulo de monitoreo de archivos

## Propósito

Este directorio define perfiles delegables para implementar el módulo descrito en [`docs/PLAN_MODULO_MONITOREO_E_INDEXACION.md`](../../docs/PLAN_MODULO_MONITOREO_E_INDEXACION.md).

El módulo termina al entregar un `FileChange` mediante `IFileChangeSink`. Ningún agente de este equipo debe implementar extracción, OCR, limpieza, chunking, embeddings, búsqueda o grafo, salvo los cambios mínimos de integración expresamente acordados con el orquestador.

## Perfiles

| Perfil | Archivo | Ownership principal | Dependencia |
|---|---|---|---|
| Arquitectura y contratos | [01_arquitecto_contratos.md](01_arquitecto_contratos.md) | Modelo de dominio, interfaces y ADR | Ninguna |
| Núcleo de descubrimiento | [02_ingeniero_scanner_reconciliacion.md](02_ingeniero_scanner_reconciliacion.md) | Scanner, políticas, catálogo y reconciliación | Contratos |
| Plataforma Windows | [03_ingeniero_watcher_windows.md](03_ingeniero_watcher_windows.md) | `ReadDirectoryChangesW` y normalización nativa | Contratos |
| Confiabilidad y persistencia | [04_ingeniero_outbox_confiabilidad.md](04_ingeniero_outbox_confiabilidad.md) | SQLite, outbox, ACK, retry y recovery | Contratos |
| Integración con orquestador | [05_ingeniero_integracion_orquestador.md](05_ingeniero_integracion_orquestador.md) | Adaptador `IFileChangeSink` y semántica E2E | Contratos + outbox |
| QA, concurrencia y portabilidad | [06_ingeniero_qa_portabilidad.md](06_ingeniero_qa_portabilidad.md) | Tests de contrato, estrés, fallos y CI | Todos |

## Orden de ejecución

```text
Arquitectura y contratos
          |
          +----------------+-------------------+
          |                |                   |
          v                v                   v
 Scanner/reconcile   Watcher Windows     Outbox/recovery
          |                |                   |
          +----------------+-------------------+
                           |
                           v
              Integración con orquestador
                           |
                           v
                 QA final y portabilidad
```

Después de estabilizar contratos, scanner, watcher y outbox pueden desarrollarse en paralelo. QA debe participar desde el inicio definiendo fixtures y tests de contrato, aunque la validación integral ocurra al final.

## Reglas de coordinación

1. `file_change.h`, `i_file_watcher.h`, `i_file_change_sink.h` y `watch_config.h` pertenecen al perfil de Arquitectura. Otros agentes proponen cambios, pero no alteran su semántica unilateralmente.
2. Toda modificación de contrato exige actualizar tests, ADR y adaptadores afectados.
3. Los agentes trabajan detrás de interfaces y evitan dependencias circulares.
4. Ningún callback nativo debe ejecutar lógica semántica ni bloquear esperando al orquestador.
5. La garantía es entrega al menos una vez. El monitor reintenta; el orquestador deduplica.
6. Cada entrega debe incluir tests, documentación breve y evidencia de compilación.
7. No se considera terminado un perfil si sólo funciona en el camino feliz.

## Matriz de ownership de archivos

| Área | Owner | Revisores obligatorios |
|---|---|---|
| Interfaces públicas y tipos | Arquitectura | Integración, QA |
| Scanner, filtros, estabilidad, reconciliación | Núcleo | Arquitectura, QA |
| Implementación Win32 | Windows | Arquitectura, QA |
| SQLite y outbox | Confiabilidad | Arquitectura, QA |
| Adaptador al orquestador | Integración | Arquitectura, Confiabilidad |
| Tests compartidos y CI | QA | Owner del componente probado |

## Definition of Done conjunta

- Compila en C++20 sin headers nativos filtrados a contratos comunes.
- Los eventos Created, Modified, Removed y Renamed cumplen el contrato.
- Un overflow dispara reconciliación.
- Reiniciar antes del ACK no pierde el evento y conserva su `eventId`.
- Ráfagas y escrituras lentas no publican generaciones obsoletas.
- El módulo funciona con un sink falso sin enlazar módulos semánticos.
- La integración confirma recepción, no finalización de indexación.
- Sanitizers/análisis estático y tests aplicables pasan.
- La documentación refleja las decisiones finales.

## Capacidad sugerida

Para agentes autónomos pueden activarse cuatro perfiles simultáneos después de cerrar contratos: núcleo, Windows, confiabilidad y QA. Integración comienza cuando el contrato y el ACK estén estables. En un equipo humano pequeño, Arquitectura puede asumir Integración y QA puede asumir portabilidad, reduciendo el equipo a cuatro personas.

