# [MON-003] Feature: Windows watcher safe startup

- **Status:** completed
- **Date:** 2026-09-02
- **Responsible profile:** architecture
- **Participants:** architecture, Windows, QA
- **Chain:** feature-branch-chain, PR #3 documentation and final-verification slice; base `ccfb3e0`

## Outcome

Windows monitoring now documents a watcher-first, process-local startup cutover: capture is armed before scanning, loss is sticky, and a root is healthy only after reconciliation (when needed) and a later drained barrier establish coverage.

## What to review

1. Read [ADR-0002](../adr/0002-windows-watcher-safe-startup.md) for the decision and its explicit limits.
2. Confirm the portable coordinator and private `WIN32` adapter preserve ordered ingress, sticky dirty state, cancellation/join, and conservative rename degradation.
3. Confirm the final build and CTest evidence below.

## Guarantees and limits

| Topic | Result |
|---|---|
| Bounded ingress | Producers do not wait; overflow, saturation, malformed input, or ordering gaps produce sticky dirty/loss. |
| Safe cutover | Every mutation after arming and before health has ordered notification coverage or a reconciliation obligation whose scan interval and later barrier cover it. |
| Lifecycle | Cancellation and degraded exits expose a pending obligation; `stopAndJoin()` joins workers before callbacks can continue. |
| Rename decoding | Bounds, UTF-16, and root containment are validated; only contiguous old/new records form a rename. Ambiguous, gapped, or cancelled pairs become separate evidence. |
| Pre-durable boundary | No atomic snapshot, durable delivery, crash-persistent dirty state, durable IDs, catalog/outbox, publication, or runtime wiring. A restart after interruption repeats watcher-first startup and does not trust prior in-memory coverage. |

## Files and contracts

- `Backend/include/semantic_fs/monitoring/`: portable watcher and startup contracts.
- `Backend/src/monitoring/safe_startup_coordinator.cpp`: serialized startup, dirty reconciliation, barrier health gate, and non-healthy obligations.
- `Backend/src/monitoring/windows_file_watcher.cpp`: private `ReadDirectoryChangesW` capture, validated decoding, cancellation, and joined shutdown.
- No public durable contract, migration, or persistence schema changed.

## Verification

```text
cmd /d /c "set \"PATH=%Path%\" && cmake --build Backend/build --config Debug && ctest --test-dir Backend/build -C Debug --output-on-failure --no-tests=error"
```

- Result: PASS (exit 0). Debug build completed and CTest passed 31/31 tests (2.48 s).
- Runtime harness: N/A for this documentation/final-verification boundary. Existing Win32 adapter coverage uses temporary-directory `ReadDirectoryChangesW` scenarios and joined shutdown; this slice adds no runtime behavior.

## Rollback and follow-up

- **Documentation-slice rollback:** revert this file, `ADR-0002`, and the `MON-003` index row only.
- **Feature rollback:** additionally revert the portable coordinator and Win32 watcher slices (`df31407`, `f2c6cad`, `62594c0`, `ccfb3e0`) in dependency order. No durable data or runtime wiring must be rolled back.
- **Out of scope:** non-Windows watchers, atomic snapshot semantics, crash recovery, and durable publication remain future work.
