#!/usr/bin/env bash
# The exact invocation of this milestone's mutation run, recorded so it is
# reproducible rather than described.
#
# mutate.sh and apply.py are carried BYTE-IDENTICAL from INFRA-VIEWER-001
# (git hash-object compares equal), which is why the one thing that had to
# change is here instead of in them: mutate.sh calls `python`, and on this
# machine PATH sometimes resolves that to the Windows Store alias -- a stub
# that prints "Python was not found" and exits 0. The whole run then completes
# in two seconds with an empty results.txt and no error, which is exactly the
# kind of silent nothing that could be mistaken for a pass.
#
# Prepending the real interpreter's directory removes the dependency on PATH
# order without touching the carried harness.
set -u
export PATH=/c/msys64/ucrt64/bin:$PATH
export BETTERCAD_BUILD_ROOT=${BETTERCAD_BUILD_ROOT:-C:/Users/uqhas/AppData/Local/bc-build}
exec bash "$(dirname "$0")/mutation/mutate.sh"
