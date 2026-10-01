# INFRA-NETGEN-001 — clean-checkout reproducibility

```text
SUBJECT:  does a fresh checkout, with nothing from this session's build trees,
          configure, build and pass?
RESULT:   YES
DATE:     2026-10-01
```

## What was tested, and why it is the index rather than the working tree

A copy of the working directory would prove only that the files on this disk
build. It would not catch the commonest packaging failure: a file that the
build needs and that nobody added to git. So the test archives the **git
index** — byte for byte the content the commit will contain — into a directory
that has never been built in:

```text
git write-tree                 -> 48b138f5523a64c62df79af2ae510788cfecfeb7
git archive <that tree> | tar -x -C C:/Users/uqhas/AppData/Local/bc-clean
files extracted                   2962
```

**This test was run twice, and the first run did not count.** `git write-tree`
writes the *index*, and the index had been staged before two later edits, so
the first run built stale copies of `cmake/FindNetgen.cmake` and
`deps/CMakeLists.txt`. Both differences were comment-only — a moved comment
block and one `#` separator — so the first result was almost certainly still
valid. "Almost certainly" is not what this test is for, so it was re-staged
and re-run against the tree actually being committed. Only the second run is
reported below.

(The qualification itself was never affected: its freeze happened after those
edits, and its before/after fingerprint matches the committed tree on all
eight paths.)

A file left untracked would simply be absent from that tree, and the build
would fail. Everything this milestone adds is present:

```text
cmake/FindNetgen.cmake
deps/ApplyNetgenPatches.cmake
deps/patches/netgen/0001-mingw-msvc-only-flags.patch
deps/patches/netgen/0002-mingw-getprocaddress-cast.patch
deps/patches/netgen/0003-mingw-dllimport-inline-defs.patch
deps/patches/netgen/0004-version-without-git.patch
include/bettercad/meshing/VolumeBackend.hpp
src/meshing/NoVolumeBackend.cpp
src/meshing/netgen/NetgenBackend.cpp
tests/meshing/VolumeBackendTests.cpp
tests/architecture/fixtures/mesh-backend-generated-leak/...
```

## Result

```text
configure   exit 0, 41.4 s     "Volume meshing : ON (Netgen 6.2.2604)"
build       exit 0             0 warnings (warnings are errors)
ctest       7/7 passed         5 x unit.VolumeBackend_*
                               architecture.checker.mesh-backend-leak
                               architecture.checker.mesh-backend-generated-leak
```

The configure line is the one that matters most: a checkout that had never
seen this session's build trees found Netgen through `cmake/FindNetgen.cmake`
alone, in the shared dependency prefix, and read `6.2.2604` out of the
library's own generated header.

## What this does and does not establish

```text
ESTABLISHED
  the committed file set is complete -- nothing the build needs is untracked
  a fresh source tree configures, builds warning-free and passes
  Netgen is discovered from the dependency prefix with no local setup, no
    PATH entry and no manual copying
  the backend tests are compiled and RUN, not skipped

NOT ESTABLISHED
  that the DEPENDENCY PREFIX rebuilds identically from nothing. The prefix
  used here was built earlier in this session by deps/CMakeLists.txt, from a
  clean build directory, from hash-pinned source -- but it was not rebuilt a
  second time for this test, so byte-level reproducibility of the Netgen
  binaries themselves is not claimed. What IS shown for them is that the
  patch set reproduces the qualified SOURCE tree byte for byte, and that the
  mesh rules generate byte-identically across two independent builds.

  cross-machine reproducibility. One machine, one compiler.
```

## Reproducing it

```text
git archive <tree> | tar -x -C <empty dir>
cmake -S <empty dir> -B <build dir> -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DBETTERCAD_WARNINGS_AS_ERRORS=ON
cmake --build <build dir>
ctest --test-dir <build dir> -R "VolumeBackend|mesh-backend"
```

`BETTERCAD_BUILD_ROOT` must be set when `BETTERCAD_REQUIRE_EXTERNAL_BUILD_ROOT`
is on; the first attempt at this test failed for exactly that reason, which is
the check doing its job rather than a defect.
