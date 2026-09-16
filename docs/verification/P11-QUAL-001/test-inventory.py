"""Builds the P11-QUAL-001 test inventory: what the suite contains and where
each test belongs.

Three sources, all read from the repository or the built test binary:

* `ctest --show-only=json-v1` for every registered CTest test (unit tests,
  process tests, architecture checks);
* the test binary's own `--list-tests --reporter xml` for each Catch2 test
  case with its tags and source file;
* the v0.1.0 tag — the P0-P10 release — for the legacy set: the TEST_CASE
  names and process-test names that existed before P11 began.

A test counts as P11 when it carries one of the feature tags below, or comes
from the reference-model tests. Legacy means the name was already there at
v0.1.0. A test in neither set is listed as "added since v0.1.0, not P11" so
nothing is hidden by the categories.

    python test-inventory.py <ctest.json> <catch-list.xml> > test-inventory.txt
"""
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

# Tag -> milestone. A test may carry several; every match is reported.
MILESTONES = [
    ("P11-FEAT-001 Revolve", lambda tags, path: "revolve" in tags),
    ("P11-FEAT-002 Chamfer", lambda tags, path: "chamfer" in tags),
    ("P11-FEAT-003 Fillet", lambda tags, path: "fillet" in tags),
    ("P11-FEAT-004 Hole", lambda tags, path: "hole" in tags),
    ("P11-FEAT-005 Linear pattern", lambda tags, path: "pattern" in tags and "circular" not in tags),
    ("P11-FEAT-006 Circular pattern", lambda tags, path: "circular" in tags),
    ("P11-FEAT-007 Mirror", lambda tags, path: "mirror" in tags),
    ("P11-FEAT-008 Sweep", lambda tags, path: "sweep" in tags),
    ("P11-FEAT-009 Loft", lambda tags, path: "loft" in tags),
    ("P11-REF-001 Reference models", lambda tags, path: "reference" in tags or "/tests/reference/" in path),
]


def gitOutput(*arguments):
    return subprocess.run(["git", *arguments], capture_output=True, text=True, check=True).stdout


def legacyNames():
    """The Catch2 and process test names of the P0-P10 release (v0.1.0)."""
    cases = set()
    for line in gitOutput("grep", "-h", 'TEST_CASE("', "v0.1.0", "--", "tests/").splitlines():
        match = re.search(r'TEST_CASE\("([^"]*)"', line)
        if match:
            cases.add(match.group(1))
    processes = set()
    for path in ("tests/CMakeLists.txt", "tests/architecture/CMakeLists.txt", "tests/compile_fail/CMakeLists.txt"):
        text = gitOutput("show", f"v0.1.0:{path}")
        processes.update(re.findall(r"bettercad_add_process_test\(([\w.\-]+)", text))
        processes.update(re.findall(r"add_test\(NAME ([\w.\-]+)", text))
        # The compile-failure cases are registered through a helper:
        # bettercad_compile_fail_case(<group> <source> <CASE> ...) becomes
        # compile_fail.<group>.<case>, lowercased with dashes.
        for group, case in re.findall(r"bettercad_compile_fail_case\((\w+) [^\n]*?\s(\w+) \"", text):
            processes.add(f"compile_fail.{group}.{case.lower().replace('_', '-')}")
    return cases, processes


def main(ctestJson, catchXml):
    registered = [test["name"] for test in json.load(open(ctestJson, encoding="utf-8"))["tests"]]
    cases = {}
    for case in ET.parse(catchXml).getroot().iter("TestCase"):
        name = case.findtext("Name")
        tags = set(re.findall(r"\[([^\]]+)\]", case.findtext("Tags") or ""))
        path = (case.find("SourceInfo").findtext("File") or "").replace("\\", "/")
        cases[name] = (tags, path)

    legacyCases, legacyProcesses = legacyNames()
    milestoneOf = {}
    for name, (tags, path) in cases.items():
        hits = [milestone for milestone, matches in MILESTONES if matches(tags, path)]
        if hits:
            milestoneOf[name] = hits

    unit = [t for t in registered if t.startswith("unit.")]
    process = [t for t in registered if not t.startswith("unit.")]
    unknown = [t for t in unit if t[len("unit."):] not in cases]

    p11 = [t for t in unit if t[len("unit."):] in milestoneOf]
    p11 += [t for t in process if "reference" in t]
    legacy = [t for t in unit if t[len("unit."):] in legacyCases]
    legacy += [t for t in process if t in legacyProcesses]
    newOther = [t for t in registered if t not in set(p11) and t not in set(legacy)]

    print("P11-QUAL-001 test inventory")
    print(f"revision {gitOutput('rev-parse', 'HEAD').strip()}")
    print()
    print(f"registered tests          {len(registered)}")
    print(f"  Catch2 unit tests       {len(unit)}")
    print(f"  process and check tests {len(process)}")
    print(f"legacy (present at v0.1.0, the P0-P10 release)  {len(legacy)}")
    print(f"P11 (feature or reference-model tags)           {len(p11)}")
    print(f"added since v0.1.0 without a P11 tag            {len(newOther)}")
    if unknown:
        print(f"registered but not listed by the binary: {len(unknown)}")
    print()
    print("P11 tests by milestone (a test may cover more than one):")
    for milestone, _ in MILESTONES:
        count = sum(1 for name in milestoneOf if milestone in milestoneOf[name])
        print(f"  {milestone:34} {count}")
    print()
    print("Added since v0.1.0 without a P11 feature tag. These are the core geometry, math and")
    print("build checks the P11 milestones brought with them (edge and face references, rigid")
    print("transforms, the boolean regression the reference models found, the examples layering")
    print("check), listed so no test hides between the categories:")
    for name in sorted(newOther):
        key = name[len("unit."):] if name.startswith("unit.") else name
        tags, path = cases.get(key, (set(), ""))
        where = path.split("/tests/")[-1] if path else "process test"
        print(f"  {name}\n      {where} [{']['.join(sorted(tags))}]" if tags else f"  {name}\n      {where}")
    print()
    print("Legacy tests, in order:")
    for name in sorted(legacy):
        print(f"  {name}")
    print()
    print("P11 tests, in order:")
    for name in sorted(p11):
        key = name[len("unit."):] if name.startswith("unit.") else name
        print(f"  {name}  ({', '.join(milestoneOf.get(key, ['P11-REF-001 Reference models']))})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
