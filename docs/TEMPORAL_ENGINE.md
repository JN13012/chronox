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
