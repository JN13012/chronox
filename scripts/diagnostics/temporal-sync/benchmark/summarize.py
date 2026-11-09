"""Summarize the completed native benchmark; CPU is weighted by observed time."""
import json
import pathlib

root = pathlib.Path(__file__).resolve().parents[4]
evidence = root / "build/temporal-v2-evidence"
rows = json.loads((evidence / "benchmark.json").read_text())
if len(rows) != 18:
    raise SystemExit(f"Benchmark incomplete: {len(rows)}/18 records")
summary = {}
for profile in ("hot", "history", "cadence"):
    summary[profile] = {}
    for flavor in ("baseline-d6db29d", "measured", "measured-logging"):
        selected = [r for r in rows if r["profile"] == profile and r["flavor"] == flavor]
        assert len(selected) == 2
        summary[profile][flavor] = {
            "cpu_one_core_percent": 100 * sum(r["cpu_s"] for r in selected) / sum(r["wall_s"] for r in selected),
            "private_max_mib": max(r["private_max_bytes"] for r in selected) / 1048576,
            "median_us_range": [min(r["median_us"] for r in selected), max(r["median_us"] for r in selected)],
            "p95_us_max": max(r["p95_us"] for r in selected),
            "handles_stable": all(r["handles_before"] == r["handles_after"] for r in selected),
        }
old, new = (summary["cadence"][key] for key in ("baseline-d6db29d", "measured"))
summary["default_overhead"] = {
    "cpu_percentage_points": new["cpu_one_core_percent"] - old["cpu_one_core_percent"],
    "private_mib": new["private_max_mib"] - old["private_max_mib"],
    "within_proposed_budget": new["cpu_one_core_percent"] - old["cpu_one_core_percent"] <= 0.1
    and new["private_max_mib"] - old["private_max_mib"] <= 2,
}
(evidence / "benchmark-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
