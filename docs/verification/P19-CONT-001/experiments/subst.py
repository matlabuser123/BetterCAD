"""Apply or revert ONE mutation probe. Text only -- this never builds anything.

THE WHOLE POINT OF THE SPLIT. Two earlier harnesses drove the build from Python
and both failed for environment reasons: this interpreter is an MSYS2 python
whose os.environ carries POSIX-converted PATH and LOCALAPPDATA, and handing that
to CMake made MSYS2 a find_package prefix candidate (poisoning CMakeCache.txt
with Open CASCADE 7.9.3) and then produced a CMAKE_PREFIX_PATH Windows could not
resolve. The builds are now done by a .cmd driver launched the way every build
in this session has been launched successfully. Python does nothing but edit
text and report.

Usage:
    subst.py save                 copy both files to .pristine, print hashes
    subst.py apply <n>            apply probe n; exit 0 ok, 3 = anchor not unique
    subst.py revert               restore both files; exit 4 if a hash differs
    subst.py meta <n>             print "<id>|<target>|<expect>"
    subst.py count                print the number of probes
"""

import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = r"C:\Users\Hasib\Desktop\CAD"
FILES = {
    "S": os.path.join(ROOT, "src", "cfd", "CfdContinuity.cpp"),
    "H": os.path.join(ROOT, "include", "bettercad", "cfd", "CfdContinuity.hpp"),
}
BACKUPS = {k: os.path.join(HERE, os.path.basename(v) + ".pristine") for k, v in FILES.items()}
PROBES = json.load(open(os.path.join(HERE, "probes.json"), encoding="utf-8"))


def sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    cmd = argv[0]

    if cmd == "count":
        print(len(PROBES))
        return 0

    if cmd == "save":
        for k, path in FILES.items():
            shutil.copy2(path, BACKUPS[k])
            print("%s %s" % (k, sha(path)))
        return 0

    if cmd == "meta":
        p = PROBES[int(argv[1])]
        print("%s|%s|%s" % (p["id"], p["target"], p["expect"]))
        return 0

    if cmd == "revert":
        bad = 0
        for k, path in FILES.items():
            shutil.copy2(BACKUPS[k], path)
            if sha(path) != sha(BACKUPS[k]):
                print("REVERT FAILED %s" % k)
                bad = 4
        return bad

    if cmd == "apply":
        p = PROBES[int(argv[1])]
        text = open(BACKUPS[p["target"]], encoding="utf-8").read()
        n = text.count(p["old"])
        if n != 1:
            print("ANCHOR x%d" % n)
            return 3
        open(FILES[p["target"]], "w", encoding="utf-8", newline="").write(
            text.replace(p["old"], p["new"], 1))
        return 0

    print("unknown command %s" % cmd)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
