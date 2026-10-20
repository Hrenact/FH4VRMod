"""Compare uniquely matched matrix data at producer/consumer and capture checkpoints.

This establishes CPU ordering only, not ownership of final GPU pixels.
Repeated fingerprints are excluded rather than assigned to the nearest frame.
"""
import argparse
import bisect
import collections
import csv
import json
import re
from pathlib import Path


def analyze(path):
    with path.open() as source:
        header = source.readline().strip()
        frequency = int(re.search(r"qpc_frequency=(\d+)", header)[1])
        base = int(re.search(r"base=([0-9a-fA-F]+)", header)[1], 16)
        dropped = int(re.search(r"dropped=(\d+)", header)[1])
        rows = [
            {key: int(value, 16 if key in ("caller", "object") else 10)
             for key, value in row.items()}
            for row in csv.DictReader(source)
        ]
    rows.sort(key=lambda row: (row["qpc"], row["index"]))
    snapshots = {}
    queued, consumed, captures = [], [], []
    for row in rows:
        if row["kind"] == 15 and row["caller"] == base + 0x30c762d:
            snapshots[row["thread"]] = row
        if row["kind"] == 20 and row["caller"] == base + 0x30c7661:
            row["snapshot"] = snapshots.get(row["thread"])
            queued.append(row)
        if row["kind"] == 21 and row["caller"] == base + 0x3940766:
            consumed.append(row)
        if row["kind"] == 19:
            captures.append(row)

    by_hash = collections.defaultdict(list)
    consumption_counts = collections.Counter(row["data0"] for row in consumed)
    for row in queued:
        by_hash[row["data0"]].append(row)
    matches, unknown = [], 0
    for row in consumed:
        row["source_command"] = None
        candidates = by_hash[row["data0"]]
        if len(candidates) != 1 or consumption_counts[row["data0"]] != 1:
            unknown += 1
            continue
        producer = candidates[0]
        snapshot = producer["snapshot"]
        if not snapshot or producer["qpc"] >= row["qpc"] or dropped:
            unknown += 1
            continue
        row["source_command"] = snapshot["command"]
        matches.append({
            "source_command": snapshot["command"],
            "observed_consumer_command": row["command"],
            "delay_ms": (row["qpc"] - producer["qpc"]) * 1000 / frequency,
            "producer_thread": producer["thread"],
            "consumer_thread": row["thread"],
        })

    # Keep unknown consumers in the timeline. Never silently reuse an older
    # known source when the newest consumer was ambiguous.
    consumer_times = [row["qpc"] for row in consumed]
    comparisons = []
    unknown_captures = 0
    for capture in captures:
        index = bisect.bisect_right(consumer_times, capture["qpc"]) - 1
        if index < 0 or consumed[index]["source_command"] is None:
            unknown_captures += 1
            continue
        consumer = consumed[index]
        if consumer["thread"] != capture["thread"]:
            unknown_captures += 1
            continue
        comparisons.append({
            "capture_qpc": capture["qpc"],
            "predicted_command": capture["command"],
            "consumed_source_command": consumer["source_command"],
            "latest_camera_cycle": capture["data0"],
            "prediction_minus_source": capture["command"] - consumer["source_command"],
        })
    delays = sorted(row["delay_ms"] for row in matches)
    latest_capture = {}
    shadow_rows = []
    for row in rows:
        if row["kind"] == 19:
            latest_capture[row["thread"]] = row
        elif row["kind"] == 22:
            capture = latest_capture.get(row["thread"])
            if capture:
                shadow_rows.append({
                    "capture_qpc": capture["qpc"],
                    "predicted_command": capture["command"],
                    "source_command": row["command"], "serial": row["data0"],
                    "status": row["data1"],
                })
    offline = {row["capture_qpc"]: row["consumed_source_command"]
               for row in comparisons}
    comparable = [row for row in shadow_rows
                  if row["status"] == 1 and row["capture_qpc"] in offline]
    return {
        "header": header,
        "caveat": "Unique raw-bit fingerprints and same-thread CPU ordering; not proof of final GPU pixel provenance.",
        "queued": len(queued), "consumed": len(consumed),
        "uniquely_matched": len(matches), "unknown_consumers": unknown,
        "captures": len(captures), "unknown_captures": unknown_captures,
        "comparison_count": len(comparisons),
        "prediction_minus_source": dict(collections.Counter(
            row["prediction_minus_source"] for row in comparisons)),
        "delay_ms": {
            "min": delays[0], "median": delays[len(delays) // 2],
            "p95": delays[min(len(delays) - 1, int(len(delays) * .95))],
            "max": delays[-1],
        } if delays else {},
        "matches": matches, "comparisons": comparisons,
        "shadow": {
            "events": len(shadow_rows),
            "status_counts": dict(collections.Counter(row["status"] for row in shadow_rows)),
            "offline_unique_comparisons": len(comparable),
            "offline_disagreements": sum(
                row["source_command"] != offline[row["capture_qpc"]] for row in comparable),
            "source_minus_prediction": dict(collections.Counter(
                row["source_command"] - row["predicted_command"]
                for row in shadow_rows if row["status"] == 1)),
            "rows": shadow_rows,
        },
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = analyze(args.trace)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    brief = {key: value for key, value in result.items()
             if key not in ("matches", "comparisons")}
    brief["shadow"] = {key: value for key, value in result["shadow"].items() if key != "rows"}
    print(json.dumps(brief, indent=2))
