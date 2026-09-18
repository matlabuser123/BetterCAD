"""Builds the P12-QUAL-001 test inventory and the P0-P11 regression check.

Three sources, all read from the repository or the built test binary:

* `ctest --show-only=json-v1` for every registered CTest test;
* the test binary's own `--list-tests --reporter xml` for each Catch2 test
  case with its tags and source file;
* two Git revisions for the baselines -- `v0.1.0` (the P0-P10 release) and
  the revision P11-QUAL-001 qualified, which is the baseline the
  "P0-P11 regression unchanged" gate is measured against.

A test is attributed to a P12 milestone by the source file it lives in, and
by tag where one file serves two milestones (FilletFeatureTests.cpp holds
P11's constant-radius fillet and P12's variable-radius one). Every test
carrying [p12] that matches no milestone is listed, so nothing hides between
the categories.

    python test-inventory.py <ctest.json> <catch-list.xml> <p11-revision>
"""
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

# Milestone -> (source-file basenames, extra tag predicate). A file listed
# here attributes every [p12] test in it; the predicate adds tests that live
# in a file shared with an earlier phase.
MILESTONES = [
    ("P12-PARAM-001  Parameter expressions",
     ["ExpressionTests.cpp", "ExpressionRegenerationTests.cpp", "ParameterExpressionTests.cpp",
      "ExpressionFileTests.cpp", "ExpressionCliTests.cpp"],
     lambda tags, path: "expressions" in tags),
    ("P12-SKETCH-001 Advanced sketch constraints",
     ["AdvancedConstraintSolverTests.cpp", "AdvancedConstraintDocumentTests.cpp"],
     lambda tags, path: False),
    ("P12-SKETCH-002 Ellipse and spline entities",
     ["EllipseSplineTests.cpp", "EllipseSplineDocumentTests.cpp", "CurvedProfileTests.cpp",
      "EllipseSplineFileTests.cpp"],
     lambda tags, path: bool({"ellipse", "spline", "bspline"} & tags)),
    ("P12-DATUM-001  Datums",
     ["DatumTests.cpp", "RigidTransformTests.cpp", "DatumFileTests.cpp"],
     lambda tags, path: "datum" in tags),
    ("P12-STREF-001  Stable face references",
     ["FaceReferenceTests.cpp", "FaceNameTests.cpp", "FaceReferenceFileTests.cpp"],
     lambda tags, path: "references" in tags),
    ("P12-SKETCH-003 Sketches on planar faces",
     ["SketchOnFaceTests.cpp"], lambda tags, path: False),
    ("P12-FEAT-001   Through-all extrude",
     ["ThroughAllExtrudeTests.cpp", "ThroughAllFileTests.cpp", "ThroughAllCliTests.cpp"],
     lambda tags, path: False),
    ("P12-FEAT-002   Split and combine",
     ["BodyOpsTests.cpp"], lambda tags, path: bool({"split", "combine"} & tags)),
    ("P12-FEAT-003   Shell",
     ["ShellFeatureTests.cpp"], lambda tags, path: "shell" in tags),
    ("P12-FEAT-004   Draft",
     ["DraftFeatureTests.cpp"], lambda tags, path: "draft" in tags),
    ("P12-FEAT-005   Rib",
     ["RibFeatureTests.cpp"], lambda tags, path: "rib" in tags),
    ("P12-FEAT-006   Variable-radius fillet",
     [], lambda tags, path: "variable" in tags),
    ("P12-HOLE-001   Hole standards",
     ["HoleStandardTests.cpp", "HoleTests.cpp", "HoleStandardFileTests.cpp"],
     lambda tags, path: bool({"standards", "thread"} & tags)),
    ("P12-PATTERN-001 Advanced patterns",
     ["PatternDistributionTests.cpp", "PatternNestingTests.cpp", "PatternInstanceFileTests.cpp",
      "PatternInstanceCliTests.cpp"],
     lambda tags, path: "nesting" in tags),
    ("P12-SWEEP-001  Sweeps: paths, twist, guides",
     ["SpatialSweepTests.cpp", "SweptPathTests.cpp", "SpatialSweepFileTests.cpp",
      "SpatialSweepCliTests.cpp"],
     lambda tags, path: "spatial" in tags),
    ("P12-LOFT-001   Lofts: shapes and smoothness",
     ["LoftShapeTests.cpp", "LoftShapeFeatureTests.cpp", "LoftFileTests.cpp"],
     lambda tags, path: {"loft", "p12"} <= tags),
    ("P12-PARAM-002  Design equations and configurations",
     ["ConfigurationTests.cpp", "ConfigurationFeatureTests.cpp"],
     lambda tags, path: bool({"configurations", "table"} & tags)),
    ("P12-REF-001    Production reference models",
     ["MotorMountTests.cpp", "GearboxCoverTests.cpp", "ManifoldTubeTests.cpp",
      "TransitionDuctTests.cpp", "IndexPlateTests.cpp", "RibbedBracketTests.cpp",
      "P12ModelsTests.cpp"],
     lambda tags, path: False),
]


def gitOutput(*arguments):
    return subprocess.run(["git", *arguments], capture_output=True, text=True, check=True).stdout


def namesAt(revision):
    """The Catch2 case names and process-test names registered at `revision`."""
    cases = set()
    for line in gitOutput("grep", "-h", 'TEST_CASE("', revision, "--", "tests/").splitlines():
        match = re.search(r'TEST_CASE\("([^"]*)"', line)
        if match:
            cases.add(match.group(1))
    processes = set()
    for path in ("tests/CMakeLists.txt", "tests/architecture/CMakeLists.txt",
                 "tests/compile_fail/CMakeLists.txt"):
        try:
            text = gitOutput("show", f"{revision}:{path}")
        except subprocess.CalledProcessError:
            continue
        processes.update(re.findall(r"bettercad_add_process_test\(([\w.\-]+)", text))
        processes.update(re.findall(r"add_test\(NAME ([\w.\-]+)", text))
        # Anchored, so the function DEFINITION line is not read as a
        # registration of a test named after its own parameters.
        for group, case in re.findall(r"^bettercad_compile_fail_case\((\w+) [^\n]*?\s(\w+) \"",
                                      text, re.MULTILINE):
            processes.add(f"compile_fail.{group}.{case.lower().replace('_', '-')}")
        # Names built from a CMake loop variable, e.g.
        #   foreach(_model_volume "shaft|Shaft|EndChamfers|97773\.099" ...)
        #     bettercad_add_process_test(cli.validate.reference.${_stem} ...)
        # capture as the bare prefix "cli.validate.reference.". Expand them
        # from the loop's own rows so the tests are really checked, rather
        # than dropping a whole family as unverifiable.
        for body in re.findall(r"foreach\((.*?)endforeach\(\)", text, re.DOTALL):
            registration = re.search(r"bettercad_add_process_test\(([\w.\-]+)\$\{", body)
            if not registration:
                continue
            # Only the loop's HEADER list, not the body: an ARGS line such as
            # ARGS "validate|${_reference}/${_stem}.bcad" also holds a '|'
            # and would otherwise read as a row whose stem is "validate".
            header = re.match(r"\s*\w+\s*((?:\s*\"[^\"]*\"\s*)+)", body)
            if not header:
                continue
            rows = re.findall(r"\"([^\"]*)\"", header.group(1))
            for row in rows:
                processes.add(registration.group(1) + row.split("|")[0])
    # Anything still ending in '.' is a template this expansion did not
    # cover; report it rather than counting it as a missing test.
    processes = {n for n in processes if not n.endswith(".")}
    return cases, processes


def main(ctestJson, catchXml, p11Revision):
    registered = [test["name"] for test in json.load(open(ctestJson, encoding="utf-8"))["tests"]]
    cases = {}
    for case in ET.parse(catchXml).getroot().iter("TestCase"):
        name = case.findtext("Name")
        tags = set(re.findall(r"\[([^\]]+)\]", case.findtext("Tags") or ""))
        path = (case.find("SourceInfo").findtext("File") or "").replace("\\", "/")
        cases[name] = (tags, path)

    milestoneOf = {}
    for name, (tags, path) in cases.items():
        basename = path.rsplit("/", 1)[-1]
        hits = []
        for milestone, files, extra in MILESTONES:
            if (basename in files and "p12" in tags) or extra(tags, path):
                hits.append(milestone)
        if hits:
            milestoneOf[name] = hits

    unit = [t for t in registered if t.startswith("unit.")]
    process = [t for t in registered if not t.startswith("unit.")]
    key = lambda t: t[len("unit."):] if t.startswith("unit.") else t

    p12Tagged = [t for t in unit if "p12" in cases.get(key(t), (set(), ""))[0]]
    attributed = [t for t in p12Tagged if key(t) in milestoneOf]
    unattributed = [t for t in p12Tagged if key(t) not in milestoneOf]

    releaseCases, releaseProcesses = namesAt("v0.1.0")
    p11Cases, p11Processes = namesAt(p11Revision)

    def presence(baseCases, baseProcesses):
        haveUnit = {key(t) for t in unit}
        haveProcess = set(process)
        missingCases = sorted(n for n in baseCases if n not in haveUnit)
        missingProcesses = sorted(n for n in baseProcesses if n not in haveProcess)
        return missingCases, missingProcesses

    print("P12-QUAL-001 test inventory and P0-P11 regression check")
    print(f"revision {gitOutput('rev-parse', 'HEAD').strip()}")
    print()
    print(f"registered tests          {len(registered)}")
    print(f"  Catch2 unit tests       {len(unit)}")
    print(f"  process and check tests {len(process)}")
    print(f"carrying [p12]            {len(p12Tagged)}")
    print()

    for label, (baseCases, baseProcesses), revision in (
            ("P0-P10, the v0.1.0 release", (releaseCases, releaseProcesses), "v0.1.0"),
            ("P0-P11, the P11-QUAL-001 qualified tree", (p11Cases, p11Processes), p11Revision)):
        missingCases, missingProcesses = presence(baseCases, baseProcesses)
        print(f"{label}  ({revision[:12]})")
        print(f"  Catch2 cases then            {len(baseCases)}")
        print(f"  process tests then           {len(baseProcesses)}")
        print(f"  Catch2 cases now missing     {len(missingCases)}")
        print(f"  process tests now missing    {len(missingProcesses)}")
        for name in missingCases:
            print(f"    MISSING CASE    {name}")
        for name in missingProcesses:
            print(f"    MISSING PROCESS {name}")
        print()

    print("P12 tests by milestone (a test may cover more than one):")
    for milestone, _, _ in MILESTONES:
        count = sum(1 for name in milestoneOf if milestone in milestoneOf[name])
        flag = "   *** NONE ***" if count == 0 else ""
        print(f"  {milestone:44} {count}{flag}")
    print()
    print(f"[p12] tests attributed to a milestone   {len(attributed)}")
    print(f"[p12] tests attributed to none          {len(unattributed)}")
    for name in sorted(unattributed):
        tags, path = cases[key(name)]
        print(f"    {name}\n        {path.split('/tests/')[-1]} [{']['.join(sorted(tags))}]")


if __name__ == "__main__":
    main(*sys.argv[1:4])
