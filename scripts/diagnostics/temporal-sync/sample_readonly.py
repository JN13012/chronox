"""Observe Chronox checkpoints without opening SessionDatabase or writing user data.

UTC projected from a newly observed checkpoint is an estimate, not process
telemetry. Its uncertainty includes polling and unmeasured writer latency.
"""
import argparse
import datetime as dt
import hashlib
import json
import pathlib
import sqlite3
import time


def utc(us):
    return dt.datetime.fromtimestamp(us / 1e6, dt.timezone.utc).isoformat()


def fingerprint(db):
    rows = db.execute("SELECT * FROM session_intervals WHERE ended_at_us IS NOT NULL ORDER BY id").fetchall()
    return {"closed_rows": len(rows), "closed_sha256": hashlib.sha256(json.dumps(rows).encode()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--seconds", type=float, default=150)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()
    if args.seconds <= 0 or args.interval <= 0:
        parser.error("seconds and interval must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    # mode=ro prevents writes; query_only is a second guard. Do not use immutable
    # on a live database: it bypasses locking/change detection.
    db = sqlite3.connect(args.database.resolve().as_uri() + "?mode=ro", uri=True, timeout=1)
    db.execute("PRAGMA query_only=ON")
    before = fingerprint(db)
    metadata = {"database": str(args.database.resolve()), "sqlite_reader": sqlite3.sqlite_version,
                "quick_check_before": db.execute("PRAGMA quick_check").fetchall(),
                "journal_mode": db.execute("PRAGMA journal_mode").fetchone()[0],
                "before": before}
    start = time.perf_counter_ns()
    last = None
    previous_mono = None
    generation = 0
    anchor = None
    with (args.output / "live-samples.jsonl").open("x", encoding="utf-8") as output:
        while True:
            m0 = time.perf_counter_ns()
            w0 = time.time_ns() // 1000
            rows = db.execute("SELECT id, started_at_us, checkpoint_at_us, ended_at_us FROM session_intervals ORDER BY id DESC LIMIT 2").fetchall()
            w1 = time.time_ns() // 1000
            m1 = time.perf_counter_ns()
            wall = (w0 + w1) // 2
            mono = (m0 + m1) // 2
            row = rows[0] if rows else None
            key = tuple(row) if row else None
            changed = last is not None and key != last
            if changed:
                generation += 1
                # Only calibrate on a fresh checkpoint of the same open
                # interval; do not extrapolate an old closed row after a stop.
                anchor = ((row[2], mono) if row is not None and last is not None
                          and row[0] == last[0] and row[1] == last[1]
                          and row[3] is None and last[3] is None
                          and row[2] > last[2] else None)
            projected = None if anchor is None else anchor[0] + (mono - anchor[1]) // 1000
            item = {"windows_utc": utc(wall), "windows_us": wall,
                    "monotonic_elapsed_s": (mono - start) / 1e9,
                    "query_duration_s": (m1 - m0) / 1e9,
                    "latest_rows": rows, "checkpoint_utc": None if row is None else utc(row[2]),
                    "checkpoint_minus_windows_s": None if row is None else (row[2] - wall) / 1e6,
                    "checkpoint_changed": changed, "generation": generation,
                    "transition_poll_span_s": None if previous_mono is None else (mono - previous_mono) / 1e9,
                    "chronox_projected_utc": None if projected is None else utc(projected),
                    "chronox_projected_minus_windows_s": None if projected is None else (projected - wall) / 1e6}
            output.write(json.dumps(item) + "\n")
            output.flush()
            last = key
            previous_mono = mono
            if (m1 - start) / 1e9 >= args.seconds:
                break
            time.sleep(args.interval)
    metadata["after"] = fingerprint(db)
    metadata["quick_check_after"] = db.execute("PRAGMA quick_check").fetchall()
    metadata["row_count"] = db.execute("SELECT COUNT(*) FROM session_intervals").fetchone()[0]
    metadata["open_rows"] = db.execute("SELECT * FROM session_intervals WHERE ended_at_us IS NULL").fetchall()
    db.close()
    (args.output / "live-metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
