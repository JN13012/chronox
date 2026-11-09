# Temporal engine V2

UTC is a civil observation, never a duration. The native source supplies UTC,
QPC and unbiased awake counters in microseconds. TemporalEngine consumes only
injected samples, measures awake/unlocked time, and splits spans into epochs on
UTC discontinuities. Each measured microsecond belongs to one span of one run.
Civil overlaps across epochs/runs are permitted; monotonic overlaps are rejected.

Corrections take effect when observed. The previous span ends at its monotonic
prediction; the next begins at the newly observed civil UTC. The 50 ms detection
threshold never delays civil now. Missing power evidence excludes the ambiguous
interval and latches uncertainty until explicit state reconciliation; it is not
recovered as estimated activity. No historical civil timestamp is repaired.

V2 storage is additive. V1 rows remain unchanged, including interrupted rows;
their durable upper bound is COALESCE(ended_at_us, checkpoint_at_us). V2 stores
measured duration independently. Recovery seals only V2 active spans at their
durable checkpoint. Current civil UTC never inherits the historical maximum.
Calendar reports count recorded completed contributions even when their civil
dates appear future after a clock rollback. No extrapolation of inactive data.

Only temporary fixtures/copies may be migrated or recovered during validation.
Installation and real database migration require separate explicit permission.

## Opt-in observability

`CHRONOX_TIME_DIAGNOSTICS=1` enables the future process's local
`time-diagnostics/time-status.json` and `time-events[.1].jsonl` beside its data.
Default: no diagnostic I/O. Two event files capped at 256 KiB each, 128 in-memory
frames, at most 16 logged events/second and one status publication/second when
samples arrive. No extra periodic timer or worker thread. Direct internal and
Windows UTC values, QPC/awake counters, epoch, correction/uncertainty counts,
counting state and dropped events are exposed. I/O failure disables the sink
without interrupting measurement. No SQL or application commands are accepted.
Uncertainty counts are also durable run metadata, even with logging disabled;
widget/dashboard indicate `Mesure incertaine`. Only an explicit power resume
or user unlock can reconcile an uncertain awake state; a timer/WTS query alone
cannot establish the missing power evidence. Hardware remains NOT TESTED.

## Read precision and accounting boundaries

Samples include the QPC bracket width as measurement uncertainty. A difference
between elapsed QPC and awake time exceeding max(2 ms, the sum of both bracket
uncertainties) latches doubt. A read taking more than 100 ms is also
uncertain. These bounds are detection tolerances, not a claim of exact hardware
sleep reconstruction. Explicit suspend events exclude sleep without that
threshold. Ordinary civil geometry uses unit-rate measured spans; an accumulated
UTC residual above 50 ms starts a new epoch at the observation boundary. Civil
`now` always remains the directly sampled UTC. Past dates are not retroactively
corrected. Per-day totals may legitimately change day when the civil clock moves
back across midnight; the total measured duration never becomes negative.

Measured mode freezes the V1 durable interpretation, including interrupted rows.
ReadOnly opens SQLite with READONLY + query_only and never migrates/recovers.
Legacy mode remains for V1 fixtures/compatibility; writes through that API are
rejected on V2. New application code explicitly selects Measured. Checkpoints
upsert the active span rather than append one row per frame/minute. Epoch IDs
are stored in spans; run IDs distinguish counters across restarts. Failed writes
retain pending spans and can be retried idempotently. Status files carry their
capture counters: consumers must show their age, not treat old frames as live.
