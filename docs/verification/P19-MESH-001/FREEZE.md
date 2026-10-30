# P19-MESH-001 — the qualification

```text
RESULT:    PASS, by attempt 2 of 2026-10-31 on the replacement machine: 17 of
           17 stages exit 0, 5939 per preset, 2803 x 5 in two, 0 warnings, and
           the twelve identities unchanged before and after. ATTEMPT 1 of
           2026-10-28 FAILED for an environmental reason; everything this file
           records about it stands as history and is NOT rewritten. The machine
           change: ENVIRONMENT-MIGRATION.md. Attempt 2: the Fifteenth issue.
CANDIDATE: b1f151cc648a2b80ef82b1c5beb8321fdf507397  (P19-FLUID-001's record) plus the
           working tree: cfd's mesh contract (CfdMesh.hpp, CfdMesh.cpp, the cfd
           CMake), its tests, the two test registrations, ADR-063 and this directory
RAN:       Wed 28/10/2026 19:47:09 -> 20:06:44  =  19m35s, attempt 1
STAGES:    17 planned; 5 failed; qualify.cmd exit 5,
           "QUALIFICATION FAILED: 5 stage(s) failed."
HARNESS:   qualify.cmd d313a640, verify-harness.cmd 11855b8f, launch.cmd f11039e6,
           all byte-identical to P19-FLUID-001's (re-hashed 2026-10-29);
           verify-harness.out: PASS
MACHINE:   the hasib machine (GCC 16.1.0 WinLibs POSIX UCRT, CMake 4.4.1, Ninja,
           the deps prefix of deps/CMakeLists.txt)
```

## What happened

Every build stage died with the same operating-system error, and nothing after a
build could run:

```text
fatal error: error writing to C:\Users\hasib\AppData\Local\Temp\cc....s:
             No space left on device
```

The compiler's temporary assembler output could not be written: the volume holding
`%TEMP%` on the qualification machine was full. The run was launched without the
free-space check that the OneDrive restart ritual in `run-qualification.cmd`
covers for commit charge but not for disk. No source file was involved: the
translation units that failed (`src/features/rib/RibRegeneration.cpp`, four files
in `src/io/json/`, `src/structural/StructuralLoad.cpp`) are qualified P11–P17
code that this milestone does not touch, and the error is an I/O failure, not a
diagnostic.

| stage | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| configure | exit 0, 10 s | exit 0, 5 s | exit 0, 4 s |
| clean | exit 0, attempt 1 | exit 0, attempt 1 | exit 0, attempt 1 |
| build | **exit 1** at step 131 of 780 (127 objects compiled) | **exit 1** at step 341 of 780 (330 objects) | **exit 1** at step 188 of 780 (181 objects) |
| `warning:` before the abort | 0 | 0 | 0 |
| `No space left on device` | 1 | 4 | 1 |
| no-op rebuild | not reached | not reached | not reached |
| ctest | **skipped: the build failed** | **skipped: the build failed** | **skipped: the build failed** |

```text
repeat release-ext   352 selected   exit 8   205 passed, 147 failed   34.06 s
repeat debug-ext     352 selected   exit 8   205 passed, 147 failed   38.35 s
```

The two repeat stages ran against build trees the clean had just emptied: every
failure is "exit code: no such file or directory" (the CLI and the test executable
did not exist), and the fixture-dependent `refmod.*` cases were Not Run. Those
numbers measure nothing about the code and are not a determinism result.

**Files the harness did not produce**, because the stages that write them never
ran: `rebuild-*.log`, `ctest-*.log` (the three full suites), `postrun-summary.txt`
and the `cfdmesh-*.txt` per-preset outputs. `freeze-mesh.ps1`'s outputs
(`freeze-mesh-pre.txt`, `-pre2.txt`, `-post.txt`) were written to the Windows
scratchpad and were not copied here; the re-run must copy them, as P19-FLUID-001 did.

## The tree that was frozen

The harness took the eight-path fingerprint before the first build and after the
last test run, and they are identical:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086   == HEAD (b1f151c)
include            aa83af4213b54690223ce805bf5fdc687a3777b0   MOVED (cfd/CfdMesh.hpp added)
src                499bb156787ec3162ffcc7107f463be249953608   MOVED (cfd/CfdMesh.cpp, cfd CMake)
tests              fd735ed14d90480110c10313d90200bdd331f1c0   MOVED (cfd/CfdMeshTests.cpp, the
                                                              registration, the guard)
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   == HEAD
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == HEAD
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == HEAD
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == HEAD
```

Recomputed on 2026-10-29 from the same working tree on a second machine (Linux,
a scratch index, `git add -A` over the eight paths, `git write-tree`): all eight
hashes equal the frozen ones; scratch tree `909a0e233d7df026f84eb72852b5ae8654706ef2`.
`git status` over `include/bettercad/meshing`, `src/meshing`, `structural`,
`thermal`, `core` and `apps` is empty: no P16, P17 or P18 production file changed.

Because the attempt failed, **this fingerprint is the identity of the candidate,
not of a qualified tree.** If any source or test file changes before the re-run,
the re-run freezes afresh; if none does, the re-run must reproduce these eight
hashes before and after.

**HEAD has moved since the freeze, and the eight hashes have not.** `b474ac8`
(2026-10-29) corrected the P19 status drift in `TODO.md` and `ROADMAP.md` and
changed nothing else; it is pushed, and `origin/main` is now `b474ac8`. Neither
file is one of the eight paths, so every hash in the table above is unchanged and
each `== HEAD` annotation still holds against `b474ac8`. THREE lines of
`freeze-mesh.ps1`'s output will nevertheless differ from this file when the re-run
prints them, because all three are computed over every path rather than the eight:

```text
HEAD           b1f151c  ->  b474ac8  ->  7b22d98
HEAD tree      2716eed5 ->  c44b0fc7 ->  1c967443
scratch tree   909a0e23 ->  399d006c ->  995eacfa
```

Read those three as documentation commits, **not** as a moved candidate. The test
that governs is the one stated just above -- the eight subtree hashes, and the
four milestone fingerprints below -- and all twelve were re-confirmed against
`b474ac8` on 2026-10-29 and again against `7b22d98` on 2026-10-30. No source or
test file changed, so the re-run does not freeze afresh.

Two notes on that table, both corrections. `HEAD tree` is the third moving line
and was missing from it, so the earlier "Two lines" undercounted: a re-run
operator comparing output against this file line by line would have found an
unexplained third difference. And the `-> 7b22d98` column is the evidence commit
that first put `docs/verification/P19-MESH-001/` under version control, including
the harness scripts, which until then existed only in a working tree.

The `scratch tree` line is the one worth understanding, because it is not a
property of HEAD alone. `freeze-mesh.ps1` seeds a scratch index with
`git read-tree HEAD`, overlays the eight paths from the WORKING TREE, and writes
that index back. So `995eacfa` identifies `7b22d98` **plus the uncommitted frozen
candidate** -- which is precisely the tree the re-run would qualify, and the
reason the value is worth recording at all. It moves when HEAD moves and when the
candidate moves, and only the eight subtree hashes and the four milestone
fingerprints separate those two causes.

## What did run, before the freeze (not a gate)

On the same tree (newest source mtime `src/cfd/CfdMesh.cpp` 18:25:58, after the
mutation campaign restored it; the build below started 18:26:48):

```text
debug-ext incremental build   18:26:48 -> 18:28:52   1 object (CfdMesh.cpp), 0 warnings
debug-ext full ctest          18:28 -> 19:40         5938 / 5939 passed, 4279.89 s
   the one failure            cli.new.unicode-path: the run was not under code page
                              65001 (the CLI's output shows the mojibake), which
                              qualify.cmd sets and this run did not; re-run under
                              65001: 1 / 1 passed (prefreeze-unicode-path-65001.log)
CFD suites on that binary     CfdData_*, CfdInput_*, CfdFluid_*, CfdMesh_*:
                              97 cases, 3727 assertions, all passed
mutation campaign             31 probes, 31 killed, sources restored byte for byte
```

That is one preset, built incrementally, outside the harness. It says the tree
compiles warning-free and passes the whole suite once in Debug; it does not say
what the gate says, and it is recorded here only so the re-run has a baseline.

## What the re-run must show

Three presets each configured, cleaned, rebuilt and run unfiltered, with a no-op
rebuild of one edge each; the two repeat stages at 5 x the selected set, with the
selection COUNTED and equal to 2803 rather than merely non-zero (why, below);
0 `warning:` lines over the full object count
(753 + 2 expected: `CfdMesh.cpp` and `CfdMeshTests.cpp`); the suite at
5919 + 20 = 5939 in every preset; fingerprints before == after == this file's;
`postrun-mesh.ps1` exit 0, with the `CfdMesh_*` census and quality text identical
on all three presets; `freeze-mesh-pre.txt` == `-pre2.txt` == `-post.txt`, copied
here. Only then do `Determinism PASS`, `Regression PASS` and `Evidence recorded`
become `[x]`, and only then does `commit-mesh.ps1` run.

**The repeat selection must read 2803, and `qualify.cmd`'s guard will not tell you
so.** The guard refuses a selection of zero, which is not the failure that
happened. Attempt 1's repeat stages selected **352** and passed the guard: with no
test binary, `ctest -N` still matched the 352 `architecture.*` checks, which are
CMake script tests and need no executable. So the Determinism stage can report a
pass with not one CFD test in it, whatever its exit code. `qualify.cmd` is
byte-identical across fifty-four milestones and is not this milestone's to change,
so the count is checked by reading the log.

```text
P19-FLUID-001, same filter, both repeat presets    2783   its own
                                                          ctest-repeat-debug-ext.log
                                                          and -release-ext.log
QUALIFY_REPEAT in the two run-qualification.cmd    byte-identical, verified
                                                   2026-10-30
this milestone adds 20 tests, every one matching   so the baseline moves by 20
the filter's `unit\.Cfd` term
EXPECTED on the qualification machine              2803 = 2783 + 20
measured on the second machine, same filter        2793, of which 20 are
                                                   unit.CfdMesh_*. The 10 absent are
                                                   the gui.* tests the filter covers
                                                   and BETTERCAD_BUILD_GUI=OFF
                                                   removes; infra.deploy.location-
                                                   independent is not in the filter,
                                                   which is why the repeat delta is
                                                   10 and the full-suite delta is 11.
```

Require `ctest-repeat-debug-ext.log` and `ctest-repeat-release-ext.log` to each
read `out of 2803`. Any smaller number means the binary was missing or the filter
moved, and the stage measured nothing.

**One operational note on the launch.** `run-qualification.cmd` sets
`QUALIFY_MIN_FREE_GB` at its own `set` line, which is after `setlocal`, so a value
exported in the shell beforehand does not survive. The line itself has to carry the
figure, or the preflight refuses with exit 9 and the run never starts. Since
2026-10-30 it carries 20; before then it cleared the variable and the run could not
start at all.

## Recovery, 2026-10-29 (second machine, no toolchain)

What could be done without the qualification machine, and was:

```text
FINGERPRINT   re-computed from this tree a second time: all eight hashes == frozen
              (scratch tree 909a0e23, HEAD b1f151c unchanged)
P16 READ SET  the eight P16 files the gate reads hash-equal to HEAD, file by file
HARNESS FIX   run-qualification.cmd, line 39, comment only: "eight named refusals"
              -> "nine named refusals" (the header and ADR-063 define nine;
              InconsistentFaceOrientation was added after the independent review).
              git hash-object 704dda7bf5a5122ba03a1f5c6028822505e32e72
                           -> 39b91978204a9a440c108a221c196797266b5fd4
              Every line still begins with rem / set / call / echo / setlocal /
              endlocal / @echo (0 violations of the whole-word check). The three
              harness files whose hashes the invocation records are untouched:
              qualify.cmd d313a640, verify-harness.cmd 11855b8f, launch.cmd f11039e6.
              The file is not running; the edit is made before the launch.
```

The milestone's own fingerprints, computed here exactly as `freeze-mesh.ps1`
computes them (one line per file, `{path,-90} {git hash-object}`, LF-joined,
hashed). The re-run's `freeze-mesh-pre.txt` must show these four values, or the
tree moved:

```text
P19_MESH_CFD_MESH_FINGERPRINT    822ab6ed776c842950264bd410fe3e97e1627e75
    CfdMesh.hpp d24e3bd0, CfdMesh.cpp 17099c43 (the clean copies the mutation
    campaign restored), src/cfd/CMakeLists.txt f73ebbfe, CfdAnalysis.hpp 6f9d8fe9,
    CfdAnalysis.cpp 3ad2f6d4
P19_MESH_P16_READ_FINGERPRINT    caa1f6e90af0b1f9825dc9444d1d03a337de32e2
P19_MESH_MESH_TESTS_FINGERPRINT  af78f3aa7aecf1e6bc853034a3ba46789e7c5190
P19_MESH_ADRS_FINGERPRINT        6cc03381c76784139abde1eb2d47ca70e739ad5a
```

**Storage.** The volume that filled is the qualification machine's `C:`, which
holds `%TEMP%` (the compiler's `cc*.s` files), the OneDrive checkout, the build
root `C:\Users\hasib\bcdev\bc-build` and the WinGet toolchain. It is not
reachable from here, so its free space was not measured. What this copy of the
checkout shows: an in-tree `build/` directory of 8.1 GB holding three stale
CMake trees (configured on another machine, `C:/Users/uqhas/...`, no binaries,
newest file 2026-10-14), git-ignored and unused by the harness, which builds in
`bc-build`. If the OneDrive checkout on the qualification machine carries the
same directory, it is the first safe thing to remove. Also safe: orphaned
`%TEMP%\cc*.s` files from the three aborted compiles, `%TEMP%\bettercad-qualify-index`
and `%TEMP%\bettercad-harness-check` (both tiny, recreated by the harness).
Nothing under `docs/verification/` is temporary and none of it may be removed.
No estimate of the space the run needs is claimed here; the previous passing run
(P19-FLUID-001) built the same three trees on the same volume, so the space that
sufficed then is the baseline, plus what has since accumulated.

**Three corrections to the paragraph above, measured 2026-10-30.** Two of its
claims about the in-tree `build/` directory are wrong, and a third figure does not
reproduce. The original text is left as written; this is what measurement says.

```text
claim "no binaries"            WRONG. 164 .exe/.dll files across the three trees,
                               including build/debug/bin/bettercad_tests.exe at
                               780 248 526 bytes, a 531 MB shared-Debug test
                               binary, a 285 MB reference-models example and a
                               276 MB CLI. They are complete builds, not
                               configured-but-unbuilt trees.
claim "newest file 2026-10-14" WRONG. Three different dates, none of them that
                               one: debug-shared 2026-09-26, release 2026-09-28,
                               debug 2026-10-30 -- the last being a 144-byte VS
                               Code CMake query file and an empty ctest log
                               written today, not a build.
claim "of 8.1 GB"              DOES NOT REPRODUCE. Measured today: 7 251 358 536
                               bytes apparent over 10 588 files (7.25 GB decimal,
                               6.75 GiB), 6940 MiB allocated. How 8.1 was obtained
                               is not recoverable, so no cause is claimed here.
```

Because the three trees are of three different commits, their inter-preset size
ratios are not a usable model for anything, and were not used as one. The
paragraph's conclusion is unaffected: the directory is git-ignored, unused by the
harness, and still the first safe thing to remove.

**The free-space figure, measured rather than estimated.** The paragraph above
claims no estimate, and `preflight-space.ps1` deliberately supplies no default,
telling the operator to calibrate from the last passing run. That instruction
cannot be followed: P19-FLUID-001's freeze records **commit charge** (18.1 GB to
0.08 GB, 60.6 GB commit free), not disk free space, so there is no measured disk
figure from a passing run to calibrate against. The threshold was therefore
measured directly, on the second machine, by building all three presets from an
empty build root:

```text
three complete trees, GUI off, after full ctest       11 495 MB  = 11.23 GB
%TEMP% compiler scratch, peak, at -j6                      7.4 MB
    measured over 150 samples at 10 s; peak 7 concurrent cc*.s files,
    7616 KB; 148 of 150 samples populated
ctest artifacts, per tree                                    ~4 MB
Qt DLL staging, 3 trees, from the WinLibs GUI-on tree        143 MB
GUI app binaries, 3 presets (3.08 + 0.33 + 2.08 MB)          5.5 MB
Qt-dependent objects                              NOT MEASURABLE HERE, no Qt on
                                                  this machine; bounded well
                                                  under 0.5 GB by the GUI-on
                                                  tree's whole apps directory
                                           -----------------------------------
requirement from an EMPTY build root                        ~12 GB
```

`QUALIFY_MIN_FREE_GB` is **20**: about 12 GB consumed, about 8 GB still free when
the run ends. It was SET on 2026-10-30, on the owner's instruction, at
`run-qualification.cmd` line 110 -- line 98 before the comment block above it grew.

The preflight was re-tested with the figure in place, five threshold cases and both
chain directions:

```text
unset      exit 9   refuses, and still invents no default
abc        exit 9   not a positive whole number
0          exit 9   not positive
100000     exit 1   a volume is short
20         exit 0   "Every distinct volume has at least 20 GB free"
chain, 20       preflight returns and the line after the call IS reached, proved
                with a sentinel echo standing in for the qualify.cmd call
chain, 100000   the refusal banner prints and the sentinel is NOT reached: the
                process ends before qualify.cmd, which is the whole point of
                `exit` rather than `exit /b` in the wrapper
```

Two reasons 20 is conservative rather than tight. The threshold is free space
required on *each* distinct volume, and if the previous run's trees are still
present the run is close to net-zero on disk, because `qualify.cmd`'s `:clean`
runs `ninja -t clean` on a preset immediately before rebuilding that same preset,
freeing its outputs before writing new ones. The 12 GB case is an emptied build
root -- which is exactly what an operator short of space would arrange. Against
that, this machine's Release binaries came out 4 to 6 per cent *smaller* than the
qualification machine's, so its trees may run slightly larger; the headroom covers
it.

It also reframes attempt 1. `%TEMP%` needs 7.4 MB, not gigabytes, so the volume
that produced `No space left on device` had essentially nothing left. No threshold
above about 1 GB would have let that run start, and the preflight would have
refused it.

**Not done, and why.** Stages 6–13 of the recovery (configure, clean, build,
ctest in three presets; the 5x repeats; the cross-preset comparison; the
post-run script; the final README, TODO ticks and commit) need the authorised
toolchain: GCC 16.1 WinLibs MinGW, CMake 4.4, Ninja, the OCCT 8.0.1 / Qt 6.11.2
prefix, on the hasib machine. This machine has none of them, the `C:` volume is
not mounted, and the README claims no result from any other toolchain. They
remain BLOCKED until the run is launched there.

## The preflight added after attempt 1, 2026-10-29

Attempt 1 was launched with no disk check: `run-qualification.cmd`'s pre-launch
ritual restarts the OneDrive client and checks its memory, which is commit charge,
not free space. A free-space gate now stands in front of the run.

```text
preflight-space.ps1   bbf8e95a145076e727e31230a15bd7ee9ec80d1f   NEW
    measures the %TEMP% volume and the BETTERCAD_BUILD_ROOT volume, counts a
    shared volume once, lists what FREEZE.md says is safe to remove (the in-tree
    build/, the two harness scratch dirs, orphaned %TEMP%\cc*.s) and, with
    -WithSizes, the build root's occupied size. ASCII, no BOM, CRLF.
    Exit 0 enough / 1 short / 9 no threshold set.
preflight-space.cmd   a65d6578000b6e8a2811d5154bcec24be09c2b54   NEW
    the wrapper the harness calls. On a refusal it uses `exit`, not `exit /b`,
    so the process ends before run-qualification.cmd reaches its qualify.cmd
    line: no configure, no clean, no stage. launch.cmd's closing echo therefore
    does not appear on a refusal; the preflight's report does, in
    run-qualification.out, because launch.cmd redirects before calling.
run-qualification.cmd 39b91978 -> b819648b -> 865f206fe09a4a1373aa4462af40218416acf429
    three lines of code and a comment block: the threshold `set` line and
    `call "%~dp0preflight-space.cmd"` before the existing qualify.cmd call.
    Re-checked: every line still begins with rem / set / call / echo / setlocal /
    endlocal / @echo followed by a space or the end of the line, 0 violations of
    the whole-word check over all 116 lines.
    2026-10-30: the threshold is now SET to 20, on what is now line 110, and the
    comment block above it -- which said in capitals that the value was unset on
    purpose and that no figure was claimed -- was rewritten to carry the
    measurement and its derivation instead. Leaving it would have made the harness
    contradict its own value, which is the same class of defect as a stale
    comment claiming a guard that does not run. CRLF preserved, 104 -> 116 lines,
    pure ASCII, and `qualify.cmd` untouched at `d313a640`.
qualify.cmd           d313a640   UNCHANGED, byte-identical, fifty-four milestones
verify-harness.cmd    11855b8f   UNCHANGED, byte-identical
launch.cmd            f11039e6   UNCHANGED, byte-identical
```

The comparison could not go in `run-qualification.cmd` itself, because `if` is not
one of its allowed line prefixes, nor in `qualify.cmd`, which this milestone must
not touch. Hence a separate pair of files reached by one `call`.

**`QUALIFY_MIN_FREE_GB` is deliberately left unset, and the preflight refuses
(exit 9) until the owner sets it.** This file claims no figure for the space the
qualification needs, and the preflight does not invent one: a guess would either
refuse a run that would have passed or admit one that cannot finish. The figure
should come from a measurement -- `preflight-space.ps1 -WithSizes` reports the
build root's occupied size, and after a passing run that is the measured size of
three complete trees, plus headroom for `%TEMP%`.

**HAND-TESTED 2026-10-29, twelve cases, all as designed.** The preflight touches no
part of the toolchain -- it reads `%TEMP%`, `BETTERCAD_BUILD_ROOT` and a drive's free
space -- so it runs on a machine that cannot build BetterCAD, and it was run there,
under the same Windows PowerShell 5.1 and `cmd.exe` the qualification machine uses.

```text
A  threshold unset                     exit 9, names the variable          OK
B  threshold 1 GB                      exit 0, both volumes reported,
                                       one shared volume counted once      OK
C  threshold 999999 GB                 exit 1, refusal names the volume,
                                       its free space and the requirement  OK
D  threshold "abc"                     exit 9                              OK
E  threshold "0"                        exit 9                              OK
F  threshold "-5"                      exit 9                              OK
G  -WithSizes, build root absent       exit 0, "does not exist yet"        OK
H  wrapper, QUALIFY_SPACE_CHECK_ONLY,  exit 1 via exit /b, console left
   failing threshold                   alive, as the header promises       OK
I  wrapper, failing threshold          exit 1 by the hard `exit`           OK
J  wrapper, passing threshold          exit 0                              OK
K  INTEGRATION, a parent batch that
   calls the wrapper then echoes:
   failing threshold                   the parent NEVER reached its next
                                       line; exit 1                        OK
L  the same parent, passing threshold  the parent continued; exit 0        OK
```

K and L are the two that matter. They prove the design's claim on a stand-in for
`run-qualification.cmd`: on a refusal the process ends before the next line, so
`call qualify.cmd` is never reached and no stage begins; on a pass the parent
continues untouched. The branch the line-prefix rule forbids is not needed.

**What the test does NOT establish.** It was not run on the qualification machine, so
the free-space figures and the presence of the in-tree `build/` are this machine's,
not that one's; the `$SourceTree` default pointed at a path that does not exist here
and was correctly reported `[absent]`. It has never run as part of a real
`launch.cmd` invocation, so the claim that the preflight's report lands in
`run-qualification.out` is still read from `launch.cmd`'s redirect and not observed.
And `QUALIFY_MIN_FREE_GB` is still unset, so the next launch will stop at case A
until the owner sets it -- which is the intended behaviour, not a fault.

## The freeze-to-commit handoff, repaired and rehearsed, 2026-10-29

`freeze-mesh.ps1` and `commit-mesh.ps1` both defaulted their scratch directory to one
agent session's temp scratchpad:

```text
%TEMP%\claude\C--Users-hasib-Desktop-CAD\31d55056-e746-4b0d-938c-e551fb84138c\scratchpad
```

That directory is deleted when the session ends, and it does not exist now. The two
agreed with each other, so there was no mismatch -- but the freeze would have failed
writing into a missing directory, and had it been recreated and later cleaned, the
commit script would have failed **after** the seven-hour run.

**This did NOT void the source/test freeze.** Both scripts live under
`docs/verification/`, and the eight fingerprinted paths are `apps`, `include`, `src`,
`tests`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json`, while the four
milestone fingerprints name only files under `include/`, `src/`, `tests/`,
`examples/` and ADR-063. No harness script is inside either set, so the 2026-10-28
candidate stands and needs no refreeze. All twelve were re-confirmed afterwards.

```text
freeze-mesh.ps1   082322b6 -> d35083047881790ad1833f4026807dcff15266a3
    scratch defaults to C:\Users\hasib\bcdev\p19-mesh-scratch -- durable, beside the
    build root, not cloud-synced -- and is CREATED if absent. -RootDir and -GitExe
    added, defaulting to the qualification machine's values, with a fallback to
    `git` on PATH, so the freeze can be rehearsed on another checkout. It now also
    writes the scratch and root it used into its own output.
commit-mesh.ps1   396345ab -> 023c1deb -> ffbd94dbc98920092848f893a44025ea422db1c1
    same durable default and the same two parameters; refuses with a NAMED exit 6
    when the freeze file is absent, instead of the old misleading "does not carry
    eight hashes"; the placeholder guard is now -CaseSensitive; and, 2026-10-30,
    NAMED exits 7 and 8 for the commit message, which had the same defect and no
    guard at all (below).
```

**The case-sensitivity defect, found by rehearsing it.** `Select-String` is
case-insensitive by default, so the guard matched the ordinary lowercase word
"placeholders" in this directory's own README revision prose. It reported four
offending lines where three markers exist. Left alone, the operator would replace all
three real markers after a passing run, and the commit would still refuse at exit 4,
pointing at a sentence that is not a placeholder, with no edit able to clear it.
Measured on the real README: case-insensitive 4 lines (149, 165, 182, 183),
case-sensitive 3 lines (149, 165, 183). Fixed.

**The rehearsal, run on the non-qualification machine against this checkout** with
`-Scratch`, `-RootDir` and the PATH git:

```text
freeze-mesh.ps1 -Out freeze-mesh-pre.txt    exit 0; scratch auto-created; the file
                                            written; all eight subtree hashes and
                                            all four milestone fingerprints == frozen;
                                            "P16 files changed against HEAD" empty
commit-mesh.ps1 (same -Scratch)             READ the freeze file, recomputed, reported
                                            all eight "== frozen", REACHED the BOM
                                            guard, then refused on the markers that
                                            remain, exit 4, naming README.md 149,
                                            165 and 183 only.
                                            CORRECTED 2026-10-30: this was first
                                            written as "passed the BOM guard". It did
                                            not pass it -- there was no message file,
                                            so the check read nothing and could not
                                            have failed. See below.
commit-mesh.ps1 (empty scratch)             NO FREEZE FILE ... exit 6
after both                                  HEAD unchanged at b474ac8, nothing staged
```

So the handoff the long run depends on is proved **before** the long run: the commit
script can read the exact artifact the freeze writes, the fingerprints match through
it, and the placeholder guard both refuses now and will release once the three real
markers go.

**All three markers are in README.md, and only two of them are placeholders.** Lines
149 and 165 are the Regression and Result sections that a passing run rewrites. Line
183 is a *sentence in the revision history* that names the marker while explaining the
guard -- it keeps the guard armed today, and after a passing run it has to be reworded
too, or the commit refuses on prose. Check with the guard's own command before running
the commit script:

```text
Select-String -CaseSensitive -Path "docs\verification\P19-MESH-001\*.md" -Pattern "PLACE[H]OLDER"
```

and require zero matches. The bracket around the H is deliberate: the regex still
matches the marker, but this command's own text does not contain it, so writing the
check down does not re-arm the guard. That is a real hazard, not a hypothetical --
this file re-armed it twice while being written, once by quoting the refusal message
and once by spelling the pattern out, and was reworded both times. Any new evidence
prose that spells the marker in capitals blocks the commit.

**The third unguarded prerequisite, 2026-10-30.** `commit-mesh.ps1` needs two
inputs it cannot produce, and only one was guarded. `commit-message-mesh.txt` was
read at the BOM check with no existence test, and no such file existed anywhere --
not in the repository, not in any scratch directory. `Get-Content` on a missing
path raises a NON-TERMINATING error, so `$bytes` stayed `$null`, `$null -eq 239` is
false, and the check reported a pass having read nothing. The run would then have
died at `git commit -F` on a path that does not exist, with no named exit, after a
seven-hour qualification. That is the same shape as the dead scratchpath and the
case-insensitive marker scan: a prerequisite that only bites at the end.

Fixed by two checks placed before the index is touched, beside the freeze-file
guard: **exit 7** when the message file is missing, **exit 8** when it is empty or
whitespace only, which `git commit` would refuse anyway. Hand-tested, six cases, on
this checkout:

```text
parse check                                  0 errors
A  empty scratch                             exit 6, "NO FREEZE FILE"
B  freeze present, message absent            exit 7, "NO COMMIT MESSAGE"
C  freeze present, message 0 bytes           exit 8, "COMMIT MESSAGE IS EMPTY"
D  freeze present, whitespace only (10 B)    exit 8, same
E  freeze present, message valid             passes both new guards, recomputes all
                                             eight as "== frozen", prints "message
                                             first bytes: 66,101,116", then refuses
                                             at exit 4 on README.md 149, 165, 183
F  freeze present, message with a UTF-8 BOM  exit 3, "BOM present"
after every case                             HEAD unmoved at b474ac8, 0 files staged
```

Case E is the one that shows the defect was real: the byte triple it prints is
`Bet`, the first three characters of the message. The guard that existed was never
reading anything. Case F follows from that -- **exit 3 had never fired in the
history of this script**, because it was unreachable with no file present, and it is
now verified. The order that keeps a rehearsal safe is unchanged: both new guards,
then the scratch-index `git add`, then exits 2, 3 and 4, and only then the real
`git add` and `git commit`.

What the rehearsal does not cover: it ran with `-RootDir` pointing at this
checkout and PATH git, not the qualification machine's OneDrive root and MinGit, and
the scratch directories it used were removed afterwards.

## The candidate built and its whole suite run on the second machine, 2026-10-30 (not a gate)

The machine that could not build on 2026-10-29 -- the one the recovery and the
re-audit were done on -- now can. The pinned dependencies were built from
`deps/CMakeLists.txt` into a local prefix, which removes the block the re-audit
recorded (`Open CASCADE 7.9.3 found, BetterCAD requires 8.0.x`, Netgen absent).

```text
MACHINE    MSYS2 UCRT64 GCC 16.2.0, CMake 4.4.3, Ninja 1.13.2, ctest 4.4.3
DEPS       OCCT 8.0.1, Netgen 6.2.2604, zlib 1.3.1, prefix
           C:/Users/Hasib/AppData/Local/bettercad-deps/gnu-16-mingw-amd64
CONFIGURE  debug-ext, exit 0: Debug, static, warnings-as-errors ON, CLI ON,
           desktop app OFF (BETTERCAD_BUILD_GUI=OFF), volume meshing on Netgen,
           tests ON
BUILD      773 / 773 edges, exit 0, 10m41s, 0 `warning:` lines
REBUILD    the no-op rebuild runs only the always-run git-revision stamp and
           relinks nothing, so both runs below used the binaries that were built
SELECTED   5928, not 5939: BETTERCAD_BUILD_GUI=OFF removes exactly 11 --
           gui.launch.smoke, the nine gui.mesh.* claims, and
           infra.deploy.location-independent. Verified absent, with the other
           five infra.deploy.* present.
```

Two full unfiltered runs of the same binaries, differing only in the code page of
the console they were launched from:

```text
code page 65001 (what qualify.cmd sets)  5928 / 5928, ctest exit 0, 2035.19 s
code page 437                            5927 / 5928, ctest exit 8, 1946.89 s
   the one failure                       cli.new.unicode-path, the same test and
                                         the same cause as the pre-freeze record
unit.CfdMesh_*                           20 of 20 passed inside each full run,
                                         not only when run alone
F1 quality table                         all eight rows reproduce this
                                         milestone's README figures exactly,
                                         104 assertions
```

**Why the second run was made, and what it closed.** The pre-freeze record above
is 5938 / 5939 with this same failure, taken outside the harness, which left the
direction of the sensitivity open. A test that passed on an OEM code page and
failed under UTF-8 would have failed attempt 2 inside the harness at hour seven,
with nothing recorded predicting it -- the same shape of defect as the
case-sensitivity bug the commit rehearsal found. No full-suite run under 65001 had
ever been recorded. One has been now, and it is clean, so the sensitivity runs one
way only and the one sensitive test is the one that is sensitive by design.

**The 437 failure is the console, not the code.** It is diagnosed byte by byte in
`local-run-2026-10-30/README.md`: the argument and the file written to disk are
correct UTF-8, and only the captured stdout is mis-decoded, byte C3 as a box-draw
character, A5 as N-tilde, E2 as capital Gamma, 9C as a pound sign and 93 as
o-circumflex -- the emitted UTF-8 read back as CP437 and re-encoded. The
`FILE_REGEX` half passes; only `STDOUT_REGEX` fails. `qualify.cmd:39` is
`chcp 65001 > nul` and `qualify.cmd:12` says why, so the harness is already
correct and nothing in it needs changing.

**This closes no gate.** One preset of three, no clean build, no determinism
repeats, no cross-preset comparison of the census and quality text, GCC 16.2.0
rather than the qualification machine's WinLibs 16.1.0, the GUI excluded, and not
run through `qualify.cmd` at all, so there is no stage accounting. It says the
frozen candidate compiles warning-free and passes its whole suite on a second
toolchain -- which is why the eight F1 rows reproducing on a different compiler
build are the most useful thing in it. It does not say what the gate says.
`Determinism PASS`, `Regression PASS` and `Evidence recorded` stay open, and
`TODO.md` is unchanged.

Two of the seven lines the re-audit listed as `BLOCKED, no build` now have a
partial answer -- a warning audit and a full regression, on one preset -- and the
suite's own `CfdMesh_TheInspectionIsDeterministic` case passes, which is a test
case and not the determinism gate. All seven stand as `BLOCKED` gate items,
because a gate item is what the harness measures across three presets. Mutation
testing was not re-run here.

**No refreeze.** Everything written is under `docs/`, which is in neither the eight
fingerprinted paths nor the four milestone fingerprints. All twelve were
re-confirmed unchanged afterwards, and `qualify.cmd` is still `d313a640`.

Evidence: `local-run-2026-10-30/`.

## Three presets and both determinism repeats, 2026-10-30 (second machine)

```text
RESULT:    NO VERDICT. No gate closed, no mark changed, nothing ticked.
           P19-MESH-001 remains NOT QUALIFIED and BLOCKED on the authorised
           machine, exactly as this file records above.
           SUPERSEDED 2026-10-31 by attempt 2, which PASSED on the replacement
           machine: see the RESULT line at the top of this file and the
           Fifteenth issue. This block records what was true when it was
           written and is kept unchanged.
```

The one-preset run recorded above was extended the same day to all three
qualification presets and both repeat stages, on the frozen candidate, outside the
harness. Full detail and the file list are in `local-run-2026-10-30/README.md`.

```text
release-ext       full suite    5928 / 5928, exit 0, 1674.72 s
debug-shared-ext  full suite    5928 / 5928, exit 0, 1795.96 s
debug-ext         full suite    5928 / 5928, exit 0, 2035.19 s   (recorded above)
release-ext       repeat 5x     2793 selected, 13965 Passed, exit 0, 1413.59 s
debug-ext         repeat 5x     2793 selected, 13965 Passed, exit 0, 1148.56 s
no-op rebuild     release-ext and debug-shared-ext: one always-dirty git-revision
                  edge, zero compiles, zero links, exit 0
fingerprint       re-run after the last stage: all eight subtree hashes and all
                  four milestone fingerprints == frozen, scratch tree 399d006c,
                  HEAD b474ac8, P16-changed section empty
```

13965 is 2793 x 5 exactly, in both repeat logs, and the selection was counted from
the log rather than assumed.

**The cross-preset comparison is the part that could have failed, and did not.**
All eight F1 reference-mesh quality rows are identical across `-O0` static Debug,
`-O2` Release and shared Debug, and identical to the figures in ADR-063, with 104
assertions in each preset. The only differences between the three captures are the
build path, the working directory and Catch2's per-run seed, which differed in all
three -- so the figures are independent of test order as well as of optimisation
level. Non-orthogonality and skewness are `arccos`, cross products and divisions,
which is where `-O0` and `-O2` are entitled to diverge. They do not, to the last
recorded digit, on a compiler that is not the qualification machine's.

**What this still is not.** Three of the earlier gap lines are superseded -- one
preset, no determinism repeats, no cross-preset comparison. Four stand: it is not
a clean build, because `debug-ext` was incremental and no stage ran
`ninja -t clean`; the compiler is GCC 16.2.0 and not WinLibs 16.1.0; the GUI is
off, so the suite selects 5928 and not 5939 and the repeats select 2793 and not
the 2803 the gate requires; and it did not run through `qualify.cmd`, so there is
no stage accounting and no fingerprint check inside the run.

`Determinism PASS`, `Regression PASS` and `Evidence recorded` stay open, and
`TODO.md` is unchanged. A determinism gate is what the harness measures across
three presets at a counted 2803; five clean repeats of 2793 outside it is evidence
towards that gate and is not that gate.

**No refreeze.** Everything written is under `docs/`, which is in neither the eight
fingerprinted paths nor the four milestone fingerprints, and all twelve were
re-confirmed unchanged afterwards. `qualify.cmd` is still `d313a640`.

Evidence: `local-run-2026-10-30/`.

## Revision

First issue, 2026-10-29: the record of attempt 1 and of the review that found its
cause. Second issue, 2026-10-29: the recovery steps that could be done without the
qualification machine, above. Third issue, 2026-10-29: the note that HEAD moved to
`b474ac8` (the TODO / ROADMAP status correction) while the eight subtree hashes and
the four milestone fingerprints did not, so the re-run's two whole-tree output lines
can be read correctly; no result, hash or stage verdict in this file was changed by
it. Fourth issue, 2026-10-29: the free-space preflight recorded above -- two new
files, three lines in `run-qualification.cmd`, `qualify.cmd` untouched. No result,
hash or stage verdict changed by it either. Fifth issue, 2026-10-29: the preflight
hand-tested, twelve cases, recorded above; the earlier "NOT TESTED" note is replaced
by the measured result, and what the test still does not establish is stated with
it. Sixth issue, 2026-10-29: the freeze-to-commit handoff repaired and rehearsed, above
-- a dead session scratchpath redirected to a durable self-creating directory in both
scripts, a named exit 6 for a missing freeze file, and a case-sensitivity defect in the
placeholder guard found by the rehearsal and fixed. Harness plumbing only: no source or
test file touched, the eight subtree hashes and four milestone fingerprints re-confirmed
unchanged, and `qualify.cmd` still `d313a640`. Seventh issue, 2026-10-30: the frozen candidate built and its whole suite run twice on the second machine, recorded above and in `local-run-2026-10-30/` -- 5928 / 5928 under code page 65001, and the one failure under code page 437 diagnosed as a console mis-decode the harness already prevents. No gate closed, no mark changed, no source or test file touched; the eight subtree hashes and the four milestone fingerprints re-confirmed unchanged, and `qualify.cmd` still `d313a640`. Eighth issue, 2026-10-30: the re-run checklist above now names the repeat stage's expected selection, 2803, instead of requiring only a non-zero one -- the weak form of the very check attempt 1 passed with 352. The figure is derived from P19-FLUID-001's own repeat logs under a byte-identical filter and confirmed by measurement on the second machine. Also recorded: `run-qualification.cmd` clears `QUALIFY_MIN_FREE_GB` after `setlocal`, so the value must be set on that line. Documentation only: no gate closed, no mark changed, no source, test or harness file touched. Ninth issue, 2026-10-30: the third unguarded prerequisite in `commit-mesh.ps1` -- a commit-message file that was read without an existence test and did not exist -- fixed with named exits 7 and 8 and hand-tested in six cases, above. The rehearsal line claiming it "passed the BOM guard" is corrected: the guard read nothing, and exit 3 is verified here for the first time. Harness plumbing only: no source or test file touched, the eight subtree hashes and the four milestone fingerprints re-confirmed unchanged, and `qualify.cmd` still `d313a640`. Tenth issue, 2026-10-30: the second-machine run extended from one preset to all three and to both determinism repeats, recorded above -- 5928 / 5928 in each of the three presets, 2793 x 5 with 13965 Passed lines in each of the two repeat stages, all exit 0, and the eight F1 quality rows identical across optimisation levels and equal to ADR-063's. Three of the earlier gap lines are superseded and four still stand, so no gate closed and nothing was ticked. Recorded with it: three corrections to the Storage paragraph, two of its claims measured wrong and one figure that does not reproduce; and the measured free-space requirement, about 12 GB from an empty build root, with 20 GB recommended for `QUALIFY_MIN_FREE_GB` and line 98 deliberately left empty. No source or test file touched; the eight subtree hashes and the four milestone fingerprints re-confirmed unchanged, and `qualify.cmd` still `d313a640`. Eleventh issue, 2026-10-30: `QUALIFY_MIN_FREE_GB` SET to 20 at `run-qualification.cmd` line 110 on the owner's instruction, which supersedes the Tenth issue's note that line 98 was left empty. The comment block above it was rewritten at the same time, because it stated in capitals that the value was unset on purpose and that no figure was claimed; both had become false. Re-tested in five threshold cases and in both chain directions -- a passing threshold returns and reaches the line after the preflight call, a failing one ends the process before it. `run-qualification.cmd` b819648b -> 865f206f, CRLF preserved, 116 lines, every one still beginning with an allowed prefix. Harness plumbing only: no source or test file touched, no gate closed, nothing ticked; the eight subtree hashes and the four milestone fingerprints re-confirmed byte-identical afterwards, and `qualify.cmd` still `d313a640`. The free-space refusal is no longer a blocker to launching; the remaining operator input is the commit message, which `commit-mesh.ps1` needs only AFTER a passing run. Twelfth issue, 2026-10-30: this file and the rest of `docs/verification/P19-MESH-001/` committed for the first time, together with the harness scripts, which until now existed only in a working tree. HEAD moves with that commit and the candidate does not: the eight subtree hashes and the four milestone fingerprints are unchanged by it, exactly as the Third issue describes for the `b474ac8` status correction. The TWO WHOLE-TREE LINES DO move, as they did then -- `freeze-mesh.ps1` seeds its index with `git read-tree HEAD`, so `docs/` sits in that index and the tree it writes back changes whenever anything under `docs/` is committed, although `docs/` is in none of the twelve identities. That is exactly why the candidate is read against the twelve and never against HEAD or the scratch tree, and the `909a0e23 -> 399d006c` transition already recorded above is the same effect. The post-commit values cannot be stated inside the commit that produces them, so they are recorded separately, as this project's "record the ... commit" entries do. (An earlier draft of this entry listed scratch tree `399d006c` among the things the commit leaves unchanged. That was wrong, and the post-commit fingerprint run is what caught it.) The frozen candidate itself -- `CfdMesh.hpp`, `CfdMesh.cpp`, `CfdMeshTests.cpp`, the three modified CMake files and ADR-063 -- is deliberately NOT in that commit: `commit-mesh.ps1` lands the production code with its evidence after a passing qualification, which is the one commit that may also change a mark. Nothing was ticked and `TODO.md` is byte-identical to HEAD. Thirteenth issue, 2026-10-30: the whole-tree values for the evidence commit `7b22d98` recorded in the transition table above, which the Twelfth issue said would be recorded separately, and that table extended to carry `HEAD tree` -- a third moving line that was missing from it, so the earlier "Two lines" was an undercount. `freeze-mesh.ps1` was run twice before the values were taken and the two outputs were byte-identical, which is the protocol its own header requires. The twelve candidate identities were re-confirmed against `7b22d98` and are unchanged, and the P16-changed section is empty. The commit carrying this note necessarily has whole-tree values of its own, and they are deliberately NOT recorded: the regress has to stop somewhere, and the candidate is read against the twelve identities, never against HEAD or the scratch tree. Documentation only: no gate closed, no mark changed, no source, test or harness file touched. Fourteenth issue, 2026-10-30: an operator runbook for the re-run added at `qualification/RUNBOOK.md`, written by reading the scripts rather than from recollection, and the reading turned up two things. First, `postrun-mesh.ps1` STILL CARRIES the dead-session-scratchpath defect that the Ninth issue records as fixed for `freeze-mesh.ps1` and `commit-mesh.ps1`: its `$out` is hardcoded to an agent session's scratchpad under a session GUID that no longer exists, and because `New-Item -Force` recreates the path the script does not fail -- it writes its working files where nobody will look. That makes four instances of one defect class, three fixed and this one not. Second, `-CopyEvidence` copies only the two `cfdmesh-*` files per preset and NOT the summary, which is the artifact carrying the FRESH BINARY verdict, the zero-match table and every failure line; the script merely echoes it to stdout, so the runbook redirects that to a file in the evidence directory. Neither is fixed here: `postrun-mesh.ps1` is harness code and changing it is a separate decision, so both hazards are documented with the workaround instead, and neither `$root` nor `$src` is a parameter, so that script cannot be rehearsed off the authorised machine. Corrected in passing: ctest prints no "0 tests failed" clause on an all-pass summary line, so the string to require is `100% tests passed out of 5939` and the presence of the word `failed` is itself the failure signal -- an earlier draft of the runbook had the operator looking for a clause that never appears. Recorded too: each repeat log should carry 14015 `Passed` lines (2803 x 5), which is a stronger check that the repeats repeated than the summary line is. Documentation only: no gate closed, no mark changed, no source, test or harness file touched; the eight subtree hashes and the four milestone fingerprints re-confirmed unchanged, and `qualify.cmd` still `d313a640`. Fifteenth issue, 2026-10-31: THE RE-RUN PASSED, and this is the entry that rewrites the file's verdict. Attempt 2 ran Fri 30/10/2026 23:04:51 -> Sat 31/10/2026 01:50:59 (2h46m08s) against candidate `6a0ed5d0a4f0a56851b2b864d814ae7cb7d43e7d` plus the working tree -- the same twelve identities as `b1f151c`'s freeze, since every commit since then touched only `docs/`. 17 of 17 stages exit 0, `qualify.cmd` exit 0, "0 stage(s) failed". The full suite selected and passed 5939 in each of `debug-ext`, `release-ext` and `debug-shared-ext`; both repeat stages selected 2803 and ran `--repeat until-fail:5` with 14015 counted per-test `Passed` markers each, the figure the Fourteenth issue above derived in advance; 0 warnings and 0 errors over 755 objects; genuine ctest failure markers (`***Failed`, `***Exception`, `***Timeout`, `***Skipped`, `***Not Run`) 0 in all five logs. The eleven GUI-dependent tests are present in all three presets, which is what makes 5939 rather than 5928 the proof that the environment is whole, and all 20 `unit.CfdMesh_*` cases sit inside the repeat selection and appear 100 times (20 x 5) in each repeat log, so the determinism stage exercised this milestone's own code. Tree captures before the first build and after the last test are identical, and a post-run `freeze-mesh.ps1` capture has an identity portion byte-identical to `freeze-mesh-pre.txt`; `qualify.cmd` is still `d313a640`. THE MACHINE CHANGED: the laptop that produced attempt 1 was permanently replaced, so the environment was reproduced and qualified in its own right before the gate was run -- the same WinGet WinLibs package (GCC 16.1.0 POSIX UCRT, which also supplies CMake 4.4.1 and Ninja 1.13.2), and Qt 6.11.2, OCCT 8.0.1, Netgen 6.2.2604 and zlib 1.3.1 built by the repository's own hash-verified superbuild with that same compiler; no MSVC-built Qt anywhere. Recorded in `ENVIRONMENT-MIGRATION.md`, with the three problems the migration had to solve: a dependency-prefix key that cannot distinguish two GCC 16 toolchains (handled by renaming, not deleting, the MSYS2 prefix), a pinned CMake with no TLS trust anchors (handled by supplying a CA bundle, not by disabling verification), and every machine-specific path in the harness pointing at the dead laptop. The most dangerous of those was not in the brief: a fresh process on this machine resolves `gcc` and `cmake` to MSYS2 UCRT64 (GCC 16.2.0, CMake 4.4.3), not to the pinned bundle, so the harness would have qualified the WRONG TOOLCHAIN with every test count, warning count and subtree hash still looking exactly right. `run-qualification.cmd` now pins the bundle on `PATH` ahead of the preflight and `qualify.cmd` calls. ONE GATE FAILED ON THE WAY, and the fix was to the checker, not the product. `postrun-mesh.ps1` compares the `CfdMesh_*` census and quality text across presets and ran the test binary without pinning a seed. That binary's own `--help` says `--order <decl|lex|rand>  test case order (defaults to rand)`, so a direct invocation shuffles the cases on a per-run seed and the stdout block order is random; the `CfdMesh_*` filter emits two reporting blocks, so three independently shuffled runs agree only 1 time in 4. It failed accordingly, with `debug-ext` and `release-ext` agreeing and `debug-shared-ext` not -- a pattern that invites the tidy explanation that static-versus-shared initialiser order is responsible, which running the same binary twice disproved. Every measured value was in fact identical: all 16 RM-MESH census and quality lines match once sorted, across runs of one binary and across presets, as do the 3727 assertions in 97 cases and the golden fingerprint `cfd-definition-v1:20707079b32e4964`. `--rng-seed 1` -- the convention the P12 `collect-evidence.cmd` scripts already use for direct-binary evidence runs, and what CLAUDE.md means by recording a seed a result depends on -- makes three runs on three presets byte-identical, and makes the check STRICTER, because it can no longer pass by luck. The comparison itself was not touched and was not made order-insensitive; the six evidence files now reduce to two sha256 values, one per invocation type. The failing summary is preserved outside the repository as `C:\bettercad\attempt-1-evidence\postrun-summary-FAILED-unpinned-seed.txt`, because a gate that failed is evidence too. `postrun-mesh.ps1` lives under `docs/verification/`, in neither the eight fingerprinted paths nor the four milestone fingerprints, so neither that fix nor the path migration voided the freeze. THE FOURTEENTH ISSUE'S TWO HAZARDS ARE NOW CLOSED: `postrun-mesh.ps1`'s `$out`, `$root` and `$src` became the parameters `-Out`, `-BuildRoot` and `-SourceTree`, which retires the dead-session-GUID scratch path and makes that script rehearsable off one machine; and `-CopyEvidence` still does not copy the summary, so the run redirected it to `qualification/postrun-summary.txt`, where it is now committed evidence rather than lost stdout. ON THIS EVIDENCE `Determinism PASS`, `Regression PASS` and `Evidence recorded` become `[x]`, the first marks this milestone has gained since 2026-10-28, and `commit-mesh.ps1` lands the production code with this evidence. The owner accepted the `--rng-seed 1` fix before anything was ticked. Sixteenth issue, 2026-10-31: the milestone committed as `189b87f` by `commit-mesh.ps1`, production code and evidence in one commit -- the frozen candidate's seven paths, ADR-063, the 34 evidence files of this directory, and `TODO.md` and `ROADMAP.md` with the three marks moved. The script recomputed the eight-path fingerprint from the working tree before touching the index and read the commit's own subtree hashes back afterwards; both sets equal the frozen ones, so the qualified tree is the committed tree. `.claude/` and `AGENTS.md` were deliberately left untracked: agent tooling is the developer's, not the project's. This note's own commit necessarily has whole-tree values of its own and they are not recorded, for the reason the Thirteenth issue gives: the regress has to stop somewhere, and the candidate is read against the twelve identities, never against HEAD or the scratch tree.
