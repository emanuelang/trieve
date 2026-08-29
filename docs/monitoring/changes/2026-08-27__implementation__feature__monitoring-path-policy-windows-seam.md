# [MON-002] Implementación: política de rutas y costura de sistema de archivos

Esta unidad completa la base portable para decidir qué rutas pertenecen a una raíz y qué archivos se admiten, con adaptadores nativos limitados a Windows. No incorpora todavía el recorrido ni la entrega de `InitialScanner`.

## Resumen de implementación

La normalización se expresa a través de `IPathSemantics`: las rutas absolutas se normalizan léxicamente y la pertenencia se decide comparando componentes, no prefijos de texto. Por eso `root2` no pertenece a `root` y una ruta con `..` no puede escapar de la raíz. `PathPolicy` conserva una política neutral: admite archivos regulares —incluidos los ocultos— si no hay filtros configurados; excluye directorios y enlaces/reparse points; y aplica extensiones, exclusiones por componentes relativos y tamaño máximo sólo cuando fueron configurados.

`IFileSystemView` es la costura inyectable entre la política y el sistema de archivos. Modela listados, metadatos y errores previsibles sin exponer API nativa en los contratos públicos. En Windows, los adaptadores privados usan comparación ordinal Unicode sin distinción de mayúsculas y consultas Win32. No existe sustituto POSIX.

## Archivos y propósito

| Archivo | Acción | Propósito |
|---|---|---|
| `Backend/include/semantic_fs/monitoring/i_path_semantics.h` | Creado | Puerto portable para normalización, comparación de componentes y relativización. |
| `Backend/include/semantic_fs/monitoring/i_file_system_view.h` | Creado | Costura inyectable para listados, metadatos, entradas y errores de filesystem. |
| `Backend/include/semantic_fs/monitoring/path_policy.h` | Creado | Contrato de pertenencia de raíces, solapamiento y admisión neutral. |
| `Backend/src/scanner/path_policy.cpp` | Creado | Implementación por componentes de propiedad, exclusiones, extensiones y tamaño. |
| `Backend/src/scanner/windows_path_semantics.cpp` | Creado | Adaptador Win32 privado para rutas UTF-8 y comparación ordinal sin mayúsculas. |
| `Backend/src/scanner/windows_file_system_view.cpp` | Creado | Adaptador Win32 privado para listado y metadatos con errores tipados. |
| `Backend/tests/monitoring/path_policy_test.cpp` | Creado | Pruebas de límites de raíz, normalización, filtros neutrales y exclusiones por componente. |
| `Backend/tests/monitoring/native_file_system_view_test.cpp` | Creado | Prueba WIN32 de semántica ordinal, relativización, listado y macro de disponibilidad. |
| `Backend/CMakeLists.txt` | Modificado | Registra fuentes portables, adapta sólo en WIN32 y publica `SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE`. |
| `docs/monitoring/changes/2026-08-25__architecture__feature__monitoring-contracts-and-initial-scanner.md` | Modificado | Añade evidencia observada de esta unidad al registro MON-001. |
| `docs/monitoring/changes/2026-08-27__implementation__feature__monitoring-path-policy-windows-seam.md` | Creado | Documenta alcance, decisiones, verificación y límites de la Unidad 2. |

## Verificación observada

- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R path_policy`: PASS, 2/2 pruebas, 0.12 s.
- `ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error -R native_file_system_view`: PASS, 1/1 prueba, 0.07 s (Windows).
- `make -C Backend test`: PASS; configuración, build Debug y CTest completados, 7/7 pruebas, 0.40 s.

## Limitaciones y alcance excluido

- `InitialScanner`, traversal, cancelación, entrega a sinks y diagnósticos quedan para la Unidad 3.
- No se implementan watcher, SQLite/catálogo/outbox, IDs durables, reconciliación, OCR ni indexación.
- En plataformas no Windows se conservan contratos y pruebas portables, pero `SEMANTIC_FS_WINDOWS_SCANNER_AVAILABLE=0`; no se incorpora un adaptador POSIX implícito.
- La normalización es léxica y no promete una instantánea atómica del filesystem.