"""Splits the per-test time change of two CTest logs by test length.

    python timing-split.py <old ctest log> <new ctest log>

For the tests both logs have: the summed change, the number of tests under
0.5 s (in the old log) and their summed change, and for the tests of 0.5 s
or more the median ratio of new to old time and the share that got slower.
The new tests' summed time is printed too. The logs are read with
compare-times.py's parser.
"""
import importlib.util
import statistics
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location("compare_times", Path(__file__).with_name("compare-times.py"))
compare_times = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare_times)


def main(old_path, new_path):
    old = compare_times.read(old_path)
    new = compare_times.read(new_path)
    common = [name for name in old if name in new]
    added = [name for name in new if name not in old]
    missing = [name for name in old if name not in new]
    short = [name for name in common if old[name] < 0.5]
    long = [name for name in common if old[name] >= 0.5]
    ratios = [new[name] / old[name] for name in long]
    print(f"  tests in both logs: {len(common)}; only in the new log: {len(added)}; only in the old log: {len(missing)}")
    print(f"  new tests: {sum(new[n] for n in added):.2f} s summed")
    print(f"  existing tests: {sum(new[n] - old[n] for n in common):+.2f} s summed change")
    print(f"  existing tests under 0.5 s: {len(short)}, {sum(new[n] - old[n] for n in short):+.2f} s summed change")
    print(f"  existing tests of 0.5 s or more: {len(long)}, median new/old ratio {statistics.median(ratios):.3f}, "
          f"{sum(1 for r in ratios if r > 1.0) / len(ratios):.2f} of them slower")
    top = sorted(common, key=lambda n: -old[n])[:12]
    moved = max(top, key=lambda n: abs(new[n] - old[n]))
    print(f"  largest move among the 12 slowest: {new[moved] - old[moved]:+.2f} s ({moved})")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
