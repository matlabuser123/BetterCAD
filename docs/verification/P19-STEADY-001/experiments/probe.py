"""P19-STEADY-001 mutation probe driver (carried from P19-LINSOLVE-001).

Each probe replaces ONE piece of text in one production file, rebuilds
`debug-ext`, runs the suites that ought to catch it, reverts, and re-hashes
every file it could have touched. The run stops dead if a revert does not
restore a file exactly, because a probe table produced on a drifting tree is
worthless.

    KILLED    a suite failed -- what a behavioural probe must do
    COMPILER  rejected at compile time -- STRONGER than a test kill
    SURVIVED  every suite passed; correct ONLY for the no-op control and for a
              documented-unreachable branch

WHY A PYTHON DRIVER AND NOT `cmd.exe`. `P19-PVCOUPLE-001` launched its driver
twice through `cmd /c` from Git Bash, had the `/c` path-converted into a drive
root, started an interactive shell, wrote nothing and EXITED 0 both times. A
probe table from a driver that never ran reads exactly like strong evidence,
which is why the no-op control is not optional.
"""

import hashlib
import io
import json
import os
import subprocess
import sys
import time

REPO = r"C:/Users/Hasib/Desktop/CAD"
BUILD = r"C:/bettercad/bc-build/debug-ext"
BIN = BUILD + "/bin/bettercad_tests.exe"
WINLIBS = (r"C:/Users/Hasib/AppData/Local/Microsoft/WinGet/Packages/"
           r"BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin")
DEPS = r"C:/Users/Hasib/AppData/Local/bettercad-deps/gnu-16-mingw-amd64/bin"
HERE = os.path.dirname(os.path.abspath(__file__))

# Round two re-probes the two survivors, the probe whose text stopped round one,
# and three mutations of the R-diagonal scan that round one predated.
PROBES = os.environ.get("PROBE_SET", "probes.json")
OUT = os.environ.get("PROBE_OUT", "mutations.out")

# Every file any probe may touch: hashed before and after each probe.
WATCHED = [
    "src/cfd/CfdSteady.cpp",
    "src/cfd/CfdResult.cpp",
    "include/bettercad/cfd/CfdSteady.hpp",
    "include/bettercad/cfd/CfdResult.hpp",
]

# The suites that ought to catch a mutation in those files. The full suite is not
# run per probe -- it is forty minutes -- and these are the ones whose failure
# would be the signal: the steady suite for the orchestrator, and the data suite
# because CfdResult.cpp is P19-DATA-001's type and its own tests are there.
FILTERS = ["[cfd][steady]", "[cfd][data]"]


def env():
    e = dict(os.environ)
    e["BETTERCAD_BUILD_ROOT"] = r"C:/bettercad/bc-build"
    e["CMAKE_BUILD_PARALLEL_LEVEL"] = "4"
    e["PATH"] = WINLIBS.replace("/", "\\") + ";" + DEPS.replace("/", "\\") + ";" + e.get("PATH", "")
    return e


def sha(path):
    return hashlib.sha256(io.open(os.path.join(REPO, path), "rb").read()).hexdigest()


def snapshot():
    return {p: sha(p) for p in WATCHED}


def substitute(path, old, new):
    full = os.path.join(REPO, path)
    s = io.open(full, encoding="utf-8", newline="").read()
    nl = "\r\n" if s.count("\r\n") > s.count("\n") - s.count("\r\n") else "\n"
    o = old.replace("\n", nl)
    if s.count(o) != 1:
        return None, "the probe text occurs %d times, not once" % s.count(o)
    io.open(full, "w", encoding="utf-8", newline="").write(s.replace(o, new.replace("\n", nl)))
    return s, None


def restore(path, original):
    io.open(os.path.join(REPO, path), "w", encoding="utf-8", newline="").write(original)


def build():
    r = subprocess.run(["cmake", "--build", BUILD, "--target", "bettercad_tests"],
                       cwd=REPO, env=env(), capture_output=True, text=True)
    return r.returncode == 0, (r.stdout or "") + (r.stderr or "")


def suites():
    """True when every filter passes."""
    for f in FILTERS:
        r = subprocess.run([BIN, f], cwd=BUILD, env=env(), capture_output=True, text=True)
        if r.returncode != 0:
            return False, "%s failed" % f
    return True, "all filters passed"


def main():
    probes = json.load(io.open(os.path.join(HERE, PROBES), encoding="utf-8"))
    out = io.open(os.path.join(HERE, OUT), "w", encoding="utf-8", newline="\n")

    def say(line):
        print(line)
        out.write(line + "\n")
        out.flush()

    say("P19-STEADY-001 mutation probes")
    say("")
    pristine = snapshot()
    for p in WATCHED:
        say("%-44s %s" % (p, pristine[p]))
    say("")

    ok, note = suites()
    if not ok:
        say("BASELINE FAILED (%s) -- refusing to probe a red tree" % note)
        return 1
    say("BASELINE all filters pass")
    say("")

    counts = {"KILLED": 0, "COMPILER": 0, "SURVIVED": 0}
    for probe in probes:
        original, err = substitute(probe["file"], probe["old"], probe["new"])
        if err is not None:
            say("ERROR  %-70s %s" % (probe["id"], err))
            out.close()
            return 1
        started = time.time()
        built, log = build()
        if not built:
            verdict = "COMPILER"
        else:
            passed, _ = suites()
            verdict = "SURVIVED" if passed else "KILLED"
        restore(probe["file"], original)
        now = snapshot()
        if now != pristine:
            say("REVERT FAILED after %s -- stopping" % probe["id"])
            out.close()
            return 1
        counts[verdict] += 1
        say("%-8s %-8s %-68s %5.0fs" % (probe["expect"], verdict, probe["id"], time.time() - started))

    say("")
    # Put the tree back to a built state, so the next thing to run is not testing
    # a binary built from a mutated source.
    built, _ = build()
    say("rebuilt from pristine: %s" % ("ok" if built else "FAILED"))
    ok, note = suites()
    say("FINAL all filters pass: %s" % ok)
    for p in WATCHED:
        say("%-44s %s %s" % (p, snapshot()[p], "MATCH" if snapshot()[p] == pristine[p] else "DIFFERS"))
    say("")
    say("killed %d, compiler %d, survived %d, of %d probes"
        % (counts["KILLED"], counts["COMPILER"], counts["SURVIVED"], len(probes)))
    out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
