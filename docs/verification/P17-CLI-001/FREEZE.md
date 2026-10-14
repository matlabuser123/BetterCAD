# P17-CLI-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: 7e1817518872611079af3483d322f636ff0cffb8  (the parent commit)
           plus the working tree: 5 CLI files modified, 2 CLI sources and
           2 test files new, tests/CMakeLists.txt and ZeroMatchGuard.cmake
           modified -- git status --short and git diff --check (clean)
           recorded before the launch
RAN:       Thu 15/10/2026 05:01:39 -> 08:02:22  =  3h00m43s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
MACHINE:   the hasib machine -- see "Where this ran" below. Every
           qualification before this one ran on the uqhas machine.
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **3949 / 3949** | **3949 / 3949** | **3949 / 3949** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| objects | 676 / 676 | 676 / 676 | 676 / 676 |
| configure | 3.8s | 3.5s | 3.1s |
| clean | exit 0 | exit 0 | exit 0 |
| build | 19m53s | 24m23s | 20m37s |
| no-op rebuild | exit 0 | exit 0 | exit 0 |
| ctest | 29m23s (1763.45 s) | 27m57s (1677.28 s) | 30m16s (1815.88 s) |

```text
repeat release-ext   1078 tests x 5   13m54s (833.34 s)   exit 0
repeat debug-ext     1078 tests x 5   13m55s (834.84 s)   exit 0
TOTAL                                 3h00m43s
```

Every count **measured, not predicted**, and each reconciles against the
P17-PERSIST-001 run plus what this milestone added:

```text
suite    3921 -> 3949   +28:  19 unit.StructuralCli_* cases in process,
                              9 cli.structural.* fresh-process tests, of
                              which one is the 28-step multi-process workflow
repeat   1050 -> 1078   +28:  THE FILTER IS UNCHANGED. `unit\.Structural`
                              already catches unit.StructuralCli_* as a
                              substring, and `cli\.structural\.` was in the
                              filter already. compile_fail stays out of the
                              repeat set for the reason every milestone
                              records: each case is a real compilation.
```

The no-op rebuild in every preset re-ran exactly one edge, "Checking git
revision", which is the always-run step that stamps the binaries; nothing
was recompiled or relinked, so the binaries that ran the tests are the ones
the build produced.

## The tree that was qualified is the tree that was committed

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175
include            c9696d26cd8583a90cdddd00a51ee654fc3ba72b
src                f3c1245dabbfda68008db3660f3d15a73e8e607f
tests              d8a2e79db1be3f116b343a8028e56132d6d3c0bc
examples           776d36875941b214081765680efa3da6c996793a
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**Before == after**, taken by qualify.cmd from a scratch index at 05:01:39
and again at 08:02:22. **Six of the eight are byte-identical to the
P17-PERSIST-001 freeze** -- `include`, `src`, `examples`, `cmake`,
`CMakeLists.txt`, `CMakePresets.json` -- which is the fingerprint-level proof
of the README's claim that not one core file changed. Only `apps` and `tests`
moved, and those are the milestone.

The closeout commit's own subtree hashes are compared against these eight
values after the commit (`git write-tree --prefix=<dir>/` on the committed
index); the comparison is recorded in the commit's TODO.md entry.

## Where this ran, and what is identical

```text
MACHINE                the hasib machine, Windows 11 Pro 10.0.26300,
                       Ryzen 7 5800H, 16 logical cores, 32 GB
TOOLCHAIN              GCC 16.1.0, WinLibs POSIX UCRT r4 -- the revision
                       INFRA-NETGEN-001 records -- installed by winget to the
                       same package path pattern the uqhas logs show; the
                       CMake 4.4.1 and Ninja 1.13.2 bundled with it
DEPENDENCY PREFIX      built by deps/CMakeLists.txt on this machine from the
                       same hash-verified archives: Qt 6.11.2, OCCT 8.0.1,
                       zlib 1.3.1, Netgen 6.2.2604 (four patches applied by
                       MinGit 2.56 as the patch tool)
CONFIGURE SUMMARY      identical to the uqhas machine's configure log line
                       for line, except the three paths below
```

Three things differ, each recorded in `qualification/run-qualification.cmd`
beside the invocation:

```text
BETTERCAD_BUILD_ROOT     C:/Users/hasib/bcdev/bc-build
BETTERCAD_DEPS_PREFIX    C:/Users/hasib/bcdev/bettercad-deps/gnu-16-mingw-amd64
                         (cache). The %LOCALAPPDATA% default could not be
                         used: a shell launched from the packaged Claude
                         desktop app has its AppData\Local writes
                         virtualised into the app's container, which Qt's
                         build-path check refuses and no other shell sees.
FETCHCONTENT_SOURCE_DIR_CATCH2 / _NLOHMANN_JSON / _EIGEN3   (cache)
                         archives fetched outside CMake and verified against
                         the SHA256 in cmake/BetterCADDependencies.cmake,
                         because the bundled CMake's curl has no CA trust
                         anchors -- carried constraint 7, reproduced here.
```

The debug-ext tree was built and fully tested once before the harness run
(3949/3949, 1768 s) as the proof the environment works. That run is not
evidence; it was discarded by the harness's own `--target clean`.

## The fresh-binary proof, per preset

```text
preset             absolute CLI binary                                   sha256
debug-ext          C:/Users/hasib/bcdev/bc-build/debug-ext/bin/          a3bfc89d40e55ab8dc9aee4ec09804337a04217e32e7b830c244bc38be00b5b9
                   bettercad-cli.exe   368 170 949 bytes
release-ext        C:/Users/hasib/bcdev/bc-build/release-ext/bin/        bb1d41e76014680d2f78981d6f855536d4c11a0a830d682e2724b8b781a27416
                   bettercad-cli.exe    10 113 338 bytes
debug-shared-ext   C:/Users/hasib/bcdev/bc-build/debug-shared-ext/bin/   1ec403d5403aed25d933c81ee4eabcb1e0ddb87baf11db386c925b7c774e1b47
                   bettercad-cli.exe    41 310 182 bytes

embedded git revision, all three    7e1817518872, dirty = true
```

The path is `$<TARGET_FILE:bettercad_cli>` in every process test and in the
workflow, so a PATH lookup is impossible by construction; no installed copy of
BetterCAD exists on this machine (`install` was never run, and the dependency
prefix holds no BetterCAD binary). **Runtime library origin for
debug-shared-ext:** the ten `libbettercad_*.dll` modules sit beside the CLI
in its own `bin/`, put there by `bettercad_copy_runtime_dlls`, with the Qt,
OCCT and compiler runtime DLLs; there is no other copy of any of them on this
machine for the loader to find first.

## Zero-match protection

| filter | minimum | discovered | executed | PASS |
| --- | --- | --- | --- | --- |
| `unit\.StructuralCli` | 15 | 19 | 19 | ✓ in all three presets |
| `cli\.structural\.` | 8 | 9 | 9 | ✓ in all three presets |
| the whole suite | — | 3949 | 3949 | ✓ in all three presets |

Discovered with `ctest -N` after the run; executed by counting the result
lines in each preset's ctest log. `cli.mesh.zero-match-guard`, which proves
the counter itself against an impossible filter, passed in all three. The
repeat filter's selected count is the harness's own, 1078, recorded in
`qualification-times.txt`.

## Cross-preset equivalence

The same document, `examples/models/analysed_plate.bcad`, solved by each
preset's own CLI after the run:

```text
debug-ext          structural-solve --json   740 bytes   sha256 65e9e5d121cb3ac1...
release-ext        structural-solve --json   740 bytes   sha256 65e9e5d121cb3ac1...
debug-shared-ext   structural-solve --json   740 bytes   sha256 65e9e5d121cb3ac1...
```

**Byte-identical across all three presets**, not merely within a tolerance:
81 nodes, 127 tetrahedra, 123 free equations, residual 9.914e-18, largest
von Mises 2 359 827.94 Pa. There was no need for the floating tolerance the
brief offers, which is worth saying because it was available.

## Determinism

The repeat stages: 1078 tests, five times each, in release-ext and in
debug-ext, exit 0 both. In process,
`StructuralCli_ARepeatedCommandGivesTheSameBytes` requires byte-identical
stdout over four runs of every report, and the workflow requires two fresh
processes to print the same solve payload; both passed in all three presets.

## Headless smoke on a refined document

`mesh-set-global-size 3mm` on a copy of the fixture, then
`structural-solve --json` from release-ext: exit 0 in 0.4 s, 745 bytes of
clean JSON, status accepted. **It is not a large model**, and this records
why rather than claiming one: the plate's OCCT surface triangulation gives
a planar face two triangles whatever the deflection (P16-REFMOD-001's
measurement), so a smaller global size leaves 81 nodes and 127 tetrahedra on
this geometry. The large-mesh coverage this milestone inherits is
P17-ASSEMBLY-001's in-process 850-node, 4210-tetrahedron case; the CLI adds no
path of its own to it, which is the point of the equivalence tests.

## One thing that went wrong during the environment proof, before the run

The first ctest on the freshly built debug-ext tree was contaminated by my
own `ctest -N` queries, which raced Catch2's pre-test discovery and left
every test listed twice (7412 listed, 3788 unique). That run was stopped, its
log set aside under a `.contaminated` name outside the repository, the two
generated discovery files set aside, and the suite re-run from a regenerated
list of exactly 3949. Nothing of it reached the harness, which cleans and
rebuilds every tree, and no `ctest` query touched any tree while the harness
ran. The lesson is recorded in the launcher's comments: never query a tree
with ctest while a run on it is starting.
