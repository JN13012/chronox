"""Isolated native benchmark; does not launch Chronox or access user data."""
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[4]
reference = root / "build/temporal-reference"
evidence = root / "build/temporal-v2-evidence"
evidence.mkdir(parents=True, exist_ok=True)
paths = ["src/core/" + name for name in ("calendar_period.h", "calendar_period.cpp", "session_tracker.h", "session_tracker.cpp", "utc_timestamp_clock.h", "utc_timestamp_clock.cpp")]
paths += ["src/storage/session_database.h", "src/storage/session_database.cpp"]
for path in paths:
    dest = reference / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(subprocess.check_output(["git", "show", "d6db29d:" + path], cwd=root))
build = root / "build/temporal-benchmark"
subprocess.run(["cmake", "-S", str(pathlib.Path(__file__).parent), "-B", str(build), "-A", "x64"], check=True)
subprocess.run(["cmake", "--build", str(build), "--config", "Release"], check=True)
results = []
for profile in ("hot", "history", "cadence"):
    for repeat in range(2):
        # Reverse order on second repetition to reduce ordering bias.
        flavors = [("baseline", "off"), ("measured", "off"), ("measured", "logging")]
        if repeat:
            flavors.reverse()
        for flavor, logging in flavors:
            output = subprocess.check_output([str(build / "Release" / ("temporal_bench_" + flavor + ".exe")), profile, "30", logging], text=True)
            result = json.loads(output)
            result["repeat"] = repeat
            results.append(result)
            (evidence / "benchmark.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            print(json.dumps(result), flush=True)
