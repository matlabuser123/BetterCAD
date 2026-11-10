"""Does reconcile.py actually bite? Three single-line sabotages, three runs.

P19-STEADY-001 recorded a reconciliation check that reported PASS off one parsed
path instead of eight: a comparison that cannot fail is not a comparison. This
script answers the same question for reconcile.py by breaking one thing at a
time in a COPY of the evidence and requiring the run to fail.

It touches nothing in the real evidence directory: everything happens under a
scratch copy, and reconcile.py is re-pointed at it by text substitution.
"""

import io
import os
import re
import shutil
import subprocess
import sys

REAL = r"C:/Users/Hasib/Desktop/CAD/docs/verification/P19-TRANSIENT-001"
SCRATCH = (r"C:/Users/Hasib/AppData/Local/Temp/claude/8b17a8f29dfea518/"
           r"527dbd1e-d40d-42f7-8715-a186b5519185/scratchpad/falsify")
PY = sys.executable

NEEDED = ["FREEZE.md"]
QFILES = (["run-qualification.out", "qualification-times.txt", "binary-readout.txt"]
          + ["build-%s.log" % p for p in ("debug-ext", "release-ext", "debug-shared-ext")]
          + ["rebuild-%s.log" % p for p in ("debug-ext", "release-ext", "debug-shared-ext")]
          + ["ctest-%s.log" % p for p in ("debug-ext", "release-ext", "debug-shared-ext")]
          + ["ctest-repeat-%s.log" % p for p in ("release-ext", "debug-ext")])


def rd(p):
    with io.open(p, encoding="utf-8", errors="replace", newline="") as f:
        return f.read()


def wr(p, s):
    with io.open(p, "w", encoding="utf-8", errors="replace", newline="") as f:
        f.write(s)


def build_scratch():
    if os.path.isdir(SCRATCH):
        shutil.rmtree(SCRATCH)
    os.makedirs(SCRATCH + "/qualification")
    os.makedirs(SCRATCH + "/experiments")
    for f in NEEDED:
        shutil.copy2(REAL + "/" + f, SCRATCH + "/" + f)
    for f in QFILES:
        shutil.copy2(REAL + "/qualification/" + f, SCRATCH + "/qualification/" + f)
    src = rd(REAL + "/experiments/reconcile.py")
    assert src.count('ROOT = r"') == 1
    src = re.sub(r'ROOT = r"[^"]+"', 'ROOT = r"%s"' % SCRATCH, src, count=1)
    wr(SCRATCH + "/experiments/reconcile.py", src)


def run():
    r = subprocess.run([PY, "-I", SCRATCH + "/experiments/reconcile.py"],
                       capture_output=True, text=True)
    fails = [ln.strip() for ln in r.stdout.splitlines() if ln.strip().startswith("FAIL")]
    return r.returncode, fails


print("== control: the untouched copy must PASS ==")
build_scratch()
rc, fails = run()
print("   exit %d, %d FAIL line(s)" % (rc, len(fails)))
control_ok = (rc == 0 and not fails)
print("   %s" % ("OK -- the copy reproduces the real run's PASS" if control_ok
                 else "BROKEN -- the scratch copy does not even pass; the"
                      " sabotages below would prove nothing"))
if not control_ok:
    for f in fails[:6]:
        print("      " + f)
    sys.exit(1)

results = []

SABOTAGES = [
    ("one '... Passed' line removed from ctest-debug-ext.log",
     "qualification/ctest-debug-ext.log",
     lambda s: s.replace("...   Passed", "...   PASSED", 1)),
    ("one identity hex altered in the POST-RUN block",
     "qualification/qualification-times.txt",
     None),
    ("one assertion figure altered in binary-readout.txt",
     "qualification/binary-readout.txt",
     lambda s: s.replace("1813 assertions in 34 test cases",
                         "1812 assertions in 34 test cases", 1)),
    ("the verdict sentence removed from run-qualification.out",
     "qualification/run-qualification.out",
     lambda s: s.replace("Qualification passed: every stage exited 0.",
                         "Qualification finished.", 1)),
    ("one stage's exit code changed from 0 to 1",
     "qualification/qualification-times.txt",
     lambda s: s.replace("debug-ext ctest exit 0", "debug-ext ctest exit 1", 1)),
]


def sabotage_identity(s):
    # change ONE hex in the "after the last test run" block only, so the
    # pre-build reading still matches FREEZE.md and only pre == post breaks
    head, tail = s.split("qualified source trees after the last test run:", 1)
    tail = tail.replace("fc398a3d2e524178cf38e8b667236379d8381086",
                        "fc398a3d2e524178cf38e8b667236379d8381087", 1)
    return head + "qualified source trees after the last test run:" + tail


SABOTAGES[1] = (SABOTAGES[1][0], SABOTAGES[1][1], sabotage_identity)

for label, rel, fn in SABOTAGES:
    print()
    print("== sabotage: %s ==" % label)
    build_scratch()
    p = SCRATCH + "/" + rel
    before = rd(p)
    after = fn(before)
    if after == before:
        print("   SABOTAGE DID NOT APPLY -- the search text was not found")
        results.append((label, False, "not applied"))
        continue
    wr(p, after)
    rc, fails = run()
    bit = (rc != 0 and len(fails) > 0)
    print("   exit %d, %d FAIL line(s)" % (rc, len(fails)))
    for f in fails[:4]:
        print("      " + f)
    print("   %s" % ("OK -- reconcile.py caught it" if bit
                     else "DID NOT BITE -- this check cannot fail"))
    results.append((label, bit, "%d fail line(s)" % len(fails)))

print()
print("=" * 70)
bad = [r for r in results if not r[1]]
for label, bit, note in results:
    print("  %-5s %s   (%s)" % ("CAUGHT" if bit else "MISSED", label, note))
print()
if bad:
    print("FALSIFICATION FAILED: %d sabotage(s) went unnoticed" % len(bad))
    sys.exit(1)
print("FALSIFICATION PASSED: every sabotage was caught, so the checks can fail")
shutil.rmtree(SCRATCH, ignore_errors=True)
sys.exit(0)
