"""Runs mutations M3..M12, appending verdicts to results.txt.

WHY A SEPARATE DRIVER. The first run of mutate.sh was orphaned when its
session ended: it died without its EXIT trap firing, which left mutation M3
APPLIED to src/io/json/MeshControlJson.cpp -- a deliberate unit-scale defect
-- and a stale lock whose pid no longer existed. Nothing in git flagged it,
because the file is new and therefore untracked, so `git status` showed only
`??` and `git diff` showed nothing at all.

M1 and M2 had already produced verdicts and are kept: each mutation is applied
to a pristine snapshot and judged independently, so their results do not
depend on the rest of the loop. M3 had no verdict -- its run.log holds only a
header -- so it is re-run here with the rest.

Run DETACHED (PowerShell Start-Process) so that a session restart cannot
orphan it a second time, and check the tree against pristine/ afterwards
regardless of how it ends.
"""
import io
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).parent
ROOT = HERE.parents[4]
BUILD = "C:/Users/uqhas/AppData/Local/bc-build/debug-ext"
EXE = BUILD + "/bin/bettercad_tests.exe"
FILTER = "[persist]"
FIRST = 10  # M11, zero-based
LAST = 10   # M11

if (HERE / "mutate.lock").exists():
    sys.exit("REFUSED: a lock is present; check its pid before removing it.")

mutations = json.loads((HERE / "mutations.json").read_text(encoding="utf-8"))


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, cwd=str(ROOT))


print(run("python", str(HERE / "apply.py"), "snapshot").stdout.strip(), flush=True)
(HERE / "mutate.lock").write_text("driver", encoding="utf-8")

try:
    with io.open(HERE / "results.txt", "a", encoding="utf-8", newline="\n") as results:
        results.write("---- re-run of M11 alone, after its first form was killed only by the compiler ----\n")
        for index in range(FIRST, LAST + 1):
            label = mutations[index]["label"]
            applied = run("python", str(HERE / "apply.py"), str(index))
            state = applied.stdout.strip()
            if state != "applied":
                verdict = "NOT APPLIED (%s)" % state
            else:
                built = run("cmake", "--build", BUILD, "--target", "bettercad_tests")
                if built.returncode != 0:
                    verdict = "killed by the COMPILER"
                else:
                    tested = run(EXE, FILTER, "-r", "compact")
                    summary = ""
                    for line in tested.stdout.splitlines():
                        if "assertions:" in line:
                            summary = line.strip()
                    verdict = ("*** SURVIVED *** (%s)" % summary) if tested.returncode == 0 \
                        else "killed (%s)" % summary
            results.write("%-78s %s\n" % (label, verdict))
            results.flush()
            print("%s -> %s" % (label, verdict), flush=True)
finally:
    print(run("python", str(HERE / "apply.py"), "restore").stdout.strip(), flush=True)
    rebuilt = run("cmake", "--build", BUILD, "--target", "bettercad_tests")
    print("restore build:", "ok" if rebuilt.returncode == 0 else "FAILED", flush=True)
    (HERE / "mutate.lock").unlink(missing_ok=True)
    print("DRIVER FINISHED", flush=True)
