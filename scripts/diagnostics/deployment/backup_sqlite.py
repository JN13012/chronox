"""Publish a new consistent SQLite snapshot, with a read-only source connection.

Uses sqlite3_backup via Connection.backup, not a filesystem copy of a live DB.
Never overwrites a destination. Incomplete snapshots remain visibly named on error.
"""
import argparse
import hashlib
import json
import os
import pathlib
import sqlite3
import time
import uuid


def backup(source, destination):
    source = pathlib.Path(source).resolve(strict=True)
    destination = pathlib.Path(destination).absolute()
    if os.name != "nt":
        raise RuntimeError("Windows-only publication semantics: rename must refuse overwrite")
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(destination)
    if not destination.parent.is_dir():
        raise ValueError("destination parent must already exist")
    temporary = destination.with_name(destination.name + ".incomplete-" + uuid.uuid4().hex)
    with temporary.open("xb"):
        pass
    start = time.monotonic()
    def progress(status, remaining, total):
        if time.monotonic() - start > 15:
            raise TimeoutError("backup exceeded 15 seconds; source stays read-only")
    live = target = None
    try:
        live = sqlite3.connect(source.as_uri() + "?mode=ro", uri=True, timeout=2)
        live.execute("PRAGMA query_only=ON")
        target = sqlite3.connect(temporary)
        live.backup(target, pages=64, progress=progress, sleep=0.01)
        if target.execute("PRAGMA quick_check").fetchall() != [("ok",)]:
            raise RuntimeError("backup integrity failed; incomplete snapshot retained")
        version = target.execute("PRAGMA user_version").fetchone()[0]
    finally:
        if target is not None:
            target.close()
        if live is not None:
            live.close()
    # Windows rename refuses an existing destination, including a raced creation.
    temporary.rename(destination)
    return {"path": str(destination), "source": str(source), "version": version,
            "quick_check": "ok", "elapsed_s": time.monotonic() - start,
            "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            "sqlite_version": sqlite3.sqlite_version}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--destination", required=True)
    args = parser.parse_args()
    print(json.dumps(backup(args.source, args.destination), indent=2))
