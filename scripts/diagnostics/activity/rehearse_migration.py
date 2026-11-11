"""V2 archive -> isolated V3 copy. Source is opened READONLY; never restored."""
import argparse
import contextlib
import hashlib
import json
import sqlite3
import subprocess
from pathlib import Path


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(",", ":")).encode()).hexdigest()


def inventory(path):
    with contextlib.closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as db:
        db.execute("PRAGMA query_only=ON")
        assert db.execute("PRAGMA quick_check").fetchall() == [("ok",)]
        assert db.execute("PRAGMA foreign_key_check").fetchall() == []
        return {
            "version": db.execute("PRAGMA user_version").fetchone()[0],
            **{table: db.execute(f"SELECT * FROM {table} ORDER BY rowid").fetchall()
               for table in ("session_intervals", "temporal_runs", "measured_spans")},
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    output = root / "build" / "activity-audit"
    output.mkdir(parents=True, exist_ok=True)
    source = args.source.resolve(strict=True)
    copy = output / "copy-v3.db"
    if copy.exists():
        raise RuntimeError("Refusing to overwrite the rehearsal copy")
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    before = inventory(source)
    if before["version"] != 2:
        raise RuntimeError("Expected an archived V2 source")
    with contextlib.closing(sqlite3.connect(source.as_uri() + "?mode=ro", uri=True)) as src:
        src.execute("PRAGMA query_only=ON")
        with contextlib.closing(sqlite3.connect(copy)) as dst:
            src.backup(dst)
    probe = root / "build" / "src" / "Release" / "chronox_activity_migration_probe.exe"
    baseline = json.loads(subprocess.check_output([str(probe), str(copy), "--read-only"], text=True))
    migrated = json.loads(subprocess.check_output([str(probe), str(copy)], text=True))
    after = inventory(copy)
    assert after["version"] == 3
    assert before["session_intervals"] == after["session_intervals"]
    assert before["temporal_runs"] == after["temporal_runs"]
    assert len(before["measured_spans"]) == len(after["measured_spans"])
    for old, new in zip(before["measured_spans"], after["measured_spans"]):
        assert old[:-1] == new[:-1] and new[-1] == 0
    assert baseline == migrated
    assert source_hash == hashlib.sha256(source.read_bytes()).hexdigest()
    assert before == inventory(source)
    with contextlib.closing(sqlite3.connect(copy)) as db:
        assert db.execute("SELECT count(*) FROM activity_spans").fetchone()[0] == 0
        assert db.execute("SELECT count(*) FROM activity_tracking_runs").fetchone()[0] == 0
        assert db.execute("SELECT count(*) FROM activity_coverage").fetchone()[0] == 0
    result = {
        "verdict": "PASS", "source_sha256": source_hash,
        "v1_rows": len(before["session_intervals"]),
        "v1_sha256": digest(before["session_intervals"]),
        "v2_spans": len(before["measured_spans"]),
        "v2_immutable_sha256": digest([row[:-1] for row in before["measured_spans"]]),
        "sealed_open_spans": sum(row[-1] for row in before["measured_spans"]),
        "reports_compared": 4, **migrated,
        "quick_check": "ok", "foreign_key_check": [], "source_unchanged": True,
    }
    (output / "migration.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
