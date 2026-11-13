"""Online SQLite backup (Python sqlite3_backup wrapper), then isolated V2 rehearsal.

No application launch, source writes, clock changes, registry writes or deletion.
Evidence contains private session timestamps; keep it out of version control.
"""
import argparse
import csv
from contextlib import closing
import datetime as dt
import hashlib
import json
import pathlib
import shutil
import sqlite3
import subprocess
import time
from backup_sqlite import backup


def connect_ro(path):
    db = sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True, timeout=2)
    db.execute("PRAGMA query_only=ON")
    return db


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(",", ":"), ensure_ascii=True).encode()).hexdigest()


def inventory(path):
    with closing(connect_ro(path)) as db:
        tables = {}
        for (name,) in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"):
            quoted = '"' + name.replace('"', '""') + '"'
            columns = db.execute(f"PRAGMA table_info({quoted})").fetchall()
            rows = db.execute(f"SELECT * FROM {quoted} ORDER BY rowid").fetchall()
            tables[name] = {"columns": columns, "rows": rows, "sha256": digest(rows)}
        return {"version": db.execute("PRAGMA user_version").fetchone()[0],
                "quick_check": db.execute("PRAGMA quick_check").fetchall(),
                "tables": tables}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=pathlib.Path, required=True)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--probe", type=pathlib.Path, required=True)
    args = p.parse_args()
    source = args.source.resolve(strict=True)
    root = args.output.resolve()
    repo = pathlib.Path(__file__).resolve().parents[3]
    # Only new, ignored workspace evidence directories; never user-data targets.
    if not root.is_relative_to(repo / "build") or root.exists() or source.is_relative_to(root):
        p.error("output must be a new directory beneath repository/build")
    root.mkdir(parents=True)
    (root / "ISOLATED_VALIDATION").write_text("Console storage validation only\n", encoding="utf-8")
    snapshot = root / "user-v1-snapshot.db"
    with closing(connect_ro(source)) as live:
        source_version = live.execute("PRAGMA user_version").fetchone()[0]
    snapshot_result = backup(source, snapshot)
    backup_elapsed = snapshot_result["elapsed_s"]
    before = inventory(snapshot)
    if before["version"] != 1 or before["quick_check"] != [("ok",)]:
        raise RuntimeError("expected healthy V1 source snapshot")
    snapshot_hash = hashlib.sha256(snapshot.read_bytes()).hexdigest()
    shutil.copyfile(snapshot, root / "migration-copy.db")
    now = time.time_ns() // 1000
    result = subprocess.run([str(args.probe.resolve(strict=True)), str(root), str(now)],
                            capture_output=True, text=True, timeout=90)
    (root / "probe.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError("probe failed; see retained evidence/probe.log")
    after = inventory(root / "migration-copy.db")
    failures = []
    def verify(ok, label):
        if not ok:
            failures.append(label)
    verify(after["version"] == 2, "schema V2")
    verify(all(after["tables"].get(k) == v for k, v in before["tables"].items()), "exact V1 rows/columns retained")
    verify(hashlib.sha256(snapshot.read_bytes()).hexdigest() == snapshot_hash, "immutable reference backup")
    legacy = before["tables"]["session_intervals"]["rows"]
    contributions = [(r[1], r[3] if r[3] is not None else r[2]) for r in legacy]
    phase_seconds = {"v1": 0, "migrated": 0, "live": 15, "recovered": 10,
                     "stopped": 17, "restarted": 17, "migrated-backup.db": 0,
                     "active-backup.db": 10, "recovered-backup.db": 10}
    api_mismatches = []
    def phase_spans(phase):
        spans = contributions.copy()
        seconds = phase_seconds[phase]
        if seconds:
            spans.append((now, now + min(seconds, 15 if phase == "live" else 10) * 1_000_000))
            if seconds == 17:
                spans.append((now, now + 7_000_000))
        return spans
    def intersect_sum(spans, a, b):
        return sum(max(0, min(e, b) - max(s, a)) for s, e in spans)
    def days_occupied(spans, a, b):
        days = set()
        for s, e in spans:
            s, e = max(s, a), min(e, b)
            if e <= s:
                continue
            date = dt.datetime.fromtimestamp(s / 1e6).date()
            last = dt.datetime.fromtimestamp((e - 1) / 1e6).date()
            while date <= last:
                days.add(date)
                date += dt.timedelta(days=1)
        return len(days)
    count = 0
    with (root / "reports.tsv").open(encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            count += 1
            a, b = int(row["start"]), int(row["end"])
            spans = phase_spans(row["phase"])
            expected = intersect_sum(spans, a, b)
            verify(int(row["duration_us"]) == expected, "queryHistory " + str(row))
            verify(int(row["days"]) == days_occupied(spans, a, b), "occupied days " + str(row))
            expected_active = int(row["phase"] == "live" and a < now+15_000_000 and b > now)
            verify(int(row["active"]) == expected_active, "active session identity " + str(row))
            if int(row["api_duration_us"]) != expected:
                api_mismatches.append({**row, "expected_us": expected})
    verify(not api_mismatches, "duration() disagrees with durable V1/V2 contributions")
    bucket_count = 0
    with (root / "buckets.tsv").open(encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            bucket_count += 1
            expected = intersect_sum(phase_spans(row["phase"]), int(row["start"]), int(row["end"])) // 1_000_000
            verify(int(row["seconds"]) == expected, "dashboard bucket " + str(row))
    backups = {}
    for name in ("migrated-backup.db", "active-backup.db", "recovered-backup.db"):
        inv = inventory(root / name)
        verify(all(inv["tables"].get(k) == v for k, v in before["tables"].items()), name + " preserves V1")
        verify(inv["quick_check"] == [("ok",)], name + " integrity")
        backups[name] = {"version": inv["version"], "quick_check": inv["quick_check"],
                         "sha256": hashlib.sha256((root / name).read_bytes()).hexdigest()}
    # Force DDL failure on a second real-data copy; transaction must leave it exact.
    failed_root = root / "injected-migration-failure"
    failed_root.mkdir()
    (failed_root / "ISOLATED_VALIDATION").write_text("Failure fixture\n", encoding="utf-8")
    failed_db = failed_root / "migration-copy.db"
    shutil.copyfile(snapshot, failed_db)
    with sqlite3.connect(failed_db) as db:
        db.execute("CREATE TABLE measured_spans(conflict)")
    failure_before = inventory(failed_db)
    failure_result = subprocess.run([str(args.probe.resolve()), str(failed_root), str(now)],
                                    capture_output=True, text=True, timeout=90)
    (failed_root / "probe.log").write_text(failure_result.stdout + failure_result.stderr, encoding="utf-8")
    failure_atomic = failure_result.returncode != 0 and inventory(failed_db) == failure_before
    verify(failure_atomic, "failed migration rolls back schema, version and rows")
    with closing(connect_ro(root / "migration-copy.db")) as db:
        negative = db.execute("SELECT COUNT(*) FROM measured_spans WHERE duration_us<0 OR duration_us<>mono_end-mono_start").fetchone()[0]
        overlaps = db.execute("SELECT COUNT(*) FROM measured_spans a JOIN measured_spans b ON a.run_id=b.run_id AND a.seq<b.seq AND a.mono_start<b.mono_end AND a.mono_end>b.mono_start").fetchone()[0]
        active = db.execute("SELECT COUNT(*) FROM measured_spans WHERE active=1").fetchone()[0]
        measured = db.execute("SELECT SUM(duration_us) FROM measured_spans").fetchone()[0]
    verify((negative, overlaps, active, measured) == (0, 0, 0, 17_000_000), "no double count, clean restart and durable recovery")
    verify(after["quick_check"] == [("ok",)], "final integrity")
    with closing(connect_ro(source)) as live:
        version_after = live.execute("PRAGMA user_version").fetchone()[0]
    verify(version_after == source_version == 1, "source remains V1")
    evidence = {"captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                "source": str(source), "sqlite_backup_library": sqlite3.sqlite_version,
                "backup_elapsed_s": backup_elapsed, "source_version_before": source_version,
                "source_version_after": version_after, "snapshot_sha256": snapshot_hash,
                "before": before, "after": after, "backups": backups,
                "report_comparisons": count, "duration_api_mismatch_count": len(api_mismatches),
                "dashboard_bucket_comparisons": bucket_count,
                "duration_api_mismatch_examples": api_mismatches[:12],
                "negative_spans": negative, "monotonic_overlaps": overlaps,
                "injected_migration_failure_atomic": failure_atomic,
                "measured_total_us": measured, "failures": failures,
                "verdict": "FAIL" if failures else "PASS"}
    (root / "validation.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in evidence.items() if k not in ("before", "after", "duration_api_mismatch_examples")}, indent=2))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
