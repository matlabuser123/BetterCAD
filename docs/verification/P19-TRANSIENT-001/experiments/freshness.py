"""P19-TRANSIENT-001 -- was anything in the build graph touched after the build?

NO BUILD IS RUN HERE. `ninja -t targets all` is a GRAPH QUERY: it lists the
graph's outputs and runs no edge. `ninja -n` would be wrong for a different
reason -- a dry run cannot restat, so it PLANS edges it would not actually need.

Carried from P19-STEADY-001's experiments/freshness.py with the evidence path
changed. The reference time is each build log's own mtime, which the harness
wrote when the build stage exited.
"""

import os
import subprocess
import sys

NINJA = (r"C:/Users/Hasib/AppData/Local/Microsoft/WinGet/Packages/"
         r"BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/"
         r"mingw64/bin/ninja.exe")
Q = r"C:/Users/Hasib/Desktop/CAD/docs/verification/P19-TRANSIENT-001/qualification/"
PRESETS = ["debug-ext", "release-ext", "debug-shared-ext"]

print("NO BUILD IS RUN HERE. `ninja -t targets all` is a GRAPH QUERY.")
print()

bad = 0
for p in PRESETS:
    d = "C:/bettercad/bc-build/" + p
    ref = os.path.getmtime(Q + "build-%s.log" % p)
    r = subprocess.run([NINJA, "-C", d, "-t", "targets", "all"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("  %s  ninja query FAILED rc=%d" % (p, r.returncode))
        print(r.stderr[:500])
        sys.exit(1)
    outs = []
    for ln in r.stdout.splitlines():
        if ":" in ln:
            outs.append(ln.rsplit(":", 1)[0].strip())
    existing = 0
    newer = []
    for o in outs:
        fp = o if os.path.isabs(o) else os.path.join(d, o)
        try:
            m = os.path.getmtime(fp)
        except OSError:
            continue
        existing += 1
        if m > ref:
            newer.append((o, m - ref))
    exe = os.path.join(d, "bin", "bettercad_tests.exe")
    print("  %s" % p)
    print("    graph outputs listed            %d" % len(outs))
    print("    of those, existing on disk      %d" % existing)
    print("    NEWER than the build exit       %d" % len(newer))
    print("    test binary vs the build exit   %+.2f s"
          % (os.path.getmtime(exe) - ref))
    if newer:
        bad += len(newer)
        newer.sort(key=lambda t: -t[1])
        for o, dt in newer[:12]:
            print("        +%8.1f s  %s" % (dt, o))
        if len(newer) > 12:
            print("        ... and %d more" % (len(newer) - 12))
    print()

if bad:
    print("FRESHNESS FAILED: %d graph output(s) newer than their build" % bad)
    sys.exit(1)
print("FRESHNESS PASSED: no graph output in any preset is newer than its build")
sys.exit(0)
