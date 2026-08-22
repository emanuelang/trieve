# Documentación de cambios del módulo de monitoreo

## Por qué existe esta carpeta

Este directorio es el punto de convergencia de los cambios realizados por todos los perfiles del módulo. Se usa **un archivo por cambio material** para conservar contexto y reducir conflictos de edición entre agentes.

`INDEX.md` funciona como índice común. No debe reemplazarse esta estructura por una única bitácora extensa: varios agentes editando el mismo archivo producirían conflictos y entradas difíciles de revisar.

## Cuándo crear un registro

Crear un documento cuando un cambio:

- añade o modifica comportamiento;
- cambia un contrato o esquema persistente;
- corrige un defecto relevante;
- incorpora una decisión de concurrencia, plataforma o seguridad;
- agrega o cambia pruebas de contrato;
- altera configuración, build, operación o compatibilidad.

No hace falta crear uno para correcciones ortográficas aisladas sin efecto técnico.

## Nombre del archivo

```text
YYYY-MM-DD__perfil__tipo__slug.md
```

Ejemplos:

```text
2026-08-22__windows__feature__read-directory-changes.md
2026-08-23__reliability__fix__recover-expired-leases.md
2026-08-24__architecture__decision__file-change-generation.md
```

Valores recomendados de `perfil`:

- `architecture`
- `scanner`
- `windows`
- `reliability`
- `integration`
- `qa`

Valores permitidos de `tipo`:

- `feature`
- `fix`
- `refactor`
- `decision`
- `test`
- `ops`
- `docs`

El slug utiliza minúsculas, palabras separadas por guiones y describe una sola intención.

## Título obligatorio

```text
# [MON-<ID>] <Tipo>: <resultado observable>
```

Ejemplos:

```text
# [MON-014] Feature: detección asíncrona de cambios en Windows
# [MON-021] Fix: recuperación de leases vencidos después de un reinicio
# [MON-025] Decision: generación monotónica por archivo observado
```

`MON-<ID>` debe coincidir con el identificador de la tarea, issue o asignación. Si todavía no existe un tracker, usar un número secuencial reservado en `INDEX.md`; no usar títulos como “cambios varios”.

## Flujo obligatorio

1. Reservar el siguiente `MON-ID` en `INDEX.md` al comenzar.
2. Copiar `TEMPLATE.md` con el nombre establecido.
3. Completar contexto y alcance antes de implementar.
4. Actualizar decisiones, archivos y pruebas durante el trabajo.
5. Marcar el estado final y riesgos pendientes.
6. Enlazar el documento definitivo desde `INDEX.md`.

Si varios agentes colaboran en un mismo cambio, existe un único documento y se enumeran todos los perfiles participantes. Si son cambios independientes, cada agente crea su propio archivo.

## Reglas de calidad

- Describir el resultado y la razón; el diff ya describe la mecánica.
- No afirmar que una prueba pasó sin indicar el comando o evidencia.
- No ocultar fallos, limitaciones ni decisiones pendientes.
- Enlazar archivos del repositorio mediante rutas relativas.
- Indicar explícitamente si cambió un contrato público o una migración.
- Mantener secretos, contenido de usuario y rutas privadas fuera del registro.
- Para una decisión arquitectónica duradera, crear además un ADR y enlazarlo.

