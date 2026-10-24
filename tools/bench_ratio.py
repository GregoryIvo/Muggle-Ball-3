#!/usr/bin/env python3
"""Compares a host and a device run of the physics bench (docs/VITA_PLAN.md M3b, app/bench, platform/vita/bench)
and reports how much slower the device is.

    python3 tools/bench_ratio.py host.csv device.csv

Both CSVs come from the same bench-physics.json (mb_physics_bench on the Mac, the mb3_vita_bench VPK on the
console), so this joins their rows by (level, profile, velocitySteps, positionSteps, collisionSteps, threads)
-- the settings actually simulated, not just the level name -- and for every matched level prints the host and
device mean and p95 step times, the two ratios (device over host) and the device's dropped steps. It then
prints the median and maximum ratio over every matched level: the plan's "measured scale factor", which
replaces its 45-110x (70x central) estimate (docs/VITA_PLAN.md section 4.4) once a device CSV exists. A level
whose device p95 step time is over the plan's per-step physics budget, or whose mean is over half the 16 ms a
frame can spend on two steps (docs/VITA_PLAN.md section 1), is flagged: those are the levels pass 2 should
retune.

Standard library only; tests/test_bench_ratio.py checks it against two small synthetic CSVs.
"""

import argparse
import csv
import statistics
import sys

# docs/VITA_PLAN.md section 1: at most 16 ms of physics at p95 in a 33.3 ms frame, as two fixed steps of at
# most this each.
FRAME_PHYSICS_BUDGET_MS = 16.0
STEP_BUDGET_MS = FRAME_PHYSICS_BUDGET_MS / 2

# The settings a level was actually simulated with (not just its name): two rows join only when every one of
# these agrees, so a host and a device run of different solver settings for the "same" level are never
# compared as if they were the same measurement.
JOIN_KEYS = ("level", "profile", "velocitySteps", "positionSteps", "collisionSteps", "threads")


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def join_key(row):
    """(level, profile, velocitySteps, positionSteps, collisionSteps, threads), case-folded for the two text
    fields so "Village-Dvorik" and "village-dvorik" still match, as the game's own level lookups do."""
    return tuple(row[key].strip().lower() if key in ("level", "profile") else row[key].strip() for key in JOIN_KEYS)


def matched_pairs(host_rows, device_rows):
    """(host_row, device_row) for every join key present in both, host rows first in the host file's order; a
    key repeated within one file keeps only its first row, and a key in only one file is left out."""
    device_by_key = {}
    for row in device_rows:
        device_by_key.setdefault(join_key(row), row)
    pairs = []
    seen = set()
    for host_row in host_rows:
        key = join_key(host_row)
        if key in seen:
            continue
        seen.add(key)
        device_row = device_by_key.get(key)
        if device_row is not None:
            pairs.append((host_row, device_row))
    return pairs


def ratio(device_value, host_value):
    return device_value / host_value if host_value > 0 else float("nan")


def over_budget(device_row):
    return float(device_row["stepP95Ms"]) > STEP_BUDGET_MS or float(device_row["stepMeanMs"]) > STEP_BUDGET_MS


def report(host_rows, device_rows, out=sys.stdout):
    """Writes the table and the summary to `out`; returns (matched row count, levels flagged over budget)."""
    pairs = matched_pairs(host_rows, device_rows)
    if not pairs:
        return 0, []

    mean_ratios, p95_ratios, flagged = [], [], []
    columns = ("level", "host mean", "host p95", "dev mean", "dev p95", "mean x", "p95 x", "dropped")
    widths = (24, 10, 10, 10, 10, 8, 8, 8)
    out.write("".join(f"{c:>{w}}" if i else f"{c:<{w}}" for i, (c, w) in enumerate(zip(columns, widths))) + "\n")
    out.write("-" * sum(widths) + "\n")

    for host_row, device_row in pairs:
        host_mean, host_p95 = float(host_row["stepMeanMs"]), float(host_row["stepP95Ms"])
        device_mean, device_p95 = float(device_row["stepMeanMs"]), float(device_row["stepP95Ms"])
        mean_ratio, p95_ratio = ratio(device_mean, host_mean), ratio(device_p95, host_p95)
        mean_ratios.append(mean_ratio)
        p95_ratios.append(p95_ratio)
        flag = over_budget(device_row)
        if flag:
            flagged.append(host_row["level"])
        dropped = device_row["droppedSteps"]
        out.write(
            f"{host_row['level']:<24}{host_mean:>10.3f}{host_p95:>10.3f}{device_mean:>10.3f}{device_p95:>10.3f}"
            f"{mean_ratio:>8.1f}{p95_ratio:>8.1f}{dropped:>8}"
            f"{'  OVER BUDGET' if flag else ''}\n"
        )

    out.write("\n")
    out.write(f"{len(pairs)} levels matched\n")
    out.write(f"mean-time ratio: median {statistics.median(mean_ratios):.1f}x, max {max(mean_ratios):.1f}x\n")
    out.write(f"p95-time ratio: median {statistics.median(p95_ratios):.1f}x, max {max(p95_ratios):.1f}x\n")
    out.write(
        f"measured scale factor (p95, replaces the plan's 70x central / 45-110x estimate): "
        f"median {statistics.median(p95_ratios):.1f}x, max {max(p95_ratios):.1f}x\n"
    )
    if flagged:
        out.write(
            f"{len(flagged)} level(s) over the {STEP_BUDGET_MS:.0f} ms per-step budget on the device: "
            f"{', '.join(flagged)}\n"
        )
    else:
        out.write(f"no level is over the {STEP_BUDGET_MS:.0f} ms per-step budget on the device\n")
    return len(pairs), flagged


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("host_csv", help="CSV from mb_physics_bench on the host")
    parser.add_argument("device_csv", help="CSV copied back from the Vita (bench-physics.csv)")
    args = parser.parse_args(argv)

    host_rows = read_rows(args.host_csv)
    device_rows = read_rows(args.device_csv)
    matched, _flagged = report(host_rows, device_rows)
    if matched == 0:
        print("no (level, profile, settings) combination is in both CSVs", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
