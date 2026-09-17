"""Compares CTest logs of this milestone with the P11 qualification's by test name.

Usage: python compare-regression.py <baseline ctest log> <ctest log>...

For each log: every test name in the baseline must be present and have passed;
names that are new are listed. Prints a summary and exits non-zero on any
missing or failed baseline test.
"""
import re
import sys

LINE = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (.+?) \.+\s*(\*{0,3}\S.*?)\s+[\d.]+ sec$")


def results(path):
    """Maps test name -> list of outcomes ('Passed', '***Failed', ...)."""
    found = {}
    with open(path, encoding="utf-8", errors="replace") as log:
        for line in log:
            match = LINE.match(line.rstrip("\n"))
            if match:
                found.setdefault(match.group(1), []).append(match.group(2).strip())
    return found


def main():
    baseline = results(sys.argv[1])
    ok = True
    print(f"baseline {sys.argv[1]}: {len(baseline)} distinct names")
    for path in sys.argv[2:]:
        current = results(path)
        missing = sorted(set(baseline) - set(current))
        failed = sorted(n for n in baseline if n in current and any(o != "Passed" for o in current[n]))
        new = sorted(set(current) - set(baseline))
        not_passed = sorted(n for n, o in current.items() if any(x != "Passed" for x in o))
        print(f"\n{path}: {len(current)} distinct names, {sum(len(o) for o in current.values())} entries")
        print(f"  baseline names missing: {len(missing)}")
        for name in missing:
            print(f"    {name}")
        print(f"  baseline names not passed: {len(failed)}")
        for name in failed:
            print(f"    {name}: {current[name]}")
        print(f"  entries not passed (any test): {len(not_passed)}")
        print(f"  new names: {len(new)}")
        for name in new:
            print(f"    {name}")
        ok = ok and not missing and not failed and not not_passed
    print("\nRESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
