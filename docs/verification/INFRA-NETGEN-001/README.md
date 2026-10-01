# INFRA-NETGEN-001 — Netgen Toolchain Qualification

```text
STATUS:   PASS -- Netgen v6.2.2604 is QUALIFIED
TASK:     INFRA-NETGEN-001 -- make the approved volume-meshing backend
          reproducibly buildable, runnable, testable and consumable by
          BetterCAD under the qualified Windows/MinGW toolchain
PHASE:    P16 -- Meshing (infrastructure)
DATE:     2026-10-01
```

The brief required one of two conclusions and forbade an ambiguous middle. The
conclusion is **A: Netgen v6.2.2604 is QUALIFIED.**

It also required that the earlier judgement be revisited honestly. It has been:
**the `P16-VOL-001` BLOCKED report was wrong.** Netgen has no defect that GCC 16
MinGW exposes. The crash and hang that blocked it were caused by the
investigating environment loading a mismatched C++ standard library, and the
full analysis — including the evidence that distinguishes the two explanations
and the hypothesis that was tested and rejected — is in
[MAKERLS_ROOT_CAUSE.md](MAKERLS_ROOT_CAUSE.md).

```text
Netgen builds                 from pinned source, with 4 patches, no git needed
Netgen runs                   meshes a tetrahedron and a cube; stable on repeat
Netgen is reproducible        hash-pinned source; patch set reproduces the
                              qualified tree byte for byte; no binary downloads
Netgen is consumable          Netgen::nglib imported target, confined to
                              src/meshing/netgen/ by the architecture check
Netgen identifies itself      6.2.2604, read from the library, asserted by a test
```

## Baseline

```text
branch        main
HEAD at start 1347283  BetterCAD: record P16-VOL-001 blocked on the Netgen build
working tree  clean
build root    C:/Users/uqhas/AppData/Local/bc-build
deps prefix   C:/Users/uqhas/AppData/Local/bettercad-deps/gnu-16-mingw-amd64
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT r4), C++23
CMake/Ninja   4.4.2 / 1.13.2
OCCT          8.0.1
```

## What was admitted

```text
Netgen v6.2.2604  LGPL-2.1  sha256 0a614193ee6106c0c276a1516639bef872f2f2371dec159e1b1f6ffcb425954c
zlib   1.3.1      Zlib      sha256 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
```

Pinned by tag *and* hash in `deps/CMakeLists.txt`, beside Qt and OCCT. Full
closure, licence position and what Netgen does **not** drag in:
[DEPENDENCY_AUDIT.md](DEPENDENCY_AUDIT.md).

**A prebuilt binary was removed, not added.** Netgen's superbuild downloads a
prebuilt MSVC zlib — no configure step, no build step — which imports
`VCRUNTIME140.dll` and would put two C++ runtimes in one process.
`USE_SUPERBUILD=OFF` plus a zlib built from source eliminates it, and with it
the build-time download of prebuilt OCC and Tcl binaries from the same
third-party release page. Nothing BetterCAD links is downloaded as a binary.

## The patches

Four, 47 changed lines, all general MinGW/portability fixes with no BetterCAD
content, in `deps/patches/netgen/`. Detail and rationale:
[PATCHES.md](PATCHES.md).

```text
0001  MSVC-only flags (/bigobj, /MP, /ignore:) applied under if(WIN32)
0002  GetProcAddress returns FARPROC; GCC requires the cast
0003  two header-DEFINED functions marked NGCORE_API, i.e. dllimport
0004  built from a tarball, Netgen reports itself as 6.2.0 when it is 6.2.2604,
      and find_package(Git REQUIRED) kills its own fallback before it can run
```

Applied by `deps/ApplyNetgenPatches.cmake` from ExternalProject's patch step.
Idempotent, fails loudly on drift, and pins `core.autocrlf`/`core.eol` so the
patched tree does not depend on the developer's git configuration.

```text
applied to a pristine extraction of the pinned tarball   4/4 applied
applied again to the same tree                           4/4 skipped, exit 0
resulting tree vs the tree that was built                 BYTE-IDENTICAL
```

## Netgen runs

A standalone probe against the built `nglib`, before any BetterCAD code existed
to call it:

```text
case                     result    nodes  tets   verdict
closed tetrahedron       NG_OK     4      1      PASS
closed cube              NG_OK     8      6      PASS
OPEN surface (3 faces)   NG_OK     4      0      PASS -- process survived
repeated lifecycle x4    NG_OK     4      1      PASS -- identical every run

smoke failures: 0
```

**The third row is a finding, not a pass to be pleased about.** Given a
non-watertight surface, nglib printed `Meshing of domain 1 failed` and
**returned NG_OK with zero tetrahedra**. `P16-VOL-001`'s adapter must not trust
the return code; it has to check the element count and validate the result.
Carried forward in [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

### Determinism of the mesh rules

The `.rls` mesh-rule files *are* the mesher, and they are produced at build time
by `makerls`. Since `makerls` is now linked statically, its output had to be
shown unchanged by that:

```text
build A   makerls dynamically linked, correct runtime placed beside it
build B   makerls statically linked, deliberately restricted PATH, no git

rule_tetrules.cpp 28274   rule_triarules.cpp 8399   rule_quadrules.cpp 15961
rule_hexrules.cpp  4171   rule_prismrules2.cpp 7557 rule_pyramidrules.cpp 4390
rule_pyramidrules2.cpp 5599

byte-identical: 7 / 7
```

`USE_NATIVE_ARCH=OFF` keeps `-march=native` out, so the result does not depend
on the build machine's instruction set. Both builds were nonetheless on **one**
machine; cross-machine reproducibility is not claimed.

## Integration into BetterCAD

```text
deps/CMakeLists.txt            zlib + netgen as hash-pinned ExternalProjects
deps/ApplyNetgenPatches.cmake   idempotent patch step
deps/patches/netgen/*.patch     the four patches
cmake/FindNetgen.cmake          Netgen::nglib / ::ngcore / ::zlib
cmake/BetterCADDependencies.cmake  AUTO/ON/OFF discovery
cmake/BetterCADSummary.cmake    reports the backend at configure time
CMakeLists.txt                  BETTERCAD_VOLUME_MESHING option
include/bettercad/meshing/VolumeBackend.hpp   presence + liveness API
src/meshing/netgen/NetgenBackend.cpp          the ONLY file that may include a
                                              backend header
src/meshing/NoVolumeBackend.cpp               compiled instead when absent
tests/meshing/VolumeBackendTests.cpp          5 tests
tests/architecture/CheckLayering.cmake        rule 5 widened
tests/architecture/fixtures/mesh-backend-generated-leak/   proves it fires
```

`find_package(Netgen ... MODULE)` is deliberate: Netgen's own
`NetgenConfig.cmake` sets `CMAKE_MSVC_RUNTIME_LIBRARY` in the *consumer's*
scope and bakes the build machine's absolute source path into the installed
file. MODULE mode makes it unreachable however the prefix is arranged.

`BETTERCAD_VOLUME_MESHING` is AUTO, not required: the only milestone that needs
a volume mesher is `P16-VOL-001`, which is not implemented, and a checkout whose
deps prefix predates Netgen must still build. **In this qualification it was ON
in all three presets** — `Volume meshing : ON (Netgen 6.2.2604)` — so the
backend tests were compiled and executed, not skipped.

### Why the probe is permanent rather than a throwaway script

The backend is resolved at configure time, loaded from a DLL at run time, and
built by another project. Every one of those steps can break with no BetterCAD
source change. `volumeBackendResponds()` calls `Ng_Init` / `Ng_NewMesh` /
`Ng_DeleteMesh` / `Ng_Exit`, so it forces the DLL **and its whole runtime
closure** to load — which a compile-and-link check does not. That distinction
was not theoretical: with `libzlib.dll` undeployed, the test executable linked
perfectly and would not start.

The probe meshes nothing. Volume meshing is `P16-VOL-001`'s.

## Tests

```text
unit.VolumeBackend_IdentityIsConsistentWithAvailability
unit.VolumeBackend_IsNetgenWhenTheBuildLinkedIt
unit.VolumeBackend_ReportsThePinnedNetgenVersion
unit.VolumeBackend_InitialisesAndShutsDownCleanly
unit.VolumeBackend_SurvivesRepeatedInitialisation
architecture.checker.mesh-backend-generated-leak
```

`BETTERCAD_TESTS_EXPECT_NETGEN` is defined by CMake exactly when it found and
linked Netgen, so the build's intent is compared against the binary's
behaviour. A test that asks the library whether it is present and then asserts
whatever it answered cannot fail; these can.

`VolumeBackend_ReportsThePinnedNetgenVersion` is a regression test for a real
defect: before patch `0004` the library reported `6.2.0` and this test fails.

Both configurations were built and run, so the conditional compilation is
verified rather than assumed:

```text
BETTERCAD_VOLUME_MESHING=AUTO, Netgen present   configure: ON (Netgen 6.2.2604)
                                                build exit 0, 5 tests, 5 passed

BETTERCAD_VOLUME_MESHING=OFF                    configure: OFF
                                                build exit 0, 1 test, 1 passed
```

One test in the second case, not five, and that is the point: the four strong
assertions are not compiled when there is no backend to assert about, and the
one that remains checks that the API tells the truth about its own absence.
`NoVolumeBackend.cpp` is compiled in its place, so
`bettercad::meshing::volumeBackend()` is defined either way and no caller
branches on configuration.

## Independent validation

```text
the version              read from Netgen's own generated header, compared with
                         the tag pinned in deps/CMakeLists.txt -- two
                         independent places that must agree
the mesh rules           compared byte for byte between two independently
                         configured builds, not against an expectation
the runtime closure       read from libnglib.dll's import table with objdump,
                         never inferred from documentation
zlib's provenance        read from zlib.dll's import table: the downloaded one
                         imports VCRUNTIME140.dll, ours does not
the patch set            applied to a pristine extraction and diffed against
                         the tree that was actually built
the tetrahedron           1 tetrahedron from 4 points and 4 faces; the cube's 6
                         from 8 points and 12 triangles -- both hand-checkable
```

## Regression

Full qualification, `qualification/qualify.cmd`, harness carried unchanged from
`P15-QUAL-001` (its own regression, `verify-harness.cmd`, was re-run first and
passed). Each preset is configured, has **every** build output removed, is
rebuilt with warnings as errors, and only then runs CTest — unfiltered.

```text
preset             build   warnings  ctest                    time
debug-ext          exit 0  0         2937/2937 passed (100%)  917.65 s
release-ext        exit 0  0         2937/2937 passed (100%)  993.68 s
debug-shared-ext   exit 0  0         2937/2937 passed (100%)  1040.31 s

repeat release-ext   116 tests selected, x5, exit 0
repeat debug-ext     116 tests selected, x5, exit 0

stages failed: 0          qualify.cmd exit 0
started 19:01:11   finished 20:56:09   2026-10-01
```

```text
test count   2931 (P16-SURF-001) -> 2937, exactly +6:
             5 x unit.VolumeBackend_*
             1 x architecture.checker.mesh-backend-generated-leak
executions   2937 x 3 presets + 116 x 5 x 2 repeat presets = 9971
```

The repeat set of 116 is the blast radius, not the new tests alone: the whole
meshing module, every surface-mesh test, and all 10 architecture tests, because
this change touched `src/meshing/`, the architecture rules and the CMake that
configures both.

### Qualified tree == committed tree

The harness fingerprints the source before the first build and after the last
test, from a scratch index over the working tree:

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           e3c8e796621133e9a2b2e34dfed33eb0859bf348
src               7c9c138608664979c7500eb96734228387de08a2
tests             bb78903165dc27426e13a1774935d9e025e77dfb
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Identical before and after: nothing moved under the qualification. These are the
trees that were committed.

**And, outside the 8-path fingerprint**, the dependency inputs this milestone
introduces — the patches and the superbuild that applies them:

```text
deps              b88c6c6dba07ac9c7e291f6bee61b5010c85c17b
```

Recorded here because a patch file now helps determine a linked binary while
sitting outside the harness's fingerprint. See **Known limitations**.

## Clean checkout

The **git index** — byte for byte what the commit contains, not a copy of the
working directory — archived into a directory that had never been built in.
A file needed by the build but never added to git would simply be absent.
Detail: [CLEAN_CHECKOUT.md](CLEAN_CHECKOUT.md).

```text
tree 48b138f5523a64c62df79af2ae510788cfecfeb7, 2962 files
configure  exit 0   "Volume meshing : ON (Netgen 6.2.2604)"
build      exit 0   0 warnings (warnings are errors)
ctest      7/7 passed
```

A tree that had never seen this session's build directories found Netgen
through `cmake/FindNetgen.cmake` alone and read `6.2.2604` out of the
library's own header. The dependency *prefix* was not rebuilt a second time,
so byte-level reproducibility of the Netgen binaries is not claimed; what is
shown for them is that the patch set reproduces the qualified source tree
exactly and that the mesh rules generate byte-identically.

## Adversarial review

**Four defects found by the review itself, all fixed and re-verified**; three
limitations carried forward. Full account:
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
1  runtime closure incomplete -- built, linked, and could not start
2  the qualified source tree varied with the developer's git config
3  the containment rule missed two of the backend's four public headers
4  Netgen misreported its own version, silently
```

## Known limitations

```text
no sanitizer coverage      this MinGW ships no libasan/libubsan. NONE is
                           claimed, for Netgen or for the probe.

one machine only           determinism was shown across two builds on one
                           machine with one compiler. Hash-pinned inputs and
                           USE_NATIVE_ARCH=OFF are good reasons to expect
                           cross-machine reproducibility; they are not
                           evidence of it.

the MESHER is not validated  what is qualified is the DEPENDENCY: it builds,
                           loads, runs, identifies itself and is consumable.
                           Whether Netgen produces CORRECT tetrahedral meshes
                           of BetterCAD bodies is P16-VOL-001's question and
                           nothing here answers it.

deps/ is outside the source fingerprint  the qualification fingerprints apps,
                           include, src, tests, examples, cmake, CMakeLists.txt
                           and CMakePresets.json. A patch file now helps
                           determine a linked binary, so this milestone also
                           records the deps/ tree hash below. The gap is
                           pre-existing in kind -- a dependency prefix rebuilt
                           differently was never detectable either -- and
                           widening the shared harness mid-milestone was
                           judged the wrong trade.

install tree carries no backend DLL  consistent with OCCT today, whose DLLs
                           are not installed either. Only the MinGW runtime is.

the install prefix gains share/netgen   Netgen installs its own example
                           geometry and manual unconditionally. Unused,
                           unreferenced, not redistributed.
```

## Result

```text
RESULT:     PASS -- conclusion A. Netgen v6.2.2604 is QUALIFIED.
BACKEND:    unchanged. Netgen was not swapped, no prebuilt binary was
            vendored, and the licence decision stays untouched and the owner's.
CORRECTED:  the P16-VOL-001 BLOCKED report. Netgen builds; the fault was the
            investigating environment's.
NOT DONE:   P16-VOL-001. Deliberately not started -- the brief forbids it in
            this run, and authorization comes from TODO.md alone.
```
