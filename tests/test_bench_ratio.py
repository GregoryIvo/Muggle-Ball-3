"""tools/bench_ratio.py: joining a host and a device CSV, the ratios and the over-budget flag."""

import io
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import bench_ratio  # noqa: E402

HEADER = (
    "platform,label,level,profile,velocitySteps,positionSteps,collisionSteps,threads,simulatedSeconds,steps,"
    "endedBy,loadMs,buildMs,stepMeanMs,stepP95Ms,stepP99Ms,stepMaxMs,bodies,peakAwake,meanAwake,droppedSteps,"
    "tempAllocatorPeakBytes,score,wallSeconds"
)


def row(level, profile="vita", velocity=100, position=1, collision=1, threads=0, mean=1.0, p95=2.0, dropped=0,
       platform="host", label="pass1"):
    """One CSV line in csvLine()'s column order, with everything not under test at a fixed, harmless value."""
    return (
        f'"{platform}","{label}","{level}","{profile}",{velocity},{position},{collision},{threads},20.00,1200,'
        f'"time",1.000,1.000,{mean:.3f},{p95:.3f},{p95 + 1:.3f},{p95 + 2:.3f},100,10,5.00,{dropped},1000000,0,1.00'
    )


def write_csv(tmp_path, name, lines):
    path = tmp_path / name
    path.write_text(HEADER + "\n" + "\n".join(lines) + "\n", encoding="utf-8")
    return path


def run_report(host_path, device_path):
    """(matched, flagged, printed text): bench_ratio.report() against two CSV paths, with the text captured
    instead of going to stdout."""
    out = io.StringIO()
    matched, flagged = bench_ratio.report(bench_ratio.read_rows(host_path), bench_ratio.read_rows(device_path), out=out)
    return matched, flagged, out.getvalue()


class BenchRatio(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.tmp_path = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_matches_and_computes_ratios(self):
        host = write_csv(self.tmp_path, "host.csv", [
            row("knight-garden", mean=0.02, p95=0.09, platform="host"),
            row("village-love", mean=0.017, p95=0.07, platform="host"),
        ])
        device = write_csv(self.tmp_path, "device.csv", [
            row("knight-garden", mean=1.4, p95=6.3, dropped=0, platform="vita"),
            row("village-love", mean=1.19, p95=4.9, dropped=0, platform="vita"),
        ])
        pairs = bench_ratio.matched_pairs(bench_ratio.read_rows(host), bench_ratio.read_rows(device))
        self.assertEqual(len(pairs), 2)

        matched, flagged, text = run_report(host, device)
        self.assertEqual(matched, 2)
        self.assertEqual(flagged, [])
        self.assertIn("knight-garden", text)
        self.assertIn("village-love", text)
        self.assertIn("2 levels matched", text)
        # 1.4 / 0.02 = 70x: the mean ratio shows up in the table.
        self.assertIn("70.0", text)

    def test_flags_level_over_the_per_step_budget(self):
        host = write_csv(self.tmp_path, "host.csv", [row("knight-miss-1457", mean=0.07, p95=0.26, platform="host")])
        device = write_csv(self.tmp_path, "device.csv", [
            row("knight-miss-1457", mean=6.0, p95=18.0, dropped=3, platform="vita"),
        ])
        matched, flagged, _text = run_report(host, device)
        self.assertEqual(matched, 1)
        self.assertEqual(flagged, ["knight-miss-1457"])

    def test_unmatched_rows_are_skipped(self):
        # Different settings for the "same" level join to nothing: comparing them would be comparing two
        # different simulations, not a measurement of the device's speed.
        host = write_csv(self.tmp_path, "host.csv", [row("village-dinner", velocity=100)])
        device = write_csv(self.tmp_path, "device.csv", [row("village-dinner", velocity=30)])
        matched, flagged, _text = run_report(host, device)
        self.assertEqual(matched, 0)
        self.assertEqual(flagged, [])

    def test_level_names_join_without_regard_to_case(self):
        host = write_csv(self.tmp_path, "host.csv", [row("Village-Dvorik", mean=0.1, p95=0.3)])
        device = write_csv(self.tmp_path, "device.csv", [row("village-dvorik", mean=5.0, p95=15.0)])
        matched, _flagged, _text = run_report(host, device)
        self.assertEqual(matched, 1)

    def test_main_reports_failure_when_nothing_matches(self):
        host = write_csv(self.tmp_path, "host.csv", [row("knight-garden")])
        device = write_csv(self.tmp_path, "device.csv", [row("village-love")])
        self.assertEqual(bench_ratio.main([str(host), str(device)]), 1)

    def test_main_succeeds_when_something_matches(self):
        host = write_csv(self.tmp_path, "host.csv", [row("knight-garden", mean=0.02, p95=0.09)])
        device = write_csv(self.tmp_path, "device.csv", [row("knight-garden", mean=1.4, p95=6.3)])
        self.assertEqual(bench_ratio.main([str(host), str(device)]), 0)


if __name__ == "__main__":
    unittest.main()
