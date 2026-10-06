# P17-DATA-001 — the qualification freeze

```text
SUBJECT:  the tree that was qualified, recorded before the first build
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction.
```

## The frozen candidate

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            70d61d81326250119f2c468dd4ebd1749e8cfe42
src                3e2a39aebf8e47f691fe0fab578e13249d0711cf
tests              0169449c65ab89d071fce07df3d726c0ff0e2fb0
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

WHOLE FINGERPRINT  fcf2ea160bbe3cad9cb94452570b9dac1a517f69
```

## What moved, against P17-ARCH-001's qualified tree

```text
apps      b4972247  unchanged   examples  75ce1c68  unchanged
cmake     5a382115  unchanged   CMakeLists.txt / CMakePresets.json unchanged

include   b7aeeba2 -> 70d61d81   core/Id.hpp (3 tags, 3 aliases, 1 widening),
                                 core/math/Vector.hpp (Force3D and its
                                 operators), and three new structural headers
src       47a0eb3a -> 3e2a39ae   three new structural sources and their
                                 registration
tests     bb79c89b -> 0169449c   one new test file, one new compile-fail file
                                 with 12 cases, and both registrations
```

Three paths of eight. **Two of the changed headers are included by almost
everything**, which is why the repeat set is sized for the reach and not for
the diff: `core/Id.hpp` and `core/math/Vector.hpp`. Both changes are purely
additive -- no existing declaration moved, no semantics changed, nothing
removed -- but additive to a universally included header is still a change
whose blast radius is total.

## Pre-freeze checks, run BEFORE the expensive one

In this order deliberately: all three of P15's voided qualifications came from
running a cheap check after an expensive one and finding a defect that
invalidated it.

```text
git diff --check                 clean (CRLF notices are .gitattributes'
                                 `* text=auto eol=lf` normalisation)
over-100-column lines            0 added to tests/CMakeLists.txt (107 before and
                                 after) and to tests/compile_fail/CMakeLists.txt
                                 (43 before and after); 1 added to Vector.hpp,
                                 matching the identical form Translation3D::along
                                 already uses
export macro on a header-defined
  constexpr/inline               none. describesTheModel is deliberately
                                 unexported, for the reason P16 recorded: the
                                 macro becomes dllimport on an inline function
                                 and GCC rejects it -- a defect that appears
                                 ONLY in debug-shared-ext
the SHARED build                 configure 0, build 0, 0 warnings, 10 DLLs.
                                 The structural tests pass ACROSS the DLL
                                 boundary: 1116 assertions in 31 cases,
                                 IDENTICAL to debug-ext's
the new tests under --repeat     45 tests x 5, 100% passed, 14.98 s
  until-fail:5
zero-match protection            StructuralData 21, compile_fail.structids 12,
                                 StructuralInput 10, architecture 14 -- each
                                 checked with -N before running
```

No DLL-boundary defect, which is worth recording because three of the previous
four milestones each found one here.

## Blast-radius note

The repeat set is **715 tests**, chosen because `core/Id.hpp` changed:

```text
architecture             containment over 453 files + 13 checker self-tests
unit\.Structural         this milestone's 21 and P17-ARCH's 10
unit\.Id                 core's own identity tests -- what the Id.hpp change
                         could break
unit\.Unit, unit\.Quantity  the units framework Vector.hpp builds on
unit\.Mesh, unit\.Volume, unit\.Siz   the mesh contracts provenance depends on
unit\.Material, unit\.Mass            P15's material contract and its other
                                      consumer
unit\.ReferenceModel     the twelve mechanical parts, which exercise ObjectId
                         and the ID machinery hardest
unit\.Analytic, refmod\., gui\.
```

`compile_fail` is excluded from the repeat and the reason is specific: this
milestone adds twelve compile-fail cases and they are the proof of the identity
model, but each is a real COMPILATION, so repeating one measures the compiler's
determinism and not BetterCAD's. Forty-seven identity-related cases over five
rounds in two presets would cost about three quarters of an hour and report
nothing. They run UNFILTERED in all three presets, which is where a
compile-fail regression is caught.
