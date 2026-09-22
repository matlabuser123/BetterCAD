"""P13-QUAL-001: the committed assembly models against their builders.

Runs the reference-model example program in a FRESH PROCESS, writes every
model to a scratch directory, and compares each one byte for byte with the
file committed in the repository. Also checks the solve line the program
prints for each model against the expectation derived on paper.

    python reference-models.py <preset>

Exit code 0 only if every model is byte-identical and every solve line agrees.
"""

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PRESET = sys.argv[1] if len(sys.argv) > 1 else "release"
EXE = ROOT / "build" / PRESET / "bin" / "bettercad_example_reference_models.exe"
MODELS = ROOT / "examples" / "models" / "reference"

# Hand-derived from the qualified mate equation table, exactly as in
# end-to-end.py. RM-H's DOF is 6 - rank = 6 - 1 = 5, because its two
# contradictory Distance mates produce identical Jacobian rows.
EXPECTED = {
    "GroundedPair":     ("RM-A", "FULLY_CONSTRAINED", 0, 0, 0),
    "ConstrainedStack": ("RM-B", "FULLY_CONSTRAINED", 12, 12, 0),
    "ShaftInBore":      ("RM-C", "UNDER_CONSTRAINED", 6, 4, 2),
    "JointSet":         ("RM-D", "UNDER_CONSTRAINED", 24, 17, 7),
    "ConfiguredFrame":  ("RM-E", "FULLY_CONSTRAINED", 18, 18, 0),
    "DrivenCover":      ("RM-F", "FULLY_CONSTRAINED", 6, 6, 0),
    "Machine":          ("RM-G", "UNDER_CONSTRAINED", 42, 40, 2),
    "FaultCases":       ("RM-H", "INCONSISTENT", 6, 2, 5),
}

failures = []
checks = 0


def check(ok, what, detail=""):
    global checks
    checks += 1
    if not ok:
        failures.append(what + ((": " + detail) if detail else ""))
    print(("PASS  " if ok else "FAIL  ") + what + ((" -- " + detail) if detail and not ok else ""))


print("=== P13-QUAL-001 reference models, preset " + PRESET + " ===")
check(EXE.is_file(), "the reference-model example program exists")
if not EXE.is_file():
    sys.exit(2)

with tempfile.TemporaryDirectory() as tmp:
    out = Path(tmp) / "models"
    out.mkdir()
    run = subprocess.run([str(EXE), "--out", str(out)], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    check(run.returncode == 0, "the example program exits 0", run.stderr.strip()[:300])
    print(run.stdout)

    # --- every committed model is byte-identical to what the builder makes ---
    for name in EXPECTED:
        label = EXPECTED[name][0]
        stem = "assembly_" + re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()
        committed = MODELS / (stem + ".bcad")
        produced = out / (stem + ".bcad")
        check(committed.is_file(), label + " " + stem + ".bcad is committed")
        check(produced.is_file(), label + " the builder produced " + stem + ".bcad")
        if committed.is_file() and produced.is_file():
            check(committed.read_bytes() == produced.read_bytes(),
                  label + " committed file is byte-identical to the builder's output")

    # --- the solve each model reports, against the paper derivation ---
    # Lines look like:
    #   solve UNDER_CONSTRAINED unknowns 42 equations 40 dof 2 residual_mm 0
    text = run.stdout
    for name in EXPECTED:
        label, status, unknowns, equations, dof = EXPECTED[name]
        block = re.search(r"== " + name + r" \(" + label + r"\)(.*?)(?=\n== |\Z)", text, re.S)
        check(block is not None, label + " appears in the program's report")
        if block is None:
            continue
        m = re.search(r"solve (\w+) unknowns (\d+) equations (\d+) dof (\d+)", block.group(1))
        check(m is not None, label + " reports a solve line", block.group(1).strip()[:200])
        if m is None:
            continue
        check(m.group(1) == status, label + " solves " + status, "got " + m.group(1))
        check(int(m.group(2)) == unknowns, label + " has " + str(unknowns) + " unknowns",
              "got " + m.group(2))
        check(int(m.group(3)) == equations, label + " has " + str(equations) + " equations",
              "got " + m.group(3))
        check(int(m.group(4)) == dof, label + " has " + str(dof) + " degrees of freedom",
              "got " + m.group(4))

    # --- running it twice gives the same bytes -------------------------------
    second = Path(tmp) / "again"
    second.mkdir()
    run2 = subprocess.run([str(EXE), "--out", str(second)], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    check(run2.returncode == 0, "a second run exits 0")
    for name in EXPECTED:
        stem = "assembly_" + re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()
        a, b = out / (stem + ".bcad"), second / (stem + ".bcad")
        if a.is_file() and b.is_file():
            check(a.read_bytes() == b.read_bytes(),
                  EXPECTED[name][0] + " two runs of the builder agree byte for byte")

print("\n=== " + str(checks - len(failures)) + "/" + str(checks) + " checks passed ===")
for f in failures:
    print("FAILED: " + f)
sys.exit(1 if failures else 0)
