# ADR-0001: frontera de observación pre-durable

- **Estado:** aceptada
- **Fecha:** 2026-08-25

## Contexto

El escaneo descubre estado del sistema de archivos, pero no asigna `observationId`, `eventId` ni `generation`.

## Decisión

`InitialScanner` entregará `FileObservation` a `IFileObservationSink`. `IFileChangeSink` recibirá únicamente `FileChange` post-outbox.

## Consecuencias

- El scanner no escribirá SQLite ni fabricará cambios durables.
- Un resultado no aceptado del sink pre-durable detendrá el scanner futuro sin reintentos implícitos.
- La asignación de IDs y la publicación durable permanecen fuera de esta porción.

## Alternativa descartada

Entregar `FileChange` directamente desde el scanner mezclaría descubrimiento con persistencia e inventaría identidad durable antes de la transacción correspondiente.