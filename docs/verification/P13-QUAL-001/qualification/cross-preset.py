"""P13-QUAL-001: do the three presets agree, byte for byte?

Runs the real CLI from each built preset over every committed assembly model
and compares the output. The solve line carries the status, the degrees of
freedom, the equation count, the iteration count, the largest residual and
every component's solved position -- so byte equality here is a strong claim:
Debug, Release and Debug-shared took the same number of Gauss-Newton steps and
landed on the same doubles.

    python cross-preset.py [preset ...]        (default: all three)

Exit code 0 only if every preset agrees on every model.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PRESETS = sys.argv[1:] or ["debug", "release", "debug-shared"]
MODELS = sorted((ROOT / "examples" / "models" / "reference").glob("assembly_*.bcad"))

failures = []
checks = 0


def check(ok, what, detail=""):
    global checks
    checks += 1
    if not ok:
        failures.append(what + ((": " + detail) if detail else ""))
    print(("PASS  " if ok else "FAIL  ") + what + ((" -- " + detail) if detail and not ok else ""))


def run(preset, *args):
    exe = ROOT / "build" / preset / "bin" / "bettercad-cli.exe"
    r = subprocess.run([str(exe)] + [str(a) for a in args], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return r.returncode, r.stdout


print("=== P13-QUAL-001 cross-preset agreement ===")
print("presets: " + ", ".join(PRESETS))
available = []
for p in PRESETS:
    exe = ROOT / "build" / p / "bin" / "bettercad-cli.exe"
    check(exe.is_file(), "the " + p + " CLI is built")
    if exe.is_file():
        available.append(p)
if len(available) < 2:
    print("need at least two built presets to compare")
    sys.exit(2)

base = available[0]
for model in MODELS:
    name = model.stem
    for command in ("solve", "status"):
        rc0, out0 = run(base, command, model)
        for other in available[1:]:
            rc1, out1 = run(other, command, model)
            check(rc0 == rc1, name + " " + command + ": " + base + " and " + other +
                  " return the same exit code", str(rc0) + " vs " + str(rc1))
            check(out0 == out1, name + " " + command + ": " + base + " and " + other +
                  " agree byte for byte")

print("\n=== " + str(checks - len(failures)) + "/" + str(checks) + " checks passed ===")
for f in failures:
    print("FAILED: " + f)
sys.exit(1 if failures else 0)
