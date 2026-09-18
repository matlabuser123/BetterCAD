"""Largest measured deviations in a measured-values file (values.py output).

Usage: python deviations.py [--worst N] <values file> [test-case name prefix]...

Reads every Catch2 floating-point matcher expansion:

    X is within T of Y                    absolute: |X - Y|
    X and Y are within P% of each other   relative: |X - Y| / max(|X|, |Y|)

and prints, per test file and tolerance, the number of checks and the
largest deviation, for the test cases whose names start with one of the
prefixes (all of them without prefixes). With --worst N it also lists the N
largest deviations of each group with their test case, section and
context.
"""
import re
import sys
from collections import defaultdict

ABS = re.compile(r"=> (\S+) is within (\S+) of (\S+?)(?: with message: (.*))?$")
REL = re.compile(r"=> (\S+) and (\S+) are within (\S+)% of each other(?: with message: (.*))?$")
WHERE = re.compile(r"^(\S+?):\d+\s")


def main(argv):
    worst = 0
    if argv and argv[0] == "--worst":
        worst = int(argv[1])
        argv = argv[2:]
    path, prefixes = argv[0], argv[1:]
    groups = defaultdict(list)
    section = None
    where = None
    with open(path, encoding="utf-8") as values:
        for line in values:
            line = line.rstrip("\n")
            if line.startswith("== "):
                section = line[3:]
                continue
            match = WHERE.match(line)
            if match:
                where = match.group(1)
                continue
            case = section.split(" / ")[0] if section else None
            if case is None or (prefixes and not case.startswith(tuple(prefixes))):
                continue
            absolute = ABS.search(line)
            relative = REL.search(line)
            if absolute:
                x, tolerance, y = (float(v) for v in absolute.groups()[:3])
                key = (where, "abs", tolerance)
                deviation = abs(x - y)
                context = absolute.group(4) or ""
            elif relative:
                x, y, percent = (float(v) for v in relative.groups()[:3])
                key = (where, "rel", percent / 100.0)
                scale = max(abs(x), abs(y))
                deviation = abs(x - y) / scale if scale > 0 else 0.0
                context = relative.group(4) or ""
            else:
                continue
            groups[key].append((deviation, section, context, x, y))
    print(f"{'file':<32} {'kind':<4} {'tolerance':>10} {'checks':>7} {'largest':>10}")
    for (where, kind, tolerance), entries in sorted(groups.items()):
        print(f"{where:<32} {kind:<4} {tolerance:>10.1e} {len(entries):>7} {max(e[0] for e in entries):>10.2e}")
    if worst:
        for (where, kind, tolerance), entries in sorted(groups.items()):
            print(f"\n{where} {kind} {tolerance:.1e}: largest {worst}")
            for deviation, section, context, x, y in sorted(entries, key=lambda e: -e[0])[:worst]:
                print(f"  {deviation:.3e}  measured {x!r} expected {y!r}  {section}  {context}")


if __name__ == "__main__":
    main(sys.argv[1:])
