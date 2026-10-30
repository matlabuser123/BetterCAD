# P19-MESH-001 qualification runbook

Operator instructions for the authorised Windows qualification machine. Every
command, path and exit code below was read out of the scripts in this directory
on 2026-10-30, not recalled. Where a script has a hazard, it is named here rather
than worked around silently.

This runbook closes no gate and ticks nothing. It tells you how to run the gate.

```text
TARGET MACHINE  the one whose checkout is C:\Users\hasib\OneDrive\Desktop\CAD
                and whose build root is C:\Users\hasib\bcdev\bc-build
TOOLCHAIN       WinLibs POSIX UCRT GCC 16.1.0 (WinGet package
                BrechtSanders.WinLibs.POSIX.UCRT), Qt 6.11.2, GUI ON
EXPECTED        5939 tests per preset; 2803 tests in each repeat selection
RUNTIME         about seven hours
```

A second machine cannot substitute. With `BETTERCAD_BUILD_GUI=OFF` the ten
`gui.*` tests and `infra.deploy.location-independent` do not exist, so the suite
selects 5928 and the repeats 2793. Those are different numbers, not tolerances.

---

## 1. Before you start

Check all six. Any one of them wrong means stop rather than launch.

```text
[ ] cd /d C:\Users\hasib\OneDrive\Desktop\CAD
[ ] git rev-parse --short HEAD            expect 35c943c or later, and
                                          git status must show the SIX frozen
                                          candidate files still uncommitted:
                                            M src/cfd/CMakeLists.txt
                                            M tests/CMakeLists.txt
                                            M tests/cli/ZeroMatchGuard.cmake
                                            ?? include/bettercad/cfd/CfdMesh.hpp
                                            ?? src/cfd/CfdMesh.cpp
                                            ?? tests/cfd/CfdMeshTests.cpp
                                          plus ADR-063 under docs/architecture/
[ ] gcc --version                         expect WinLibs 16.1.0, NOT MSYS2
[ ] at least 20 GB free on C: and on the %TEMP% volume
[ ] OneDrive: pause syncing for the duration. The checkout is inside OneDrive,
    and a sync touching a source file mid-run voids the freeze.
[ ] close editors and IDEs that may write into the tree or hold a build lock
```

Disk is the failure that killed attempt 1. `%TEMP%` filled, every build aborted
writing `cc*.s`, and five of seventeen stages failed. The preflight added since
then refuses the launch rather than letting that recur.

---

## 2. Freeze, twice

`freeze-mesh.ps1`'s own header requires two runs whose outputs are identical.
Keep `-Scratch` the same here and in every later step, or `commit-mesh.ps1`
cannot read what the freeze wrote.

```bat
cd /d C:\Users\hasib\OneDrive\Desktop\CAD\docs\verification\P19-MESH-001\qualification
powershell -NoProfile -ExecutionPolicy Bypass -File freeze-mesh.ps1 -Out freeze-mesh-pre.txt
powershell -NoProfile -ExecutionPolicy Bypass -File freeze-mesh.ps1 -Out freeze-mesh-pre2.txt
```

Both land in the durable scratch directory `C:\Users\hasib\bcdev\p19-mesh-scratch`
(the script's default; it creates it if absent). Compare them:

```bat
fc "C:\Users\hasib\bcdev\p19-mesh-scratch\freeze-mesh-pre.txt" "C:\Users\hasib\bcdev\p19-mesh-scratch\freeze-mesh-pre2.txt"
```

Require: no differences, and these twelve identities, which are the candidate.

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            aa83af4213b54690223ce805bf5fdc687a3777b0
src                499bb156787ec3162ffcc7107f463be249953608
tests              fd735ed14d90480110c10313d90200bdd331f1c0
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
P19_MESH_CFD_MESH_FINGERPRINT    822ab6ed776c842950264bd410fe3e97e1627e75
P19_MESH_P16_READ_FINGERPRINT    caa1f6e90af0b1f9825dc9444d1d03a337de32e2
P19_MESH_MESH_TESTS_FINGERPRINT  af78f3aa7aecf1e6bc853034a3ba46789e7c5190
P19_MESH_ADRS_FINGERPRINT        6cc03381c76784139abde1eb2d47ca70e739ad5a
```

The "P16 files changed against HEAD" section must be empty. The three whole-tree
lines (`HEAD`, `HEAD tree`, `scratch tree`) will NOT match this file and are not
meant to: they move with every commit. Read the twelve, never those three.

If any of the twelve differs, the candidate has moved. Stop: the freeze is
re-taken and FREEZE.md re-written before anything is qualified.

---

## 3. Launch

```bat
cd /d C:\Users\hasib\OneDrive\Desktop\CAD
call docs\verification\P19-MESH-001\qualification\launch.cmd
```

Do not edit any script in this directory while it runs. `cmd.exe` re-reads a
batch file from disk as it executes, so an edit mid-run changes the running
program.

`launch.cmd` redirects into its own directory:

```text
run-qualification.out   stdout
run-qualification.err   stderr
```

**Both are tracked files that currently hold attempt 1's record, and the launch
truncates them.** That is intended — FREEZE.md says this file is rewritten by the
re-run — but it means attempt 1's logs exist only in git history afterwards. If
you want them to hand, copy them aside first.

The launcher's last line is `launcher saw exit N`. If you never see it, the
preflight refused and ended the process. It is not a crash:

```text
exit 0   OK, every distinct volume has at least 20 GB free
exit 1   a volume is short -- the report above it names which
exit 9   QUALIFY_MIN_FREE_GB is not a positive whole number
```

`QUALIFY_MIN_FREE_GB` is set to 20 at `run-qualification.cmd` line 110. It is set
after `setlocal`, so exporting it in the shell beforehand has no effect; the line
itself carries the figure.

---

## 4. What the harness does, so you can read its output

`qualify.cmd` runs under code page 65001 (`chcp 65001`, line 39 — the CLI's
Unicode test needs it) and writes everything into this evidence directory.
Seventeen stages: five per preset across three presets, plus two repeat stages.

```text
per preset, in QUALIFY_PRESETS order (debug-ext, release-ext, debug-shared-ext):
  configure      cmake --preset P            -> configure-P.log
  clean          --target clean, up to 5     -> clean-P.log
                 attempts, 15 s apart
  build          cmake --build --preset P    -> build-P.log
  no-op rebuild  the same command again      -> rebuild-P.log
  ctest          ctest --preset P -j 8       -> ctest-P.log
per repeat preset (release-ext, debug-ext):
  repeat         ctest -R <filter>           -> ctest-repeat-P.log
                 --repeat until-fail:5
always:
  tree capture   before the first build and  -> qualification-times.txt
                 after the last test run        (also every stage's timestamps)
```

A failed configure skips that preset's build and ctest; a failed build skips its
ctest. Each skip still counts as a failure.

**The exit code is the number of failed stages**, not a status. `exit /b 0` means
every stage exited 0. Attempt 1 exited 8.

---

## 5. The checks the harness does not make

These are yours. The harness will report success without them.

### 5.1 The absolute test count

Require `out of 5939` in each of the three `ctest-*.log` files.

```bat
findstr /c:"tests passed" docs\verification\P19-MESH-001\qualification\ctest-debug-ext.log
findstr /c:"tests passed" docs\verification\P19-MESH-001\qualification\ctest-release-ext.log
findstr /c:"tests passed" docs\verification\P19-MESH-001\qualification\ctest-debug-shared-ext.log
```

Expect exactly `100% tests passed out of 5939` three times. Note the string:
ctest prints no "0 tests failed" clause when everything passes — a failure reads
`99% tests passed, 1 tests failed out of 5939` instead, so the presence of the
word `failed` on that line is itself the failure signal. 5939 is 5919 before this
milestone plus its 20 new tests.

### 5.2 The repeat selection — the check attempt 1 slipped through

`qualify.cmd` only refuses a selection of **zero**. Attempt 1's repeat stages
selected **352** and passed that guard: with no test binary built, `ctest -N`
still matched the 352 `architecture.*` checks, which are CMake script tests and
need no executable. A Determinism stage can therefore exit 0 with not one CFD
test in it.

Require `out of 2803` in both repeat logs:

```bat
findstr /c:"tests passed" docs\verification\P19-MESH-001\qualification\ctest-repeat-release-ext.log
findstr /c:"tests passed" docs\verification\P19-MESH-001\qualification\ctest-repeat-debug-ext.log
findstr /c:"selects" docs\verification\P19-MESH-001\qualification\qualification-times.txt
```

2803 is P19-FLUID-001's 2783 under a byte-identical filter plus this milestone's
20, every one of which matches the filter's `unit\.Cfd` term. Anything smaller
means the binary was missing or the filter moved, and the stage measured nothing.

Each test runs five times, so the log holds `2803 x 5 = 14015` `Passed` lines.
Counting them is the strongest single check that the repeats really repeated.

### 5.3 Warnings

Warnings-as-errors is on, so a warning should already have failed the build.
Confirm anyway, over all three build logs:

```bat
findstr /c:"warning:" docs\verification\P19-MESH-001\qualification\build-*.log
```

Expect no output.

### 5.4 Fingerprints before and after

`qualification-times.txt` carries a tree capture from before the first build and
after the last test run. The two blocks must be identical, and must match the
eight subtree hashes in section 2. If they differ, something wrote into the
source tree during the run — most likely OneDrive or an editor.

---

## 6. Postrun

```bat
cd /d C:\Users\hasib\OneDrive\Desktop\CAD\docs\verification\P19-MESH-001\qualification
powershell -NoProfile -ExecutionPolicy Bypass -File postrun-mesh.ps1 -CopyEvidence > postrun-summary.txt 2>&1
```

It prepends the WinLibs `mingw64\bin` to `PATH`, runs the layering and link-graph
checkers, then four sections: FRESH BINARY (each preset's binaries newer than
that preset's clean and newer than the newest frozen source file), ZERO-MATCH
TABLE (expected / discovered / executed-and-passed per guard group per preset),
THE CFD DATA (the boundary cases on each preset's own test executable) and
CROSS-PRESET. It exits with the number of failures it recorded, so require
**exit 0**, and read the summary for any `*** FAIL` line.

Two hazards in this script. Neither is fixed, deliberately — it is harness code
and changing it is a separate decision.

```text
$out is a DEAD SESSION PATH. Line 7 hardcodes
  C:\Users\hasib\AppData\Local\Temp\claude\C--Users-hasib-Desktop-CAD\
  31d55056-e746-4b0d-938c-e551fb84138c\scratchpad\postrun-mesh
which is an agent session's scratchpad under a session GUID that no longer
exists. `New-Item -Force` recreates it, so the script does not fail -- it just
writes its working files somewhere nobody will look. This is the same defect
FREEZE.md records as fixed for freeze-mesh.ps1 and commit-mesh.ps1; postrun
never got the fix.

-CopyEvidence DOES NOT COPY THE SUMMARY. It copies only cfdmesh-<preset>.txt
and cfdmesh-census-<preset>.txt into this directory. The summary -- which holds
the FRESH BINARY verdict, the zero-match table and every *** FAIL line -- is
only echoed to stdout by the script's last line. Hence the `> postrun-summary.txt`
redirect above. Without it the verification output is lost to the dead path.
```

`$root` and `$src` are hardcoded too, but to this machine's correct paths, so
they are right here and only here. This script cannot be rehearsed elsewhere.

Then freeze once more and require it equal to the pre-run freeze on the twelve:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File freeze-mesh.ps1 -Out freeze-mesh-post.txt
fc "C:\Users\hasib\bcdev\p19-mesh-scratch\freeze-mesh-pre.txt" "C:\Users\hasib\bcdev\p19-mesh-scratch\freeze-mesh-post.txt"
```

---

## 7. Evidence, then the ticks

Write the result into this milestone's existing evidence structure: FREEZE.md
rewritten by the re-run as its closing line says, and README.md updated with the
measured figures. Record what was run, what was measured, and the result, with
the logs the claims cite.

Only with every one of these true do the three boxes in `TODO.md` become `[x]`:

```text
[ ] all 17 stages exited 0, so launch.cmd reported exit 0
[ ] 5939 / 5939 in each of the three ctest logs
[ ] 2803 selected and 14015 Passed lines in each of the two repeat logs
[ ] no `warning:` line in any build log
[ ] the before and after tree captures identical, and equal to the twelve
[ ] postrun-mesh.ps1 exit 0 with no *** FAIL in its summary
[ ] the post-run freeze equal to the pre-run freeze
```

Any one false: leave that box `[ ]` and write down which check failed and its
measured value. A partial run is not a partial pass.

---

## 8. Commit

`commit-mesh.ps1` lands the production code with its evidence. It is the only
commit permitted to change a mark. Clear its two prerequisites first, or it
refuses — correctly — after the seven hours.

```text
1. The commit message. Write the real message to
     C:\Users\hasib\bcdev\p19-mesh-scratch\commit-message-mesh.txt
   Same -Scratch as the freeze. UTF-8 WITHOUT a BOM. Not empty, not whitespace.
   Write it AFTER the run, from the measured figures: a message claiming
   verification the evidence does not contain is the same failure as a false
   checkbox.

2. The three marker lines. README.md lines 149, 165 and 183 each contain the
   all-caps marker word the guard scans for (`-CaseSensitive`). Rewrite those
   three lines with the true post-run state. Line 183 needs rewording rather
   than filling in. Confirm the scan returns nothing before you run the commit:

     powershell -NoProfile -Command "Select-String -CaseSensitive -Path 'docs\verification\P19-MESH-001\*.md' -Pattern ('PLACE'+'HOLDER')"
```

Then:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File commit-mesh.ps1
```

Its refusals, all before the index is touched:

```text
exit 2   fingerprint mismatch against the freeze file
exit 3   the commit message has a UTF-8 BOM
exit 4   marker lines remain -- it prints each file and line number
exit 5   the freeze file does not carry eight hashes
exit 6   no freeze file at the scratch path (run freeze-mesh.ps1 first,
         with the SAME -Scratch)
exit 7   no commit message file
exit 8   the commit message is empty or whitespace only
```

Do not bypass the guard, and do not stage by hand. If it refuses, fix what it
names and run it again.

---

## 9. If the run fails

```text
stop -> diagnose -> fix -> re-run from clean -> continue only on PASS
```

Never continue past a failed stage and never relax a gate to obtain a pass.
Distinguish the two causes, because they are handled differently:

```text
ENVIRONMENTAL   disk, locks, OneDrive, a missing toolchain. Attempt 1 was this.
                Fix the environment and re-run from clean. The candidate is
                untouched and needs no new freeze.
CODE            a test fails on its merits. Then the milestone is not complete:
                diagnose, write a regression test, fix, and the freeze is
                re-taken because the source moved.
```

If source or tests change at any point after the freeze, the qualification is
void and runs again from clean. Documentation under `docs/` cannot affect the
executable or the tests and does not void it.

Nothing under `docs/verification/` may be deleted, including attempt 1's logs.
P19-TOPO-001 is NOT AUTHORIZED: finishing this milestone is a stop condition.
