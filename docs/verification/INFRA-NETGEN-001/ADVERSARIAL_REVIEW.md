# INFRA-NETGEN-001 — adversarial review

```text
SUBJECT:  the final diff making Netgen a qualified BetterCAD dependency
METHOD:   attempt to disprove the claim "Netgen is qualified", by attacking the
          evidence rather than re-reading the code approvingly
DEFECTS FOUND AND FIXED DURING REVIEW:  4
CARRIED FORWARD:                        3
```

The claim under attack is **"Netgen v6.2.2604 is a qualified BetterCAD
dependency: reproducibly buildable, runnable, testable and consumable."**

## Defects this review found, and fixed

### 1. The runtime closure was incomplete — the build linked and could not start

`cmake/FindNetgen.cmake` first expressed nglib's zlib dependency as
`ZLIB::ZLIB` from CMake's `FindZLIB`. That module produces an **UNKNOWN**
imported target whose `IMPORTED_LOCATION` is the *import library*, and
`$<TARGET_RUNTIME_DLLS>` ignores such a target. `libzlib.dll` was therefore
never deployed.

```text
built           cleanly, no warnings
linked          cleanly
ran             NOT AT ALL: exit 127 before main()
```

Every developer would have hit it. Fixed by declaring zlib as a SHARED
imported target inside `FindNetgen.cmake`, since zlib is part of *Netgen's*
runtime closure rather than a BetterCAD dependency. Re-verified: `libzlib.dll`
is now copied beside the test executable and the backend tests run.

**The error message actively misled.** Windows reported a missing
`api-ms-win-crt-time-l1-1-0.dll` — a UCRT apiset present on every Windows 10+
machine. The same thing happened earlier with
`api-ms-win-crt-string-l1-1-0.dll`. A DLL-load failure names an unreliable
culprit; the import table is the only honest source.

### 2. The qualified source tree depended on the developer's git config

`git apply` consults the user's **global** configuration even when applying
outside a repository. With `core.autocrlf=true` the patched files came out
CRLF; with it unset, LF. Two developers following identical instructions would
get byte-different Netgen source trees.

Caught by diffing the script-patched tree against the hand-patched tree that
was actually built, and finding them different. Fixed by pinning
`-c core.autocrlf=false -c core.eol=lf` on the command line. Re-verified
byte-identical.

This one matters out of proportion to its size: the whole point of a *pinned*
dependency is that everyone compiles the same bytes.

### 3. The containment rule had holes the new code walked straight through

`src/meshing/netgen/NetgenBackend.cpp` includes `netgen_version.hpp`, which
rule 5 of `tests/architecture/CheckLayering.cmake` did not match. The rule also
keyed `nginterface` on a `.h` extension, so `nginterface_v2.hpp` — a header
Netgen installs — escaped as well. Containment that misses two of the
backend's four public headers is not containment.

Fixed by extending the pattern, and a new fixture
(`architecture.checker.mesh-backend-generated-leak`) proves the rule now fires
on `netgen_version.hpp`. `mydefs.hpp` was deliberately left out and the reason
recorded in the rule.

The rule was **tightened**, never relaxed, which is the direction the gate is
allowed to move.

### 4. Netgen misreported its own version, and would have done so silently

Built from its release tarball, Netgen defines `NETGEN_VERSION "6.2.0"` while
in fact being 6.2.2604: `git describe` finds no metadata and
`find_package(Git REQUIRED)` aborts before the script's own fallback can run.
A pinned dependency that misnames itself defeats the pinning.

Fixed by patch `0004`, and `VolumeBackend_ReportsThePinnedNetgenVersion` now
asserts the value the built library reports, so it cannot regress unnoticed.

## The attack questions

```text
Q  Do the backend tests pass vacuously when no backend is present?
A  No. BETTERCAD_TESTS_EXPECT_NETGEN is defined by CMake exactly when it found
   and linked Netgen, so the build's intent and the binary's behaviour are
   compared against each other. A test that asks the library whether it is
   there and then asserts the answer cannot fail; this one can.

Q  Is the version assertion a tautology -- does it just echo the build?
A  No. It compares the library's own generated header against a literal, and
   before patch 0004 that header said 6.2.0 and the test would have failed.
   The failing case was observed, not imagined.

Q  Does anything prove the DLL LOADS, as opposed to merely linking?
A  volumeBackendResponds() calls Ng_Init/Ng_NewMesh/Ng_DeleteMesh/Ng_Exit, so
   the DLL and its whole closure must load. Demonstrated to be a real
   discriminator: with zlib missing, the executable linked and this failed.

Q  Were the mesh rules changed by linking makerls statically?
A  No. All 7 generated files are byte-identical to the dynamically linked
   build. This was checked precisely because the generator is the mesher.

Q  Could the build pick up Netgen's own CMake config, with its
   CMAKE_MSVC_RUNTIME_LIBRARY side effect and its absolute baked-in paths?
A  No. find_package(Netgen ... MODULE) forbids config mode outright, so
   NetgenConfig.cmake can never be used however the prefix is arranged.

Q  Is a prebuilt binary vendored anywhere?
A  No, and one was actively removed: Netgen's superbuild downloads a prebuilt
   MSVC zlib (imports VCRUNTIME140.dll) with no configure or build step.
   USE_SUPERBUILD=OFF plus a source zlib eliminates it. Verified from
   libnglib.dll's import table: it imports libzlib.dll, not zlib.dll.

Q  Does the build need the network for anything but hash-pinned source?
A  No. Two source archives, both URL_HASH SHA256. No binary downloads.

Q  Does the build need git?
A  Only as a patch tool. Netgen itself no longer does: a build with git
   removed from PATH entirely succeeds, which it could not before patch 0004.

Q  Can the patch step corrupt the source tree if it runs twice?
A  No. Verified idempotent: second application reports all four already
   present and exits 0. A patch that neither applies nor is already present is
   a FATAL_ERROR naming the file, never a silent partial application.

Q  Does the patch set actually reproduce what was qualified?
A  Yes, byte for byte, verified against a pristine extraction of the pinned
   tarball.

Q  Was any gate weakened, any tolerance moved, any test skipped?
A  No. No tolerance exists in this milestone, no test was disabled, and the
   one architecture rule that changed was made stricter.

Q  Does the public API leak the backend?
A  No. include/bettercad/meshing/VolumeBackend.hpp includes no Netgen header;
   Netgen::nglib is linked PRIVATE; and the architecture check enforces
   containment independently of the link line.

Q  Does the build still work for someone with no Netgen in their deps prefix?
A  Yes -- tested, not assumed, with BETTERCAD_VOLUME_MESHING=OFF. A different
   translation unit (NoVolumeBackend.cpp) is compiled, the API stays total,
   and the weak test still passes while the strong ones are not compiled.

Q  Does Debug differ from Release, or static from shared?
A  All three production presets were built and tested. The shared preset
   matters most here, because bettercad_meshing links an imported shared
   library privately.

Q  Is global backend state safe across repeated use?
A  Ng_Init/Ng_Exit were cycled 4 times in one process, in the standalone probe
   and again in VolumeBackend_SurvivesRepeatedInitialisation. ctest --repeat
   additionally re-runs each test in a fresh process.

Q  Were sanitizers used?
A  NO. This MinGW toolchain ships no libasan or libubsan. No sanitizer
   coverage is claimed for Netgen or for the probe. This is a real gap and is
   recorded as a known limitation rather than papered over.

Q  Is the determinism claim as strong as it sounds?
A  Partly. Byte-identical rule generation was shown across two builds with
   different linkage and different PATH, and USE_NATIVE_ARCH=OFF prevents
   instruction-set-dependent code generation. But both builds ran on ONE
   machine with ONE compiler. Cross-machine reproducibility is NOT
   established, and is not claimed.

Q  Does "qualified" mean the MESHER is numerically validated?
A  No, and the distinction matters. What is qualified is the dependency: it
   builds reproducibly, loads, runs, reports itself correctly and is
   consumable. Whether Netgen produces CORRECT tetrahedral meshes of BetterCAD
   bodies is P16-VOL-001's question and is answered by nothing here.
```

## Carried forward

```text
1. nglib returns NG_OK on an unmeshable surface.
   The standalone probe fed it an OPEN surface (a tetrahedron missing a face).
   nglib printed "Meshing of domain 1 failed" and still returned 0, with zero
   tetrahedra. P16-VOL-001's adapter MUST NOT trust the return code: it has to
   check the element count and validate the result. Recorded here because it
   is the kind of thing an adapter author assumes the other way round.

2. No sanitizer coverage on this toolchain.
   No libasan/libubsan for WinLibs MinGW. If Netgen is ever to be run under
   ASan/UBSan it needs a different toolchain, and that is worth doing before
   P16-VOL-001 trusts its output numerically.

3. Cross-machine reproducibility unproven.
   Everything was built on one machine. The inputs are hash-pinned and the
   build avoids -march=native, so there is good reason to expect
   reproducibility, but reason is not evidence.
```

## Result

```text
RESULT:  PASS
         Four defects were found by this review and all four are fixed and
         re-verified. Three limitations are carried forward explicitly, none
         of which contradicts the qualification claim as stated.
```
