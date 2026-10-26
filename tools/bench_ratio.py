#!/usr/bin/env python3
"""Compares a host and a device run of the physics bench (docs/VITA_PLAN.md M3b, app/bench, platform/vita/bench)
and reports how much slower the device is.

    python3 tools/bench_ratio.py host.csv device.csv

Both CSVs come from the same bench-physics.json (mb_physics_bench on the Mac, the mb3_vita_bench VPK on the
console), so this joins their rows by (level, profile, velocitySteps, positionSteps, collisionSteps, threads)
-- the settings actually simulated, not just the level name -- and for every matched level prints the host and
device mean and p95 step times, the two ratios (device over host), the steps each side actually ran and the
device's dropped steps. A row whose host and device steps differ, or whose endedBy differs, covers two
different windows of the level (the Autopilot's own run is noisy, and the Vita's scalar VFP code against the
Mac's NEON/x86 can end a level sooner or later) and is excluded from the ratios below, flagged in the table
instead; a row with zero steps on either side is excluded and flagged the same way, rather than folding a nan
into the median.

It then prints the median and maximum ratio over every *comparable* matched level: the mean-time ratio is the
plan's "measured scale factor", which replaces its 45-110x (70x central) estimate (docs/VITA_PLAN.md section
4.4, itself derived from mean step times) once a device CSV exists; the p95-time ratio is reported alongside it
as the device's own tail (cache misses, the newlib allocator) on top of that mean, not folded into the
headline. A level whose device p95 step time is over the plan's per-step physics budget, or whose mean is over
half the 16 ms a frame can spend on two steps (docs/VITA_PLAN.md section 1), is flagged too: those are the
levels pass 2 should retune.

Within one CSV, a join key that appears more than once (an interrupted run's partial row, followed by a full
re-run appended after it, since both files are append-only) keeps the row that best represents a complete run
-- endedBy other than "stopped" over "stopped", then more steps, then the later row in the file -- not whichever
came first, and a warning names every key this happened for.

Standard library only; tests/test_bench_ratio.py checks it against several small synthetic CSVs.
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
# compared as if they were the same measurement. Steps and endedBy are deliberately not join keys -- a
# mismatch there means the two rows cover a different window of the same settings, which report() flags and
# excludes rather than silently comparing.
JOIN_KEYS = ("level", "profile", "velocitySteps", "positionSteps", "collisionSteps", "threads")


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def join_key(row):
    """(level, profile, velocitySteps, positionSteps, collisionSteps, threads), case-folded for the two text
    fields so "Village-Dvorik" and "village-dvorik" still match, as the game's own level lookups do."""
    return tuple(row[key].strip().lower() if key in ("level", "profile") else row[key].strip() for key in JOIN_KEYS)


def _rank(row, index):
    """Higher sorts as the better row to keep for one join key, when it repeats within a file: endedBy other
    than "stopped" beats "stopped" (a partial, interrupted run), then more steps beats fewer, then the later
    row in the file beats an earlier one (a re-run appended after an interrupted attempt)."""
    return (row["endedBy"].strip() != "stopped", int(row["steps"]), index)


def _dedupe(rows):
    """One row per join key, keeping the best by `_rank`; (rows_by_key, repeated_keys) so a caller can warn
    when a key appeared more than once instead of silently picking one."""
    best = {}
    best_rank = {}
    repeated = set()
    for index, row in enumerate(rows):
        key = join_key(row)
        if key in best:
            repeated.add(key)
        rank = _rank(row, index)
        if key not in best or rank > best_rank[key]:
            best[key] = row
            best_rank[key] = rank
    return best, repeated


def matched_pairs(host_rows, device_rows):
    """(host_row, device_row) for every join key present in both, host rows first in the host file's order; a
    key repeated within one file keeps its best row by `_rank()`, and a key in only one file is left out."""
    host_by_key, _ = _dedupe(host_rows)
    device_by_key, _ = _dedupe(device_rows)
    pairs = []
    seen = set()
    for host_row in host_rows:
        key = join_key(host_row)
        if key in seen:
            continue
        seen.add(key)
        device_row = device_by_key.get(key)
        if device_row is not None:
            pairs.append((host_by_key[key], device_row))
    return pairs


def repeated_join_keys(rows):
    """Join keys that appear more than once in `rows` -- matched_pairs()/`_dedupe()` silently keep only the
    best one per `_rank`, so report() uses this to warn instead of letting that happen unnoticed."""
    _, repeated = _dedupe(rows)
    return repeated


def ratio(device_value, host_value):
    return device_value / host_value if host_value > 0 else float("nan")


def _format_ratio(value):
    """"n/a", not "nan": a host mean/p95 of zero (a zero-step row) makes `ratio()` undefined, and printing
    the literal string "nan" in a table column reads as a bug rather than the explained, flagged case it is."""
    return f"{value:.1f}" if value == value else "n/a"  # NaN is the only float that is not equal to itself


def over_budget(device_row):
    return float(device_row["stepP95Ms"]) > STEP_BUDGET_MS or float(device_row["stepMeanMs"]) > STEP_BUDGET_MS


def comparable_window(host_row, device_row):
    """False if the two rows cover a different window of the same settings: a different step count, a
    different endedBy, or either side at zero steps (seconds <= 0, or an immediate stop) -- comparing their
    times would be comparing two different simulations, not two measurements of the same one."""
    if host_row["steps"].strip() == "0" or device_row["steps"].strip() == "0":
        return False
    return host_row["steps"].strip() == device_row["steps"].strip() and \
        host_row["endedBy"].strip() == device_row["endedBy"].strip()


def _ratio_summary(label, ratios):
    if not ratios:
        return f"{label}: no comparable levels\n"
    return f"{label}: median {statistics.median(ratios):.1f}x, max {max(ratios):.1f}x\n"


def report(host_rows, device_rows, out=sys.stdout):
    """Writes the table and the summary to `out`; returns (matched row count, levels flagged over budget)."""
    pairs = matched_pairs(host_rows, device_rows)
    if not pairs:
        return 0, []

    mean_ratios, p95_ratios, flagged, excluded = [], [], [], []
    columns = ("level", "host mean", "host p95", "dev mean", "dev p95", "mean x", "p95 x", "h/d steps", "dropped")
    widths = (24, 10, 10, 10, 10, 8, 8, 12, 8)
    out.write("".join(f"{c:>{w}}" if i else f"{c:<{w}}" for i, (c, w) in enumerate(zip(columns, widths))) + "\n")
    out.write("-" * sum(widths) + "\n")

    for host_row, device_row in pairs:
        host_mean, host_p95 = float(host_row["stepMeanMs"]), float(host_row["stepP95Ms"])
        device_mean, device_p95 = float(device_row["stepMeanMs"]), float(device_row["stepP95Ms"])
        mean_ratio, p95_ratio = ratio(device_mean, host_mean), ratio(device_p95, host_p95)
        comparable = comparable_window(host_row, device_row)
        if comparable:
            mean_ratios.append(mean_ratio)
            p95_ratios.append(p95_ratio)
        else:
            excluded.append(host_row["level"])
        flag = over_budget(device_row)
        if flag:
            flagged.append(host_row["level"])
        dropped = device_row["droppedSteps"]
        steps_text = f"{host_row['steps']}/{device_row['steps']}"
        notes = []
        if not comparable:
            notes.append("DIFFERENT WINDOW, excluded")
        if flag:
            notes.append("OVER BUDGET")
        note_text = ("  " + ", ".join(notes)) if notes else ""
        out.write(
            f"{host_row['level']:<24}{host_mean:>10.3f}{host_p95:>10.3f}{device_mean:>10.3f}{device_p95:>10.3f}"
            f"{_format_ratio(mean_ratio):>8}{_format_ratio(p95_ratio):>8}{steps_text:>12}{dropped:>8}"
            f"{note_text}\n"
        )

    out.write("\n")
    out.write(f"{len(pairs)} levels matched\n")
    if excluded:
        out.write(
            f"{len(excluded)} level(s) excluded from the ratios below (host/device window mismatch or zero "
            f"steps): {', '.join(excluded)}\n"
        )
    for rows, name in ((host_rows, "host"), (device_rows, "device")):
        for key in sorted(repeated_join_keys(rows)):
            out.write(
                f"note: {key[0]!r} ({key[1]} profile) appears more than once in the {name} CSV; kept the "
                f"most complete run (endedBy not \"stopped\", then the most steps, then the latest one)\n"
            )

    out.write(_ratio_summary("mean-time ratio", mean_ratios))
    out.write(_ratio_summary("p95-time ratio", p95_ratios))
    if mean_ratios:
        out.write(
            f"measured scale factor (mean, replaces the plan's 70x central / 45-110x estimate -- itself a "
            f"mean-time factor, docs/VITA_PLAN.md section 4.4): median {statistics.median(mean_ratios):.1f}x, "
            f"max {max(mean_ratios):.1f}x\n"
        )
        out.write(
            f"device tail factor (p95, the device's own variance on top of that mean): median "
            f"{statistics.median(p95_ratios):.1f}x, max {max(p95_ratios):.1f}x\n"
        )
    else:
        out.write(
            "measured scale factor: no comparable host/device windows -- every matched level's steps or "
            "endedBy differed, or ran zero steps; see the table above\n"
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
