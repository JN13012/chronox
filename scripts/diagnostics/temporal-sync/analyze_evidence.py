"""Summarize saved observations only; never opens a user database."""
import csv
import datetime as dt
import json
import pathlib

root = pathlib.Path(__file__).resolve().parents[3]
evidence = root / "build/temporal-sync-evidence"
old = root / "build/audit-p133"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def stamp(value):
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))


samples = [json.loads(line) for line in (evidence / "live-samples.jsonl").read_text().splitlines()]
changes = [row for row in samples if row["checkpoint_changed"]]
first, last = samples[0], samples[-1]
anchor = changes[0]
fixed_offset = [
    (anchor["latest_rows"][0][2] + (s["monotonic_elapsed_s"] - anchor["monotonic_elapsed_s"]) * 1e6 - s["windows_us"]) / 1e6
    for s in samples if s["monotonic_elapsed_s"] >= anchor["monotonic_elapsed_s"]
]
summary = {
    "live": {
        "count": len(samples), "first": first, "last": last,
        "changes": changes,
        "elapsed_monotone_s": last["monotonic_elapsed_s"] - first["monotonic_elapsed_s"],
        "elapsed_windows_s": (last["windows_us"] - first["windows_us"]) / 1e6,
        "fixed_anchor_offset_range_s": [min(fixed_offset), max(fixed_offset)],
        "max_poll_span_s": max(s["transition_poll_span_s"] or 0 for s in samples),
        "max_query_duration_s": max(s["query_duration_s"] for s in samples),
    },
    "historic_snapshots": [], "historic_monotone_series": [], "clock_jumps": [],
}
for path in sorted(old.rglob("sqlite*.json")):
    data = read(path)
    for interval in data.get("open_intervals", []):
        summary["historic_snapshots"].append({"file": str(path.relative_to(root)),
            "windows_utc": dt.datetime.fromtimestamp(data["now_us"] / 1e6, dt.timezone.utc).isoformat(),
            "checkpoint_us": interval[2], "checkpoint_minus_windows_s": (interval[2] - data["now_us"]) / 1e6})
for path in old.rglob("samples.csv"):
    rows = list(csv.DictReader(path.open(encoding="utf-8-sig")))
    number = lambda value: float(value.replace(",", "."))
    a, b = rows[0], rows[-1]
    wall = (stamp(b["Timestamp"]) - stamp(a["Timestamp"])).total_seconds()
    monotone = number(b["ElapsedSeconds"]) - number(a["ElapsedSeconds"])
    summary["historic_monotone_series"].append({"file": str(path.relative_to(root)), "count": len(rows),
        "first": a["Timestamp"], "last": b["Timestamp"], "windows_s": wall, "monotone_s": monotone,
        "monotone_minus_windows_s": monotone - wall})
for event in read(evidence / "windows-clock-events.json"):
    data = event["Data"]
    summary["clock_jumps"].append({"recorded_utc": event["RecordedUtc"], "reason": data["Reason"],
        "old_utc": data["OldTime"], "new_utc": data["NewTime"],
        "delta_s": (stamp(data["NewTime"]) - stamp(data["OldTime"])).total_seconds()})
rows = read(evidence / "readonly-intervals.json")
closed = sorted((r for r in rows if r[3] is not None), key=lambda r: r[1])
summary["saved_sqlite_invariants"] = {
    "rows": len(rows), "open": sum(r[3] is None for r in rows),
    "invalid_order": [r[0] for r in rows if r[2] < r[1] or (r[3] is not None and r[3] < r[2])],
    "closed_overlaps": [[a[0], b[0]] for a, b in zip(closed, closed[1:]) if a[3] > b[1]],
    "open_overlaps_closed": [[a[0], b[0]] for a in closed for b in rows if b[3] is None and a[3] > b[1]],
}
(evidence / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps({"live_count": len(samples), "changes": len(changes),
    "fixed_anchor_offset_range_s": summary["live"]["fixed_anchor_offset_range_s"],
    "max_poll_span_s": summary["live"]["max_poll_span_s"],
    "saved_sqlite_invariants": summary["saved_sqlite_invariants"]}, indent=2))
