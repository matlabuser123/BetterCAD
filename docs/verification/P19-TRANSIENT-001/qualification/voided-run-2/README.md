# The second qualification run, VOIDED — the launching shell could not see the dependency prefix

```text
LAUNCHED:  2026-11-10 07:25:08   from the desktop app's interactive terminal tab
FINISHED:  2026-11-10 08:28:53
VERDICT:   QUALIFICATION FAILED: 3 stage(s) failed
CAUSE:     the shell it was launched from cannot see
           C:\Users\Hasib\AppData\Local\bettercad-deps, so CMake never put the
           dependency prefix on CMAKE_PREFIX_PATH and find_package(OpenCASCADE)
           fell through to the ambient msys64 installation, 7.9.3
PRODUCT:   not implicated -- no compiler ran at all
ACTION:    re-run from a shell that can see the prefix. Re-launching from the
           same tab would fail again in the same five seconds.
```

## What the harness said

Its own two statements, which are the verdict:

```text
run-qualification.out     QUALIFICATION FAILED: 3 stage(s) failed.
qualification-times.txt   qualification finished ... 3 stage(s) failed
```

**A FAILED RUN IS A FAILED RUN AND THIS ONE IS KEPT AS ONE.** Nothing below
converts it into a pass. What it establishes is narrower: not one line of
C++ was compiled, so the tree cannot be what failed.

## The stage timeline

```text
debug-ext        configure  exit 1   07:25:10 -> 07:25:14    4.6 s
debug-ext        build / ctest       SKIPPED: the configure failed
release-ext      configure  exit 1   07:25:15 -> 07:25:17    2.3 s
release-ext      build / ctest       SKIPPED: the configure failed
debug-shared-ext configure  exit 1   07:25:17 -> 07:25:19    2.0 s
debug-shared-ext build / ctest       SKIPPED: the configure failed
repeat release-ext  exit 0   07:25:24 -> 07:56:54   selects 3522 test(s)
repeat debug-ext    exit 0   07:56:55 -> 08:28:52   selects 3527 test(s)
```

Three configures, three identical refusals, all within nine seconds of each
other:

```text
CMake Error at cmake/BetterCADDependencies.cmake:85 (message):
  Open CASCADE Technology 7.9.3 found in
  C:/msys64/ucrt64/lib/cmake/opencascade; BetterCAD requires 8.0.x.
```

That is the project's own supported-range check firing exactly as designed. It
is not a defect; it is the gate reporting that the wrong kernel was found.

## The root cause, MEASURED in both shells rather than argued

`CMakeLists.txt:40` computes `BETTERCAD_DEPS_PREFIX` from the toolchain key and
`CMakeLists.txt:42` prepends it to `CMAKE_PREFIX_PATH` **only
`if(EXISTS ...)`**. A probe was run that prints that test's result, the
resulting prefix path, and what `find_package(OpenCASCADE CONFIG QUIET)` then
resolves — the same script, the same `cmake.exe`, run in each shell:

```text
                             the agent's own shell   the app's terminal tab
LOCALAPPDATA                 C:\...\AppData\Local    C:\...\AppData\Local   same
cmd  IF EXIST <prefix>       YES                     NO
cmd  dir <deps root>         2 entries listed        "File Not Found"
cmake EXISTS <prefix>        YES                     NO
cmake IS_DIRECTORY <prefix>  YES                     NO
cmake file(GLOB deps/*)      2 entries               (empty)
CMAKE_PREFIX_PATH after      <the deps prefix>       (empty)
find_package(OpenCASCADE)    8.0.1 at the prefix     7.9.3 at msys64
```

The directory is on disk — the left column lists its two children — and
`LOCALAPPDATA` is the same string in both shells. **The app's interactive
terminal runs inside a filesystem sandbox that hides paths outside the
session's folders, and the dependency prefix is one of them.** Every link in
the chain is a measurement, not an inference.

msys64's OCCT is not new and is not a change: its
`OpenCASCADEConfig.cmake` is dated 2026-08-10 and `C:\msys64\ucrt64\bin` has
been on `PATH` throughout. It never wins, because `CMAKE_PREFIX_PATH` is
searched before the prefixes CMake derives from `PATH`. It won here only
because the deps prefix was invisible and so was never on
`CMAKE_PREFIX_PATH` at all.

### Why the poisoned cache told a mixed story, which is the confirmation

After the failure the three `CMakeCache.txt` files held:

```text
OpenCASCADE_DIR   C:/msys64/ucrt64/lib/cmake/opencascade          <- moved
Qt6_DIR           <deps prefix>/lib/cmake/Qt6                     <- unmoved
BETTERCAD_DEPS_PREFIX  <deps prefix>                              <- unmoved
CMAKE_CXX_COMPILER     the WinLibs GNU 16.1.0 c++                 <- unmoved
```

CMake re-validates a cached `<pkg>_DIR` when that package's `find_package`
runs. OCCT's ran: its config file was invisible, so the cached value was
discarded and the search restarted and landed on msys64. Qt's never ran,
because the fatal error came first, so its cached value survived untouched.
A mixed cache is what this mechanism predicts, and it is what the cache holds.

## The two "exit 0" repeat stages prove nothing, and one of them is a lie worth naming

Both repeat stages reported `100% tests passed` and exit 0. Neither ran a
binary built in this run — no build stage ran at all — so both ran whatever
was left in the build trees on disk.

```text
repeat release-ext   3522 tests, 20 unit.CfdTransient_, 9 unit.CfdTimeGrid_
repeat debug-ext     3527 tests, 24 unit.CfdTransient_, 10 unit.CfdTimeGrid_
```

**`3527 - 3522 = 5`, and five is exactly the number of test cases an
adversarial review and a mutation analysis added after the launcher's first
draft.** `FREEZE.md` records that draft's figures as 6960 / 3522 / 17610. The
release-ext tree was built during the *pre-freeze* preset checks, before those
five cases existed, and its per-tag split says the same thing: 20 and 9
against the frozen tree's 24 and 10 — four of the five new cases are
`CfdTransient_` and one is `CfdTimeGrid_`, which is precisely the five the
freeze names. A green stage ran a superseded binary and said nothing about
the frozen tree.

The debug-ext repeat is the one real datum here: run 1 built that binary from
the frozen tree at 06:34:46 and nothing has rebuilt it since, so its
`3527 x 5 = 17635` executions at `100% tests passed` is a genuine
repeat-determinism result for debug-ext. It is still not a qualification —
that needs three presets configured, built and run in one sequence — and it is
not relied on below.

## What was changed, and what was not

**No product code changed**, and the harness is unchanged: `qualify.cmd`,
`run-qualification.cmd` and the rest are byte-for-byte the files `FREEZE.md`
records. An environment failure is answered by fixing the environment.

Three things were done before the third run:

1. **This run's eight log files were moved here.** `docs/` is outside the
   frozen surface, so the freeze survives — and the eight identities were
   re-read afterwards by `FREEZE.md`'s own scratch-index method and are
   unchanged.

2. **All three build trees were emptied except `_deps`** — 13 GB down to
   193 MB. This is not housekeeping. It removes `CTestTestfile.cmake` and
   every test binary, so a failed configure can no longer leave something
   behind for a repeat stage to run and report green. The failure mode
   described in the section above is now impossible rather than merely
   noticed. The fetched dependency sources are kept so the run does not
   depend on the network.

3. **All three presets were configured as a pre-flight**, from the shell the
   third run is launched from, and all three resolved the kernel correctly:

```text
debug-ext         exit 0   OpenCASCADE_DIR = <deps prefix>/lib/cmake/opencascade
release-ext       exit 0   OpenCASCADE_DIR = <deps prefix>/lib/cmake/opencascade
debug-shared-ext  exit 0   OpenCASCADE_DIR = <deps prefix>/lib/cmake/opencascade
                           Dependency prefix: .../bettercad-deps/gnu-16-mingw-amd64
```

Run 1 configured only `debug-ext`; the other two presets' configures had never
been exercised inside the harness. They have been now, before three and a half
hours were committed to them.

## The launch-environment lesson, and the open tension in it

The qualification must be launched from a shell that can see the dependency
prefix. The desktop app's interactive terminal cannot, so it is not a valid
launch environment for this harness, however convenient its longevity is.

That is in tension with `voided-run-1`'s own closing note, which moved the
launch *into* that tab because run 1 died at 63 minutes as a backgrounded
child of the agent's shell. Both cannot be satisfied by choosing a shell, so
the third run is launched from the agent's shell again — the only one that
configures — with the one variable that run 1 had and this run does not:
**nothing else is running.** A background `[structural]`/`[thermal]` test run
was in flight during run 1, alongside `ctest -j4` and the compile-fail groups,
each case its own compiler process; `0xC0000142` is a process that could not
start, which is what sustained resource exhaustion looks like. That is a
hypothesis, not a measurement, and it is recorded as one. If run 3 dies the
same way with nothing else running, it is an environment report for the owner
and not a product claim.

## What is committed here, and what is not

The two 3.79 MB repeat logs are **not** committed, for the same reason and by
the same practice as `voided-run-1/`: they are the output of two stages that ran
with no build behind them, and a green result off a binary left on disk is not
evidence about this tree.

```text
kept in full     README.md, qualification-times.txt, run-qualification.out,
                 run-qualification.err, and the three configure logs -- which
                 are the whole diagnosis, since all three configures failed in
                 under five seconds each with the same message
extracted        ctest-repeat-release-ext-extract.txt and
                 ctest-repeat-debug-ext-extract.txt, 31 lines each: ctest's own
                 summary, the number of tests SELECTED against the frozen
                 tree's 3527, the per-group case counts that date the binary,
                 and the first and last progress lines
not committed    the two ctest-repeat-*.log files themselves
```

The per-group counts are the ones that matter, because they are what identify
the binary rather than merely describe the run: 20 `unit.CfdTransient_` cases
and 9 `unit.CfdTimeGrid_` where the frozen tree has 24 and 10.
