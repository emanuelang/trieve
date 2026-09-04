# ADR-0002: Windows watcher safe startup

- **Status:** accepted
- **Date:** 2026-09-02

## Decision

The Windows watcher is armed before the initial scan. `SafeStartupCoordinator` serializes watcher ingress and scan observations, reconciles sticky loss, and requires a later drained barrier before it reports a root healthy.

## Guarantees

| Area | Decision |
|---|---|
| Ingress | Accepted notifications have one process-local order. Bounded capture never waits for producers. |
| Loss | Overflow, saturation, malformed records, ordering gaps, cancellation, and delivery refusal set sticky per-root dirty state. |
| Cutover | A mutation after arming and before healthy is represented by an ordered notification or a pending/fulfilled reconciliation obligation covered by a scan interval and later barrier. |
| Reconciliation | Dirty clears only after an armed reconciliation scan is accepted, a later barrier drains, and the dirty epoch did not change. |
| Lifecycle | Cancellation stops admission; `stopAndJoin()` cancels I/O, joins I/O and dispatch workers, and returns only after callbacks are impossible. |
| Rename | Only contiguous, valid old/new names from one uninterrupted batch form a rename hint; all ambiguity degrades to separate events and dirty state. |

## Non-guarantees and restart limit

This is pre-durable, process-local coverage. It is **not** an atomic filesystem snapshot, a transactional cutover, or crash-durable loss tracking. A process termination before healthy discards in-memory ordering, dirty state, and pending obligations. Restart therefore trusts none of that memory: it arms capture first and repeats safe startup with a fresh coordinator and watcher. Durable IDs, catalogs, outboxes, publication, and runtime wiring remain outside this decision.

## Consequences

- Consumers must treat `Degraded` and `Cancelled` outcomes as non-healthy and retain the exposed pending-reconciliation obligation.
- The coordinator owns scanner ingress so a caller cannot attach a scanner to a different sink.
- The Win32 adapter remains private and `WIN32`-gated; portable contracts do not expose Win32 concepts.

## Rollback

Revert this ADR and the matching change record only to remove the documentation slice. Reverting the complete feature requires reverting the preceding portable and Win32 watcher slices as well; no durable state or runtime integration must be unwound.
