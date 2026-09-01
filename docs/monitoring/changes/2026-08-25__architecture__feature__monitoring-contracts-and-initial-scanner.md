# [MON-001] Feature: contratos de monitoreo y escaneo inicial one-shot

- **Estado:** completado (Fase 1)
- **Fecha:** 2026-08-25
- **Perfil responsable:** architecture
- **Participantes:** architecture, scanner, qa

## Alcance de esta porción

La Fase 1 entrega contratos portables, política de rutas y un `InitialScanner` one-shot síncrono. El scanner recorre un `IFileSystemView` inyectado, entrega únicamente observaciones `FileObservation` pre-durables a `IFileObservationSink` y no conoce el puerto durable `IFileChangeSink`.

## Cambios

- Valores UTF-8, IDs opacos, generaciones y resultados cerrados en `semantic_fs/monitoring`.
- Puertos `IClock`, `IFileWatcher`, `IFileObservationSink` e `IFileChangeSink`.
- Prueba de compilación con fakes que distingue `FileObservation` de `FileChange`.
- Validación pública pura y sin asignaciones para IDs opacos antes de transferir la propiedad del valor.
- Validación de conjuntos de raíces que rechaza igualdad o contención y cobertura nativa de rutas Windows legacy-long.
- Escenarios de cancelación y de errores previos a una posterior parada del sink con observaciones retenidas verificables.
- Decisión registrada en [ADR-0001](../adr/0001-pre-durable-observation-boundary.md).

## Fuera de alcance

No se implementan watcher ni cutover, SQLite/catálogo/outbox, asignación de IDs o generaciones durables, reintentos, reconciliación, orquestación, OCR ni indexación. El scanner no promete un snapshot atómico del sistema de archivos.

## Verificación

- `cmake --build Backend/build --config Debug`: PASS final, salida 0.
- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R monitoring`: PASS revalidado el 2026-09-01, salida 0; 8/8 pruebas.
- `make -C Backend test`: PASS revalidado el 2026-09-01, salida 0; configuración y build Debug completados; CTest 10/10 pruebas.

## Seguimiento

- [x] Implementar semántica de rutas y política neutral.
- [x] Implementar `InitialScanner` y completar la evidencia de Fase 1.
- [ ] Continuar con watcher, cutover y persistencia durable en una fase posterior.

## Evidencia observada — Unidad 2

- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R path_policy`: PASS, 2/2 pruebas, 0.12 s.
- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R native_file_system_view`: PASS, 1/1 prueba, 0.07 s (Windows).
- `make -C Backend test`: PASS; configuración, build Debug y CTest completados, 7/7 pruebas, 0.40 s.
- La disponibilidad nativa queda publicada como `SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE=1` en Windows y `0` fuera de Windows, sin adaptador POSIX sustituto.
## Evidencia observada — Unidad 3

- `cmake --build Backend/build --config Debug`: PASS final, salida 0.
- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R monitoring`: PASS revalidado el 2026-09-01, salida 0; 8/8 pruebas.
- `make -C Backend test`: PASS revalidado el 2026-09-01, salida 0; configuración y build Debug completados; CTest 10/10 pruebas.
- La fixture valida descubrimiento anidado ordenado, cancelación antes de descenso/metadatos/entrega, errores ACL y TOCTOU con diagnósticos relativos acotados, raíz no disponible y parada por backpressure/rechazo.
- `InitialScanner` vuelve a comprobar la pertenencia a la raíz antes de metadatos y entrega; sólo emite `FileObservation` con `Discovered`/`InitialScan`, sin IDs, generaciones ni llamadas a `IFileChangeSink`.

## Límites y seguimiento

La cobertura nativa de adaptadores sigue siendo exclusiva de Windows; no se sustituye por un adaptador POSIX. Las fases posteriores deben incorporar watcher, arranque seguro, persistencia SQLite/catálogo/outbox, reconciliación e indexación fuera de esta unidad. No se afirma un snapshot atómico del sistema de archivos.
