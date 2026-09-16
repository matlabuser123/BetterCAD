"""Splits a CTest run's results into the P11-QUAL-001 categories.

Reads a `ctest` log and the test inventory, and reports how many tests of
each category ran and passed, from the run's own output rather than from test
names alone. Categories come from test-inventory.py: legacy (present at
v0.1.0, the P0-P10 release), P11 (feature and reference-model tags), and the
core geometry, math and build checks the P11 milestones added.

    python categorize-results.py <test-inventory.txt> <ctest.log> [more.log ...]
"""
import re
import sys

RESULT = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (.*?) \.+\**\s*(Passed|Failed|Timeout|Not Run|Skipped)", re.M)


def inventory(path):
    """{name: category} from the inventory's three listings."""
    category = {}
    section = None
    for line in open(path, encoding="utf-8"):
        stripped = line.strip()
        if stripped.startswith("Added since v0.1.0 without a P11 feature tag"):
            section = "additional"
        elif stripped.startswith("Legacy tests, in order:"):
            section = "legacy"
        elif stripped.startswith("P11 tests, in order:"):
            section = "P11"
        elif section and line.startswith("  ") and not line.startswith("      "):
            name = stripped.split("  (")[0]
            if name and not name.endswith(":"):
                category.setdefault(name, section)
    return category


def main(inventoryPath, *logs):
    category = inventory(inventoryPath)
    print("P11-QUAL-001 results by category")
    print(f"inventory: {inventoryPath}")
    print()
    for log in logs:
        text = open(log, encoding="utf-8", errors="replace").read()
        results = RESULT.findall(text)
        counts = {}
        failures = []
        for name, outcome in results:
            group = category.get(name, "unclassified")
            passed, total = counts.get(group, (0, 0))
            counts[group] = (passed + (1 if outcome == "Passed" else 0), total + 1)
            if outcome != "Passed":
                failures.append(f"{name}: {outcome}")
        print(f"== {log}")
        print(f"   tests recorded in the log: {len(results)}")
        for group in ("legacy", "P11", "additional", "unclassified"):
            if group in counts:
                passed, total = counts[group]
                print(f"   {group:13} {passed}/{total} passed")
        if failures:
            print("   FAILURES:")
            for failure in failures:
                print(f"     {failure}")
        else:
            print("   no test reported anything but Passed")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
