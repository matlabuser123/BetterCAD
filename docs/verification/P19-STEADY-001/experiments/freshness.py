import os
import subprocess
import sys

NINJA = (r"C:/Users/Hasib/AppData/Local/Microsoft/WinGet/Packages/"
         r"BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/"
         r"mingw64/bin/ninja.exe")
Q = r"C:/Users/Hasib/Desktop/CAD/docs/verification/P19-STEADY-001/qualification/"
PRESETS = ["debug-ext", "release-ext", "debug-shared-ext"]

print("NO BUILD IS RUN HERE. `ninja -t targets all` is a GRAPH QUERY: it lists")
print("the graph's outputs and runs no edge. `ninja -n` would be wrong for a")
print("different reason -- a dry run cannot restat, so it PLANS edges it would")
print("not actually need (8 of them, measured on P19-LINSOLVE-001).")
print()

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
    print("  %s" % p)
    print("    graph outputs listed            %d" % len(outs))
    print("    of those, existing on disk      %d" % existing)
    print("    NEWER than the build exit       %d" % len(newer))
    if newer:
        newer.sort(key=lambda t: -t[1])
        for o, dt in newer[:12]:
            print("        +%8.1f s  %s" % (dt, o))
        if len(newer) > 12:
            print("        ... and %d more" % (len(newer) - 12))
    print()
