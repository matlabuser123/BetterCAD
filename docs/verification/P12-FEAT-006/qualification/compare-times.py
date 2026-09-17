"""Per-test CTest times of two or more logs, side by side.

    python compare-times.py <label>=<ctest log> ... [--top N] [--match REGEX]

Prints the total of the per-test times of each log (CTest's own
"Passed x sec" figures; the wall time depends on -j), and the N tests that
took longest in any of the logs, with their times in every log. Tests
missing from a log show as "-".
"""
import re
import sys

LINE = re.compile(r"Test\s+#\d+:\s+(.+?)\s*\.*\s+(Passed|Failed|\*{3}\S+)\s+([\d.]+) sec")


def read(path):
    times = {}
    with open(path, encoding="utf-8", errors="replace") as log:
        for line in log:
            match = LINE.search(line)
            if match:
                # Two tests may share a name; the later ones get a suffix.
                name = match.group(1)
                count = 1
                while name in times:
                    count += 1
                    name = f"{match.group(1)} ({count})"
                times[name] = float(match.group(3))
    return times


def main(argv):
    top = 25
    pattern = None
    runs = []
    args = iter(argv)
    for arg in args:
        if arg == "--top":
            top = int(next(args))
        elif arg == "--match":
            pattern = re.compile(next(args))
        else:
            label, _, path = arg.partition("=")
            runs.append((label, read(path)))
    names = set()
    for _, times in runs:
        names.update(times)
    if pattern:
        names = {n for n in names if pattern.search(n)}
    for label, times in runs:
        chosen = [t for n, t in times.items() if n in names]
        print(f"{label:>14}: {len(chosen)} tests, {sum(chosen):9.2f} s summed")
    print()
    slowest = sorted(names, key=lambda n: -max(t.get(n, 0.0) for _, t in runs))[:top]
    header = "".join(f"{label:>14}" for label, _ in runs)
    print(f"{'test':<70}{header}")
    for name in slowest:
        cells = "".join(f"{times[name]:14.2f}" if name in times else f"{'-':>14}" for _, times in runs)
        print(f"{name:<70}{cells}")


if __name__ == "__main__":
    main(sys.argv[1:])
