# [MON-001] Feature: contratos de monitoreo y escaneo inicial one-shot

- **Estado:** en progreso
- **Fecha:** 2026-08-25
- **Perfil responsable:** architecture
- **Participantes:** architecture, scanner, qa

## Alcance de esta porción

Se introducen contratos portables y fakes de compilación. `InitialScanner` aún no está implementado: cuando exista, entregará `FileObservation` a `IFileObservationSink`. `IFileChangeSink` queda reservado para el publicador durable post-outbox.

## Cambios

- Valores UTF-8, IDs opacos, generaciones y resultados cerrados en `semantic_fs/monitoring`.
- Puertos `IClock`, `IFileWatcher`, `IFileObservationSink` e `IFileChangeSink`.
- Prueba de compilación con fakes que distingue `FileObservation` de `FileChange`.
- Decisión registrada en [ADR-0001](../adr/0001-pre-durable-observation-boundary.md).

## Fuera de alcance

No se implementan rutas, política, adaptadores nativos, watcher, scanner, SQLite, outbox, asignación durable, OCR ni indexación.

## Verificación

- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R contracts`: PASS (1/1, 0.03 s).
- `make -C Backend test`: PASS (4/4, 0.06 s). Es el equivalente canónico documentado porque el repositorio no tiene `Makefile` en la raíz; el objetivo `test` está en `Backend/Makefile`.
- No se afirma cobertura de plataforma ni finalización de la Fase 1.

## Seguimiento

- [ ] Implementar semántica de rutas y política neutral.
- [ ] Implementar `InitialScanner` y completar la evidencia de Fase 1.

## Evidencia observada — Unidad 2

- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R path_policy`: PASS, 2/2 pruebas, 0.12 s.
- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R native_file_system_view`: PASS, 1/1 prueba, 0.07 s (Windows).
- `make -C Backend test`: PASS; configuración, build Debug y CTest completados, 7/7 pruebas, 0.40 s.
- La disponibilidad nativa queda publicada como `SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE=1` en Windows y `0` fuera de Windows, sin adaptador POSIX sustituto.