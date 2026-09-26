# INFRA-QT-DEPLOY-001 — Qt deployment independent of build location

```text
TASK:      INFRA-QT-DEPLOY-001 -- make build and test output independent of
           where the source tree lives, and make Qt deployment work from any
           build tree, so P15-UNITS-001's blocked determinism gate can be run
           in a changed environment rather than repeated in the same one.
STATUS:    see RESULT.
```

## TASK

BetterCAD's checkout lives in a OneDrive-synchronised folder. Six milestones
running — P14-DIM-001, P14-ANNO-001, P14-BOM-001, P14-REFMOD-001, P14-QUAL-001
and P15-UNITS-001 — have failed a determinism repeat on one signature:

```text
DrawnHolePlate: save failed: cannot replace
'...build/debug/tests/cli-output/refmod\drawing_hole_plate.bcad':
Permission denied
```

A Windows sharing violation on an atomic **replace**, inside the synchronised
build tree, of a file the same test had already written successfully earlier in
the same run. Never a first write; always a replace. Through P14 it cost one
controlled rerun per milestone. In P15-UNITS-001 it cost the gate: the debug
determinism stage failed twice on an unchanged tree, in two different tests,
with zero test-logic assertions failing, and a third attempt under the same
conditions would have been choosing which result to keep.

Moving the build tree out of the synchronised folder was therefore the
prerequisite, and it was itself blocked: building outside the source tree failed
the GUI target in all three presets.

## SCOPE

Build infrastructure only. No C++ changed; no module boundary crossed; no
public API touched. The milestone is authorized by `TODO.md`'s carried
infrastructure decision, which says to treat it as a separate infrastructure
task with its own regression evidence.

## BASELINE — and the diagnosis that was wrong

`TODO.md` and `docs/verification/P14-STREF-001/closure/run-qualification.cmd`
both recorded this cause:

```text
windeployqt resolves the Qt runtime RELATIVE TO THE EXECUTABLE it is
deploying, as <exe dir>/../../<toolchain key>/bin. That only names the real
Qt when the build tree sits inside the source tree.
```

**That is wrong.** It is written down here because it was believed for two
milestones and shaped the plan for this one, and because the two documents that
carry it are corrected by this directory rather than quietly edited.

It was disproved by moving the executable and watching the reported path *not*
move. `bettercad.exe` was copied to three unrelated directories and deployed
with no other change:

| Executable directory | windeployqt looked for Qt6Core.dll in |
| --- | --- |
| `%LOCALAPPDATA%\BetterCAD-build\debug\bin` | `%LOCALAPPDATA%\BetterCAD-build\gnu-16-mingw-amd64\bin` |
| `C:\bc-x\BetterCAD-build\debug\bin` | `%LOCALAPPDATA%\BetterCAD-build\gnu-16-mingw-amd64\bin` |
| `%LOCALAPPDATA%\zzz-build\debug\bin` | `%LOCALAPPDATA%\BetterCAD-build\gnu-16-mingw-amd64\bin` |

The path windeployqt reported is the same in all three, and in two of them it
has no relation to where the executable is. It is not derived from the
executable at all. `log/reproduction.log` holds these runs.

Also ruled out, each by measurement:

- `qtpaths -query` reports the correct prefix
  (`…/bettercad-deps/gnu-16-mingw-amd64`).
- Passing `--qtpaths` explicitly changes nothing.
- `qtpaths.exe` is not on `PATH` at all, and putting the Qt `bin` directory on
  `PATH` changes nothing.
- There is no `qt.conf` anywhere in the Qt installation.
- No file under the Qt prefix contains the string `BetterCAD-build`.
- No environment variable, and no `HKCU\Environment` value, contains it.

## ROOT CAUSE

windeployqt's own verbose trace names the right directory and then reads from
the wrong one:

```text
Qt binaries in C:\Users\uqhas\AppData\Local\bettercad-deps\gnu-16-mingw-amd64\bin
readPeExecutableDependencies: ... dependent libraries: Qt6Core.dll Qt6Gui.dll ...
Unable to find dependent libraries of
    C:\Users\uqhas\AppData\Local\BetterCAD-build\gnu-16-mingw-amd64\bin\Qt6Core.dll
```

One path component differs, and both directories are children of
`%LOCALAPPDATA%`. NTFS generates an 8.3 short name from the first six
characters of a long name, with the characters illegal in 8.3 removed, plus
`~N`; `bettercad-deps` and `BetterCAD-build` both reduce to `BETTER`, so both
answer to variants of one short name. **windeployqt resolves the Qt binary
directory through that short name, and reaches the wrong directory.**

Which one it reaches was then pinned down by creating single empty directories
beside the dependency prefix — the prefix itself never touched — and deploying
the same executable again:

| Directory created in `%LOCALAPPDATA%` | 8.3 basis | Sorts vs `bettercad-deps` | Deploy result |
| --- | --- | --- | --- |
| *(none)* | — | — | exit 0, 4 Qt DLLs |
| `BetterCAD-build` | `BETTER` | before | **exit 1, 0 DLLs**, names `BetterCAD-build` |
| `bettercad-aaa` | `BETTER` | before | **exit 1, 0 DLLs**, names `bettercad-aaa` |
| `bettercad-zzz` | `BETTER` | after | exit 0, 4 DLLs |
| `bette-aaa` | `BETTE-` | before | exit 0, 4 DLLs |
| `bc-build` | `BC-BUI` | before | exit 0, 4 DLLs |

and with *two* same-basis siblings present, both sorting before the prefix, the
**first in name order** is the one reached:

```text
bettercad-aaa + BetterCAD-build present  ->  names bettercad-aaa
```

So the predicate is:

> among the dependency prefix's siblings that share its six-character 8.3
> basis, one sorts before it by case-insensitive name order — and the first
> such sibling is what the lookup reaches.

This is the behaviour of a lookup that enumerates the parent directory and
takes the first match. `log/root-cause.log` holds every trial.

The build root chosen when the move was first attempted,
`%LOCALAPPDATA%\BetterCAD-build`, satisfied that predicate against
`%LOCALAPPDATA%\bettercad-deps`. The failure was created by the **name** of the
build root, and would have happened with that name from any location. Nothing
about building outside the source tree was ever the cause.

### The `~N` numbers do not decide it

Worth stating separately, because the first version of the fix got it wrong.
With all four directories present the filesystem reports:

```text
bettercad-deps   -> BETTER~1
BetterCAD-build  -> BETTER~1      (the same name, twice)
bettercad-aaa    -> BETTER~2
bettercad-zzz    -> BETTER~3
```

`bettercad-aaa` holds a **distinct** short name and still took the lookup. A
guard that required an equal short name would therefore have passed a
configuration that does not deploy. Name order decides it; the numbers do not.

### What is not established

- The exact call inside windeployqt that resolves the short name by
  enumeration. Qt's sources were not read; the behaviour is inferred from the
  ordering experiments above, which are reproducible.
- Why `%LOCALAPPDATA%` reports one short name for two of its children at all. A
  freshly created colliding pair in a different parent gets distinct names
  (`AAAAAA~1`, `AAAAAA~2`), so this is a property of that directory's
  short-name index. `UNVERIFIED`, and not relied on: the guard keys on the
  ordering rule, which held in every trial, duplicate or not.

## IMPLEMENTATION

Nothing can be fixed inside windeployqt. What is in BetterCAD's hands is: do
not create the condition, refuse it early when it exists, and never ship a
build tree whose deployment silently produced nothing.

**`cmake/BetterCADBuildLocation.cmake`** (new) — two configure-time guards.

- `bettercad_check_deps_prefix_resolvable()` finds the sibling that a
  short-name lookup would reach instead of the dependency prefix, and refuses
  to configure. The diagnostic names both directories, the short name, why they
  compete for it, and the two ways out. A same-basis sibling that sorts *after*
  the prefix is left alone: it was measured not to break deployment, and
  warning about every such directory would be noise on every configure.
- `bettercad_check_build_root()` refuses to configure an `-ext` preset when
  `BETTERCAD_BUILD_ROOT` is unset, or when it points back inside the source
  tree. A preset that exists to leave the synchronised folder must not quietly
  build inside it.

**`cmake/WindowsShortPath.cmd`** (new) — CMake has no short-path primitive and
`cmake -E` has no equivalent, and the batch-parameter path operator that
produces one works only inside a batch file.

**`cmake/DeployQtRuntime.cmake`** — windeployqt's exit code is no longer taken
as proof. The script now also requires that a `Qt6Core` library actually
appeared beside the executable, and reports when windeployqt named a Qt library
outside the configured Qt directory — including when it exited 0, since a
deployment assembled from the wrong prefix is worse than one that failed. Only
Qt libraries are matched: windeployqt may have something to say about a library
that is legitimately elsewhere, and that is not this defect.

**`CMakePresets.json`** — `debug-ext`, `release-ext`, `debug-shared-ext`, whose
`binaryDir` is `$penv{BETTERCAD_BUILD_ROOT}/${presetName}`. Configure, build and
test presets all resolve to the same directory because the build and test
presets name the configure preset. No absolute path is committed; the location
is the developer's, and an unset variable is an error rather than a default.

**`CMakeLists.txt`**, **`apps/bettercad/CMakeLists.txt`** — the two guards are
called; the deployment command is given the configured Qt directory so it can
tell a misresolution from a genuine absence.

## BLAST RADIUS

Every target is rebuilt from a new build tree, so the regression set is the
whole suite in all three presets rather than anything narrower. The deployment
script runs in the POST_BUILD step of the only Qt target; the build-root guard
runs on every configure and returns immediately unless an `-ext` preset set the
option; the collision guard runs only when the Qt application is being built.
No C++ was touched, so no module, layer, serialized format, stable reference or
reference model is affected.

## TESTS

`tests/infra/`, 14 new tests, Windows only.

The deployment failure paths are driven against a stand-in for windeployqt, so
each can be made to fire without needing a Qt installation in a particular
state:

```text
infra.deploy.deploys-what-it-promised                              a correct deployment is still accepted
infra.deploy.rejects-a-deployment-that-copied-nothing              exit 0 and no Qt6Core is not success
infra.deploy.names-both-directories-when-the-prefix-is-misresolved the diagnostic names both paths
infra.deploy.rejects-a-misresolved-prefix-that-reported-success    exit 0 from the wrong prefix is refused
infra.deploy.accepts-the-configured-prefix                         the same message shape, right path, accepted
```

The build-location logic. Because the predicate turned out to rest on directory
*names* rather than on a duplicate short name, the condition that broke
deployment can be reconstructed from names alone, and both branches are tested
for real:

```text
infra.buildlocation.short-name-basis                   the six-character basis, including dots and spaces
infra.buildlocation.shadowing-sibling-is-found         a sibling sorting first is found; the FIRST of two;
                                                       case-insensitively, or BetterCAD-build would be missed
infra.buildlocation.no-shadowing-sibling               sorting after, a differing basis, and alone: none
infra.buildlocation.short-path-of-a-generated-name     a long name reports a generated short name
infra.buildlocation.short-path-of-an-8dot3-name        a name that fits 8.3 reports none of its own
infra.buildlocation.external-root-required             unset BETTERCAD_BUILD_ROOT is refused
infra.buildlocation.external-root-accepted             a root outside the source tree is accepted
infra.buildlocation.build-inside-source-tree-rejected  a root pointing back inside is refused
```

And the one that tests the actual capability:

```text
infra.deploy.location-independent
```

It copies the built application, and everything it needs *except* Qt, into a
directory that is in neither the source tree nor the build tree; deploys through
the production `DeployQtRuntime.cmake` with the production arguments; requires
`Qt6Core.dll` and `platforms/qoffscreen.dll` to exist; and then starts the
executable there with `PATH` cut down to `System32`, so a Qt installation on
`PATH` cannot hide a deployment that copied the wrong libraries or none. The
test asserts its own premise first: if the probe directory were inside either
tree it fails rather than passing vacuously.

### Both guards were made to fail on the real defect

A test that cannot fail proves nothing, so the real condition was recreated —
`mkdir %LOCALAPPDATA%\bettercad-aaa`, one empty directory, nothing else changed:

```text
configure                          FATAL_ERROR, 'bettercad-aaa' is reached instead of 'bettercad-deps'
infra.deploy.location-independent  ***Failed
```

and with it removed, both pass again. The two-sibling case that the first
version of the guard would have passed is in the same log. `log/guard-fires.log`.

## ADVERSARIAL REVIEW

Three defects were found by review and by reading the evidence, rather than by
the suite going green. All three were real:

1. **`if(PATH_EQUAL)` does not work under `cmake -P`.** It needs policy
   CMP0139, and script mode sets no policies, so the `if()` raised an error
   instead of comparing. `DeployQtRuntime.cmake` runs as `cmake -P` in every
   GUI build, so this would have broken every GUI build on the machine. The
   deployment failure-path tests caught it before the first build. Every one of
   these comparisons turned out not to need path semantics: an empty
   `file(RELATIVE_PATH)` already means "the same directory", and the names being
   compared come from one directory listing.

2. **Two tests were passing vacuously.** `bettercad_short_path` returned empty
   for everything, because an inline `cmd /c "for %I in (…)"` was swallowed by
   cmd.exe's quoting rules — and empty is also how the helper says "this
   filesystem has no short names", so the checks built on it agreed with each
   other and tested nothing. The guard could never have fired. The fix is the
   helper script; the guard against a recurrence is
   `infra.buildlocation.short-path-of-a-generated-name`, which proves a
   generated short name is reported *and* leads back to the directory it came
   from, and which distinguishes "no short names here" from "the helper did not
   run" using an independent `dir /x` probe.

   A third, smaller version of the same mistake followed immediately: the
   helper's own `rem` comment contained a percent-tilde token, and cmd.exe
   performs batch-parameter substitution on a `rem` line too, so an unknown
   modifier there failed the whole script. The positive test caught that too.

3. **The guard's predicate was wrong, and the generated evidence log is what
   showed it.** The first version required a sibling to report the *same* 8.3
   short name as the dependency prefix. Printing the short names of all four
   test directories at once showed `bettercad-aaa` holding `BETTER~2` —
   distinct — while still taking the lookup and being named in windeployqt's
   error. So the guard would have accepted a configuration that does not
   deploy. Two further experiments pinned the real rule: with two same-basis
   siblings the first in name order is reached, and a sibling whose basis
   differs by one character is harmless. The guard now keys on that, and the
   qualification was re-run from clean because the tree changed.

   The failure mode here is worth naming: the first predicate was inferred from
   a single observation in which the two properties — equal short name, earlier
   name — happened to coincide, and no experiment had yet separated them.

Also considered: whether the new failure path could reject a working build. A
successful deployment prints no `dependent libraries of` line at all — measured,
0 occurrences — and the match is further restricted to Qt libraries, so a
windeployqt remark about a non-Qt library cannot trip it.
`infra.deploy.deploys-what-it-promised` and
`infra.deploy.accepts-the-configured-prefix` are the standing guards for that.

## QUALIFICATION

Three presets, each from the external build root
`C:\Users\uqhas\AppData\Local\bc-build`, configured, cleaned, built with
warnings as errors, proved fresh by a no-op rebuild, and only then tested.
`qualification/`, harness `qualify.cmd` with `QUALIFY_PRESETS` pointed at the
`-ext` presets.

```text
preset             configure  clean  build      no-op rebuild  suite
debug-ext              0        0      0             0         2300/2300
release-ext            0        0      0             0         2300/2300
debug-shared-ext       0        0      0             0         2300/2300

repeat debug-ext, filter infra|gui|cli, until-fail:5   0    715 = 143 x 5
qualification finished 21:09:51, 0 stage(s) failed
```

0 compiler warnings in all three builds. 2300 = the 2286 of P15-UNITS-001 plus
the 14 tests added here.

The harness was proved able to fail before it was trusted:
`verify-harness.cmd` points it at a preset that does not exist and requires a
non-zero exit — 3 stages failed, exit 3.

**Qualified tree = committed tree.** The eight source tree IDs were recorded
before the first build and after the last test run, and are identical:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include ce391ffe8b39b358dce86f0300a0bdebd7cb8ff9
src bad0cd634ee0be081d885ef41e8892af753be5ce
tests 4774354cb60e5007df8d387e9fe15ffe821df0cc
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

An earlier, complete run of this same chain — three presets, stress and the
determinism gate, all passing — was **discarded**, not cited. It qualified a
tree with the first version of the collision guard, whose predicate adversarial
review then showed to be wrong (see ADVERSARIAL REVIEW 3). Changing `cmake/`
and `tests/` voided it, so it was run again from clean rather than kept.

## REGRESSION — does moving the build tree stop losing the replace?

This is the question the whole milestone exists to answer, so it is measured
directly rather than inferred from the suite passing.

```text
ctest --preset debug-ext -R "cli.refmod.build|cli.drawing.batch" --repeat until-fail:12

cli.drawing.batch                      12 runs, 12 passed
cli.drawing.batch.fails                12 runs, 12 passed
cli.drawing.batch.fails.wrote-nothing  12 runs, 12 passed
cli.refmod.build                       12 runs, 12 passed
                                       48 runs, 0 failures, exit 0
```

`qualification/stress-file-replacement.log` — the individual runs, not just the
summary line. Twelve exceeds the ten asked for, and matters because both
recorded failures came on the **3rd** repeat.

The timings corroborate it. `cli.refmod.build` in the synchronised tree ran
3.77 s, then 1.96 s, then failed. Here, twelve consecutive runs: 1.19, 1.20,
1.32, 1.36, 1.40, 1.41, 1.42, 1.42, 1.43, 1.48, 1.62, 1.80 s. The variance that
preceded the failure is gone, which is what removing a contending file handle
should look like.

**What this does not show.** The fault is avoided, not fixed, and only for build
output. `writeFileAtomically` in `src/io/FileIo.cpp` performs a single
`std::filesystem::rename` with no retry and discards the temporary file on
failure, so a user saving a document into a synchronised folder can still get
`cannot replace '...': Permission denied` on an ordinary save. That is product
behaviour, outside this milestone's scope, and is recorded as an open item in
`TODO.md` so that this milestone's result cannot be read as having fixed it.

## KNOWN LIMITATIONS

- The guard checks the dependency **root** — the component whose name needs a
  short name. A collision on a higher ancestor of the Qt prefix would not be
  caught at configure time. The deployment-time check still refuses it, with
  the same explanation.
- Name order is compared as lower-cased byte order, which matches NTFS
  enumeration for ASCII names. A non-ASCII directory name could be ordered
  differently by the filesystem's collation than by this comparison, and the
  guard would then misjudge. The deployment-time check does not depend on
  ordering and remains the backstop.
- A Qt prefix whose path contains a space goes undiagnosed by the
  deployment-time check: the reported path is matched up to the first space,
  because windeployqt puts the rest of the sentence on the same line.
  Deployment still fails, with windeployqt's own message. Half-matching a path
  to name a directory would be worse.
- Windows only. The guards return immediately elsewhere, and the milestone
  claims nothing about other platforms.

## RESULT

```text
TASK:            INFRA-QT-DEPLOY-001 -- Qt deployment independent of build location
ROOT CAUSE:      windeployqt resolves the Qt binary directory through its 8.3
                 short name and reaches the first same-basis sibling in name
                 order. The recorded diagnosis -- resolution relative to the
                 executable -- is DISPROVED: log/reproduction.log.
IMPLEMENTATION:  two configure-time guards (short-name shadowing, external
                 build root); deployment verifies its own result; -ext presets
                 build into BETTERCAD_BUILD_ROOT
TESTS:           14 new, tests/infra/. Both guards made to fail on the real
                 defect and then pass with it removed: log/guard-fires.log
QUALIFICATION:   3 presets from an external build root, 2300/2300 each, 0
                 warnings, fresh binaries, 0 stages failed
REGRESSION:      48 consecutive runs of the four file-replacing CLI tests, 0
                 failures, from the moved build tree
ADVERSARIAL:     3 defects found and fixed, one of which would have broken
                 every GUI build and one of which invalidated the first
                 qualification
RESULT:          PASS
EVIDENCE:        docs/verification/INFRA-QT-DEPLOY-001/
                 README.md, log/{reproduction,root-cause,guard-fires}.log,
                 qualification/
NOT CLAIMED:     that the atomic-replace fault is fixed. It is avoided, for
                 build output only. src/io/FileIo.cpp still does one rename
                 with no retry -- carried in TODO.md.
TODO:            INFRA-QT-DEPLOY-001 8 items ticked; P15-UNITS-001's
                 determinism gate closed from the moved tree
```
