# Incidente y recuperación: bloqueo de orquestación de la Unidad 2

**Fecha:** 2026-08-27
**Cambio:** `add-monitoring-contracts-and-initial-scanner`
**Unidad:** Fase 1, Unidad de trabajo 2 (tareas 2.1–2.3)
**Rama:** `feature/monitoring-phase1-path-policy`

## Resumen ejecutivo

La Unidad 2 no tuvo tres intentos de implementación fallidos. Hubo eventos de orquestación distintos que deben separarse de las ejecuciones de implementación:

1. El primer ejecutor delegado terminó interrumpido antes de devolver su contrato de resultado. Su intento nativo se asentó como `interrupted`; el ledger confirmó que quedaba capacidad para continuar.
2. Una adquisición posterior fue bloqueada porque el orquestador cambió la identidad del objetivo (`work-unit` y `evidence-goal`). El runtime rechazó correctamente ese drift y sugirió reencuadrar el trabajo. No fue una ejecución de implementación ni consumió presupuesto de implementación.
3. La adquisición se reintentó con la identidad original exacta y fue autorizada. El ejecutor de recuperación completó las tareas 2.1–2.3 con evidencia verificable.

La recuperación está resuelta para esta unidad: el código permanece sin commit ni push en la rama indicada, y la Unidad 3 sigue explícitamente excluida.

## Estado verificado actual

- **Implementación:** tareas 2.1, 2.2 y 2.3 completadas.
- **Rama:** `feature/monitoring-phase1-path-policy`.
- **Entrega:** no se creó commit ni se hizo push.
- **Presupuesto:** 191 líneas authored, dentro del límite de 400 líneas.
- **Pruebas enfocadas:** política de rutas, 2/2; vista nativa de filesystem en Windows, 1/1.
- **Prueba canónica:** `make -C Backend test`, 7/7.
- **Alcance preservado:** la Unidad 3 (InitialScanner), watcher, almacenamiento/outbox, IDs durables y adaptadores POSIX no fueron implementados.
- **Documentación existente:** [resumen de implementación de la costura Windows y política de rutas](2026-08-27__implementation__feature__monitoring-path-policy-windows-seam.md).
- **Estado del bloqueo:** no hay un bloqueo de implementación activo. La evidencia de la unidad está completa; queda una validación independiente de contrato de fase si el orquestador la requiere.

## Cronología real (evento vs. resultado)

| Secuencia | Evento de orquestación | Resultado y significado |
|---|---|---|
| 1 | Se lanzó el primer ejecutor delegado para la Unidad 2. | Terminó interrumpido antes de entregar el contrato de resultado. No se podía afirmar qué artefactos había producido ni verificar la implementación desde ese resultado. |
| 2 | Se asentó una vez el intento nativo interrumpido. | El ledger quedó consistente y devolvió `proceed`; esto habilitó una nueva adquisición, no una segunda implementación automáticamente. |
| 3 | Se solicitó una adquisición con un `work-unit` y un `evidence-goal` diferentes de los originales. | El runtime respondió **BLOCKED** por drift de identidad y recomendó rescope. No ejecutó código, no cambió archivos y no consumió el presupuesto de implementación. |
| 4 | Se repitió la adquisición con `work-unit=phase1-unit2-path-policy` y el objetivo de evidencia original exacto. | La adquisición fue autorizada y comenzó la recuperación válida. |
| 5 | El ejecutor de recuperación trabajó sobre la rama de la Unidad 2. | Completó 2.1–2.3, ejecutó las pruebas enfocadas y la prueba canónica, y asentó el intento como `complete`. |
| 6 | Se intentó una validación posterior del contrato de fase. | Fue interrumpida por una indicación del usuario. La comprobación independiente quedó incompleta en ese turno, pero la implementación y su evidencia no se invalidan. |

“Tres intentos fallidos” mezcla un ejecutor interrumpido, una adquisición rechazada antes de ejecutar y una recuperación exitosa. No describe tres implementaciones fallidas.

## Causa raíz

### 1. Ejecutor interrumpido

El primer ejecutor no completó el handoff esperado. Al no existir contrato de resultado, la orquestación no podía tratar sus afirmaciones como evidencia ni lanzar fases dependientes. La causa operativa fue la interrupción del actor, no un fallo demostrado de la implementación.

### 2. Drift de identidad del objetivo

El runtime enlaza cada intento a una identidad estable: `work-unit` y `evidence-goal`. Cambiar cualquiera durante una re-adquisición altera el objetivo autorizado. El bloqueo protegió contra mezclar presupuestos o evidencia de trabajos distintos; no fue un fallo del runtime ni una ejecución fallida.

### 3. Timeout de espera no equivale a estado del agente

Un timeout sólo indica que no llegó una actualización dentro de la ventana. No demuestra que el agente haya fallado, terminado o producido un resultado. El estado terminal debe obtenerse consultando el estado del agente y el ledger nativo. Aquí, la interrupción se confirmó antes de asentarla como `interrupted`.

## Cómo se superó

1. Se conservó el estado parcial sin asumir que los archivos interrumpidos constituían evidencia histórica de TDD.
2. Se asentó exactamente una vez el intento nativo interrumpido.
3. Se descartó la adquisición con identidad modificada sin ejecutarla ni atribuirle cambios.
4. Se reacquirió con la identidad original exacta: `phase1-unit2-path-policy` y el objetivo de evidencia original.
5. El ejecutor autorizado completó la costura portable de rutas/filesystem, la política por componentes y los adaptadores privados WIN32.
6. Se ejecutaron las pruebas enfocadas y la prueba canónica completa de Backend.
7. Se asentó el intento como completo y se actualizó la evidencia sin incluir la Unidad 3.

## Procedimiento reproducible para futuras reintentos

1. Consultar primero el estado nativo del cambio y del intento; no inferir estado a partir del timeout o del transcript.
2. Si el actor está interrumpido, asentar **una sola vez** el token nativo como `interrupted`; conservar el identificador opaco sin publicarlo.
3. Volver a adquirir usando exactamente el mismo `work-unit` y `evidence-goal` autorizados originalmente.
4. Si se necesita cambiar el objetivo, solicitar una decisión explícita de maintainer para hacer rescope. No cambiar la identidad para “desbloquear” la adquisición.
5. Preservar e inspeccionar el trabajo parcial antes de editarlo; no fabricar RED/GREEN histórico no verificable.
6. Ejecutar la prueba enfocada y la evidencia nativa específica de plataforma cuando corresponda.
7. Ejecutar también la prueba canónica completa definida por el repositorio.
8. Asentar el resultado nativo sólo con evidencia acotada y verificable; registrar la Unidad 3 como excluida si no participó.
9. Si una validación posterior se interrumpe, reportarla como pendiente, no como fallo de implementación.

## Prevención / checklist

- [ ] Separar evento de orquestación, ejecución de implementación y resultado verificado.
- [ ] Tratar un timeout como “sin actualización”; consultar el estado real del agente.
- [ ] No mutar la identidad de un intento; `work-unit` y `evidence-goal` deben coincidir exactamente.
- [ ] Asentar un intento interrumpido una sola vez antes de reacquirir.
- [ ] No atribuir archivos ni pruebas a un ejecutor sin contrato.
- [ ] No convertir una adquisición bloqueada en una “implementación fallida”.
- [ ] Inspeccionar el estado parcial y conservar los límites de alcance.
- [ ] Ejecutar evidencia enfocada, nativa (si aplica) y suite canónica.
- [ ] Abrir una adquisición nueva y separada para la Unidad 3.
- [ ] Redactar tokens e identificadores opacos; conservarlos sólo en el ledger.
- [ ] Registrar aparte una validación pendiente si una intervención del usuario la interrumpe.

## Tabla de evidencia

| Evidencia | Resultado verificado | Alcance |
|---|---|---|
| Adquisición de recuperación | Autorizada al restaurar la identidad exacta de la Unidad 2. | Orquestación; no prueba de código. |
| Asentamiento del primer ejecutor | `interrupted`, asentado una vez; el ledger devolvió `proceed`. | No prueba una implementación fallida. |
| Adquisición con identidad cambiada | **BLOCKED** por drift de `work-unit`/`evidence-goal`. | No ejecutó implementación ni consumió presupuesto. |
| Política de rutas | CTest enfocado: 2/2. | Tarea 2.1 y política portable de 2.2. |
| Vista nativa de filesystem | CTest WIN32 enfocado: 1/1. | Tarea 2.3; adaptadores Windows. |
| Suite canónica | `make -C Backend test`: 7/7. | Configuración, build Debug y CTest del Backend. |
| Cambio authored | 191 líneas, 0 eliminaciones según el registro. | Dentro del límite de 400; sin commit/push. |
| Alcance | Unidad 3 y sus dependencias permanecen fuera. | No se afirma completitud de Fase 1. |

## Riesgo restante y próximo paso

La implementación de la Unidad 2 y sus pruebas están verificadas, pero la validación independiente del contrato de fase fue interrumpida por steering del usuario en un turno posterior. Eso no borra la evidencia de implementación ni convierte la recuperación en fallida; sólo significa que esa validación no quedó completada en ese turno.

Si se necesita cerrar ese control, ejecutar una **fresh phase-contract validation** sobre el estado actual, sin modificar el código ni ampliar el alcance. Después, implementar la Unidad 3 únicamente mediante una adquisición separada, con su propia identidad, presupuesto y evidencia.
