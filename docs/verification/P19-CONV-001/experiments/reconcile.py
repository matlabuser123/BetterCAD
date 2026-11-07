"""Reconcile the P19-CONV-001 qualification against FREEZE.txt.

Every figure FREEZE.txt recorded in advance is asserted here against what the
run actually produced.  Nothing is eyeballed, and no pattern is end-anchored:
every line in qualification-times.txt carries a trailing space, which has
already cost four readings this session.
"""

import re
import subprocess
import sys
from pathlib import Path

Q = Path(r"C:\Users\Hasib\Desktop\CAD\docs\verification\P19-CONV-001\qualification")
REPO = Path(r"C:\Users\Hasib\Desktop\CAD")

times = (Q / "qualification-times.txt").read_text(encoding="utf-8", errors="replace")
out = (Q / "run-qualification.out").read_text(encoding="utf-8", errors="replace")

fails = []
notes = []


def check(label, got, want):
    ok = got == want
    print(f"  {'OK  ' if ok else 'FAIL'}  {label}: got {got!r}  want {want!r}")
    if not ok:
        fails.append(f"{label}: got {got!r} want {want!r}")


print("=" * 72)
print("1. THE VERDICT (not the launcher's exit code)")
print("=" * 72)

check("harness verdict sentence present",
      "Qualification passed: every stage exited 0." in out, True)
m = re.search(r"qualification finished [^,]+, (\d+) stage\(s\) failed", times)
check("failed-stage count parsed", m is not None, True)
check("failed stages", m.group(1) if m else None, "0")
check("no 'Qualification FAILED' sentence", "FAILED" in out.upper(), False)

print()
print("=" * 72)
print("2. STAGES: 17, each exit 0")
print("=" * 72)

# A stage line is '<name> <stage> exit <code> <timestamp>'.  Not end-anchored.
stage_lines = re.findall(r"^(\S+) (configure|clean attempt \d+|build|no-op rebuild|ctest) exit (\d+)",
                         times, re.MULTILINE)
repeat_lines = re.findall(r"^repeat (\S+) exit (\d+)", times, re.MULTILINE)
print(f"  per-preset stage lines: {len(stage_lines)}")
print(f"  repeat stage lines:     {len(repeat_lines)}")
check("total stages", len(stage_lines) + len(repeat_lines), 17)
nonzero = [s for s in stage_lines if s[2] != "0"] + [r for r in repeat_lines if r[1] != "0"]
check("stages with a non-zero exit", nonzero, [])

presets = sorted({s[0] for s in stage_lines})
check("presets", presets, ["debug-ext", "debug-shared-ext", "release-ext"])
check("repeat presets", sorted(r[0] for r in repeat_lines), ["debug-ext", "release-ext"])

print()
print("=" * 72)
print("3. TESTS PER PRESET: 6525 = 6449 + 48 + 28, zero failed")
print("=" * 72)

for preset in ["debug-ext", "release-ext", "debug-shared-ext"]:
    log = Q / f"ctest-{preset}.log"
    text = log.read_text(encoding="utf-8", errors="replace")
    mt = re.findall(r"100% tests passed out of (\d+)", text)
    check(f"{preset}: ctest summary lines", len(mt), 1)
    if mt:
        check(f"{preset}: tests total", mt[0], "6525")
    check(f"{preset}: no FAILED block", "The following tests FAILED" in text, False)
    check(f"{preset}: no 'tests failed' clause", bool(re.search(r"[1-9]\d* tests failed", text)), False)

print()
print("=" * 72)
print("4. REPEAT: 3258 selected per preset, 16290 EXECUTIONS = 3258 x 5")
print("=" * 72)

for preset in ["release-ext", "debug-ext"]:
    sel = re.findall(rf"^repeat {re.escape(preset)} selects (\d+) test\(s\)", times, re.MULTILINE)
    check(f"{preset}: selects line present once", len(sel), 1)
    if sel:
        check(f"{preset}: selected", sel[0], "3258")
    log = Q / f"ctest-repeat-{preset}.log"
    text = log.read_text(encoding="utf-8", errors="replace")
    # ctest result lines look like:  '  1/3258 Test   #1: name ... Passed  0.01 sec'
    results = re.findall(r"^\s*\d+/\d+ Test\s+#\d+:", text, re.MULTILINE)
    starts = re.findall(r"^\s+Start\s+\d+:", text, re.MULTILINE)
    check(f"{preset}: repeat result lines (one per test)", len(results), 3258)
    check(f"{preset}: repeat EXECUTIONS (Start markers) = 3258 x 5", len(starts), 16290)
    check(f"{preset}: executions per test", len(starts) / len(results) if results else 0, 5.0)
    nonpass = re.findall(r"^\s*\d+/\d+ Test\s+#\d+:.*\*\*\*", text, re.MULTILINE)
    check(f"{preset}: non-passing repeat lines", len(nonpass), 0)
    check(f"{preset}: no FAILED block", "The following tests FAILED" in text, False)
    mt = re.findall(r"100% tests passed out of (\d+)", text)
    check(f"{preset}: repeat summary", mt, ["3258"])

print()
print("=" * 72)
print("5. WARNINGS: 0, with WARNINGS_AS_ERRORS=ON read from each BUILT cache")
print("=" * 72)

build_root = Path("C:/bettercad/bc-build")
for preset in ["debug-ext", "release-ext", "debug-shared-ext"]:
    log = Q / f"build-{preset}.log"
    text = log.read_text(encoding="utf-8", errors="replace")
    warns = re.findall(r"\bwarning:", text)
    check(f"{preset}: 'warning:' occurrences in the build log", len(warns), 0)
    cache = build_root / preset / "CMakeCache.txt"
    if cache.exists():
        ct = cache.read_text(encoding="utf-8", errors="replace")
        wae = re.findall(r"BETTERCAD_WARNINGS_AS_ERRORS:BOOL=(\w+)", ct)
        check(f"{preset}: WARNINGS_AS_ERRORS in the built cache", wae, ["ON"])
    else:
        notes.append(f"{preset}: CMakeCache.txt absent at {cache} -- cannot read the built value")
        print(f"  NOTE  {preset}: built cache absent at {cache}")
    rb = Q / f"rebuild-{preset}.log"
    if rb.exists():
        print(f"  ..    {preset}: no-op rebuild log = {rb.read_text(errors='replace').strip()!r}")

print()
print("=" * 72)
print("6. THE EIGHT SUBTREE IDENTITIES: before == after == FREEZE.txt")
print("=" * 72)

PATHS = ["apps", "include", "src", "tests", "examples", "cmake",
         "CMakeLists.txt", "CMakePresets.json"]

freeze = (Q.parent / "FREEZE.txt").read_text(encoding="utf-8", errors="replace")
frozen = {}
for p in PATHS:
    mm = re.findall(rf"^\s+{re.escape(p)}\s+([0-9a-f]{{40}})", freeze, re.MULTILINE)
    if len(mm) != 1:
        fails.append(f"FREEZE.txt: {p} appears {len(mm)} times in the identity block")
        print(f"  FAIL  FREEZE.txt lists {p} {len(mm)} times")
        continue
    frozen[p] = mm[0]

for p in PATHS:
    # Deliberately NOT end-anchored: every line has a trailing space.
    mm = re.findall(rf"^  {re.escape(p)} ([0-9a-f]{{40}})", times, re.MULTILINE)
    check(f"{p}: recorded twice (before and after)", len(mm), 2)
    if len(mm) == 2:
        check(f"{p}: before == after", mm[0], mm[1])
        check(f"{p}: == FREEZE.txt", mm[0], frozen.get(p))

print()
print("=" * 72)
print("7. CANDIDATE == HEAD, and the frozen surface is still the frozen surface")
print("=" * 72)

cand = re.findall(r"^([0-9a-f]{40})", times, re.MULTILINE)
check("candidate recorded", len(cand), 1)
head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO,
                      capture_output=True, text=True).stdout.strip()
check("candidate == HEAD at read time", cand[0] if cand else None, head)

print()
print("=" * 72)
print("RESULT")
print("=" * 72)
for n in notes:
    print(f"  NOTE  {n}")
if fails:
    print(f"  {len(fails)} RECONCILIATION FAILURE(S):")
    for f in fails:
        print(f"    - {f}")
    sys.exit(1)
print("  Every figure FREEZE.txt recorded in advance is matched by the run.")
