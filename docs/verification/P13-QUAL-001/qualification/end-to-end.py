"""P13-QUAL-001 end-to-end qualification of the committed assembly models.

Drives the REAL bettercad-cli process against the REAL committed .bcad files,
and checks what comes back against expectations derived on paper from the mate
equation table that P13-SOLVE-001 and P13-MATE-002 qualified -- never against
another BetterCAD result.

    python end-to-end.py <preset>

Exit code 0 only if every check passed.
"""

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PRESET = sys.argv[1] if len(sys.argv) > 1 else "release"
CLI = ROOT / "build" / PRESET / "bin" / "bettercad-cli.exe"
MODELS = ROOT / "examples" / "models" / "reference"

# Derived by hand from the qualified mate equation counts, BEFORE any solver
# output was read:
#     Fixed grounds a component (removes its 6 unknowns, adds no equation)
#     Distance 1  Perpendicular 1  Angle 1  Parallel 2  Coincident 3
#     Planar 3    Concentric 4     Cylindrical 4        Revolute 5   Slider 5
#     unknowns = 6 * (active components - grounded); DOF = unknowns - rank
EXPECTED = {
    # stem:                     (label, components, mates, dof, status, exit)
    "assembly_grounded_pair":     ("RM-A", 2, 2, 0, "FULLY_CONSTRAINED", 0),
    "assembly_constrained_stack": ("RM-B", 3, 11, 0, "FULLY_CONSTRAINED", 0),
    "assembly_shaft_in_bore":     ("RM-C", 2, 2, 2, "UNDER_CONSTRAINED", 0),
    "assembly_joint_set":         ("RM-D", 5, 5, 7, "UNDER_CONSTRAINED", 0),
    "assembly_configured_frame":  ("RM-E", 4, 16, 0, "FULLY_CONSTRAINED", 0),
    "assembly_driven_cover":      ("RM-F", 2, 5, 0, "FULLY_CONSTRAINED", 0),
    "assembly_machine":           ("RM-G", 8, 31, 2, "UNDER_CONSTRAINED", 0),
    # RM-H is committed broken: two Distance mates on the SAME pair of planes
    # at different distances. Their Jacobian rows are identical, so the rank is
    # 1, not 2, and the reported DOF is 6 - 1 = 5 -- of the system it could not
    # satisfy. That is why RM-H is the one model whose DOF is not
    # unknowns - equations.
    "assembly_fault_cases":       ("RM-H", 2, 3, 5, "INCONSISTENT", 1),
}

failures = []
checks = 0


def check(ok, what, detail=""):
    global checks
    checks += 1
    if not ok:
        failures.append(what + ((": " + detail) if detail else ""))
    line = ("PASS  " if ok else "FAIL  ") + what
    if detail and not ok:
        line += " -- " + detail
    print(line)


def cli(*args, cwd=None):
    r = subprocess.run([str(CLI)] + [str(a) for a in args], capture_output=True,
                       text=True, encoding="utf-8", errors="replace", cwd=cwd)
    return r.returncode, r.stdout, r.stderr


def number(pattern, text, what):
    m = re.search(pattern, text)
    if not m:
        failures.append(what + ": pattern " + repr(pattern) + " not found")
        return None
    return int(m.group(1))


print("=== P13-QUAL-001 end-to-end, preset " + PRESET + " ===")
print("CLI  " + str(CLI))
check(CLI.is_file(), "the CLI executable exists")
if not CLI.is_file():
    sys.exit(2)

rc, out, _ = cli("version")
check(rc == 0, "version exits 0")
print(out.strip()[:400])

# --- 1. every committed model, as a real process -----------------------------
print("\n--- solve / status on every committed model ---")
for stem in EXPECTED:
    label, comps, mates, dof, status, want_exit = EXPECTED[stem]
    path = MODELS / (stem + ".bcad")
    check(path.is_file(), label + " " + stem + ".bcad is committed")

    rc, out, err = cli("solve", path)
    check(rc == want_exit, label + " solve exits " + str(want_exit),
          "got " + str(rc) + ": " + err.strip()[:200])
    check(status in out, label + " solve reports " + status, out.strip()[-300:])
    got = number(r"(\d+) degree", out, label + " DOF")
    check(got == dof, label + " reports " + str(dof) + " degrees of freedom", "got " + str(got))
    if want_exit != 0:
        # A contradictory assembly must NAME its conflicting mates, not just fail.
        named = ("Near" in out or "Far" in out or "conflict" in out.lower())
        check(named, label + " names its conflicting mates", out.strip()[-400:])

    # `status` solves too, and its exit code follows the document: an
    # assembly that cannot be satisfied is a non-zero exit, not a clean
    # report of a broken model.
    rc, out, _ = cli("status", path)
    check(rc == want_exit, label + " status exits " + str(want_exit), "got " + str(rc))
    got = number(r"Components \((\d+)\)", out, label + " component count")
    check(got == comps, label + " status lists " + str(comps) + " components", "got " + str(got))
    got = number(r"Mates \((\d+)\)", out, label + " mate count")
    check(got == mates, label + " status lists " + str(mates) + " mates", "got " + str(got))

# --- 2. a real edit workflow, one fresh process per step ---------------------
print("\n--- edit / save / reload / solve, one process per step ---")
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    work = tmp / "shaft.bcad"
    shutil.copy(MODELS / "assembly_shaft_in_bore.bcad", work)
    before = work.read_bytes()

    rc, out, err = cli("solve", work)
    check(rc == 0 and "UNDER_CONSTRAINED" in out, "RM-C copy solves before the edit", err[:200])

    rc, out, err = cli("component-place", work, "ShaftPin", "--z", "12mm")
    check(rc == 0, "component-place succeeds", err.strip()[:300])
    check(work.read_bytes() != before, "component-place actually wrote the file")

    rc, out, err = cli("solve", work)
    check(rc == 0, "the edited document still solves", err[:200])
    got = number(r"(\d+) degree", out, "edited DOF")
    check(got == 2, "moving a component along a free axis leaves the DOF at 2", "got " + str(got))

    # A failed edit must leave the file byte-identical (ADR-009).
    after_edit = work.read_bytes()
    rc, out, err = cli("component-place", work, "NoSuchComponent", "--z", "5mm")
    check(rc != 0, "an edit naming a missing component fails")
    check(work.read_bytes() == after_edit, "a failed edit leaves the file byte-identical")

    # A batch that fails part way must write nothing at all.
    script = tmp / "half.txt"
    # The first line is valid and the second is not, so this fails PART WAY:
    # the whole point is that the good first edit must not survive either.
    script.write_text("component-place ShaftPin --z 30mm\ncomponent-place Ghost --z 1mm\n",
                      encoding="utf-8")
    rc, out, err = cli("batch", work, script)
    check(rc != 0, "a batch that fails part way exits non-zero")
    check(work.read_bytes() == after_edit, "a failed batch leaves the file byte-identical")

    rc, out, _ = cli("status", work)
    check(rc == 0 and "ShaftPin" in out, "the saved document reloads in a fresh process")

# --- 3. configuration switching, through the CLI -----------------------------
print("\n--- configuration switching changes what is in force ---")
with tempfile.TemporaryDirectory() as tmp:
    work = Path(tmp) / "machine.bcad"
    shutil.copy(MODELS / "assembly_machine.bcad", work)

    rc, base, _ = cli("status", work, "--configuration", "Assembled")
    rc2, bare, _ = cli("status", work, "--configuration", "Bare")
    check(rc == 0 and rc2 == 0, "status runs in both configurations")
    in_force_assembled = base.count("in force")
    in_force_bare = bare.count("in force")
    check(in_force_bare < in_force_assembled,
          "the Bare configuration puts fewer objects in force",
          "Assembled " + str(in_force_assembled) + ", Bare " + str(in_force_bare))
    check(bare.count("suppressed") >= 5, "Bare suppresses its five components",
          str(bare.count("suppressed")) + " suppressed")

    # Switching there and back must give exactly what never leaving gives.
    rc, first, _ = cli("solve", work, "--configuration", "Assembled")
    rc2, _, _ = cli("solve", work, "--configuration", "Bare")
    rc3, back, _ = cli("solve", work, "--configuration", "Assembled")
    check(rc == rc3, "A -> B -> A gives the same exit code")
    check(first == back, "A -> B -> A gives byte-identical solve output")

# --- 4. STEP export and read-back -------------------------------------------
print("\n--- STEP export, then read back what is in the file ---")
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    for stem in EXPECTED:
        label, comps, _m, _d, _s, want_exit = EXPECTED[stem]
        step = tmp / (stem + ".step")
        rc, out, err = cli("export-step", MODELS / (stem + ".bcad"), step)
        if want_exit != 0:
            # An assembly that does not solve has no honest placement to write.
            check(rc != 0, label + " export-step refuses an unsolvable assembly", out[:200])
            check(not step.exists() or step.stat().st_size == 0, label + " wrote no STEP file")
            continue
        check(rc == 0, label + " export-step exits 0", err.strip()[:300])
        check(step.is_file() and step.stat().st_size > 0, label + " wrote a STEP file")
        text = step.read_text(encoding="utf-8", errors="replace")
        # Counted from the file itself, not from anything BetterCAD reports.
        occurrences = text.count("NEXT_ASSEMBLY_USAGE_OCCURRENCE")
        check(occurrences == comps,
              label + " STEP holds one occurrence per active component (" + str(comps) + ")",
              "found " + str(occurrences))
        solid = ("ADVANCED_BREP_SHAPE_REPRESENTATION" in text or "MANIFOLD_SOLID_BREP" in text)
        check(solid, label + " STEP carries solid geometry")

    # A suppressed component must be measurably absent from the export.
    work = tmp / "machine.bcad"
    shutil.copy(MODELS / "assembly_machine.bcad", work)
    rc, _, err = cli("configuration-activate", work, "Bare")
    check(rc == 0, "configuration-activate Bare succeeds", err.strip()[:200])
    bare_step = tmp / "machine_bare.step"
    rc, _, err = cli("export-step", work, bare_step)
    check(rc == 0, "export-step succeeds in the Bare configuration", err.strip()[:300])
    if bare_step.is_file():
        bare_text = bare_step.read_text(encoding="utf-8", errors="replace")
        bare_occurrences = bare_text.count("NEXT_ASSEMBLY_USAGE_OCCURRENCE")
        check(bare_occurrences == 3,
              "the Bare export holds only the 3 components in force (5 are suppressed)",
              "found " + str(bare_occurrences))

# --- 5. determinism of the CLI answer ----------------------------------------
print("\n--- repeated CLI runs agree exactly ---")
for stem in ("assembly_machine", "assembly_joint_set", "assembly_fault_cases"):
    runs = [cli("solve", MODELS / (stem + ".bcad")) for _ in range(3)]
    check(len(set(r[0] for r in runs)) == 1, stem + ": three runs agree on the exit code")
    check(len(set(r[1] for r in runs)) == 1, stem + ": three runs agree byte for byte on stdout")

# --- 6. the committed files hold intent, never a solved transform ------------
print("\n--- the committed files hold intent, never a solved transform ---")
for stem in EXPECTED:
    text = json.dumps(json.loads((MODELS / (stem + ".bcad")).read_text(encoding="utf-8")))
    check("transform" not in text and "solved" not in text,
          stem + ".bcad persists no solved transform")

print("\n=== " + str(checks - len(failures)) + "/" + str(checks) + " checks passed ===")
for f in failures:
    print("FAILED: " + f)
sys.exit(1 if failures else 0)
