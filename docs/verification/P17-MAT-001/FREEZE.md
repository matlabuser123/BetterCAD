# P17-MAT-001 — the qualification freeze

```text
SUBJECT:  the tree that was qualified, recorded before the first build
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over the eight paths. docs/ and deps/ are outside it.
```

## The frozen candidate

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            9cb0ee3b279afea572e3d8c56f3dac62cf53ff5d
src                7e262539eb737df2699ab6bff080a016f64dda84
tests              1140337fb3f41a2fc1d94dbf203701744fd6eaaa
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

WHOLE FINGERPRINT  4d4698b3d0220e5fa490104c7fc6110d9d9443a1
```

## What moved, against P17-DATA-001's qualified tree

```text
apps      b4972247  unchanged   examples  75ce1c68  unchanged
cmake     5a382115  unchanged   CMakeLists.txt / CMakePresets.json unchanged

include   70d61d81 -> 9cb0ee3b   StructuralMaterial.hpp (new), the analysis
                                 mode in StructuralData.hpp, and the mode field
                                 on StructuralAnalysisDefinition
src       3e2a39ae -> 7e262539   StructuralMaterial.cpp (new), its registration,
                                 and toString(StructuralAnalysisMode)
tests     dfbb3003 -> 1140337f   StructuralMaterialTests.cpp (new) and its
                                 registration, plus the corrected size
                                 assertion in StructuralDataTests.cpp
```

Three paths of eight, and **no header outside `structural/` changed this time** —
unlike P17-DATA-001, which touched `core/Id.hpp` and `core/math/Vector.hpp`. So
the blast radius is the structural module plus whatever reads the analysis
definition, which is the structural module.

The one cross-milestone edit is worth naming: adding the mode to
`StructuralAnalysisDefinition` broke P17-DATA-001's
`sizeof(definition) == sizeof(MeshControlId)` assertion, which was doing its
job. It is now a bound plus trivial copyability; see `ADVERSARIAL_REVIEW.md`.

## Pre-freeze checks, run BEFORE the expensive one

```text
git diff --check                 clean
over-100-column lines            2 found and wrapped; 0 remain in the new
                                 sources and tests
export macro on a header-defined
  constexpr/inline               0 in the new header
the SHARED build                 configure 0, build 0, 0 warnings, 10 DLLs.
                                 The structural tests pass ACROSS the DLL
                                 boundary: 1994 assertions in 50 cases,
                                 IDENTICAL to debug-ext's
the new tests under --repeat     64 tests x 5, 100% passed, 20.81 s
  until-fail:5
architecture.layering            455 files, 0 violations
zero-match protection            StructuralMaterial 19, StructuralData 21,
                                 StructuralInput 10, compile_fail.structids 12,
                                 architecture 14 -- each checked with -N first
```

No DLL-boundary defect, as in the previous two milestones.

## The repeat set

**734 tests.** The structural module and everything its material path consumes:

```text
architecture             containment over 455 files + the checker self-tests
unit\.Structural         this milestone's 19, P17-DATA's 21, P17-ARCH's 10
unit\.Material           P15's material resolution, assignment, custom
                         materials, completeness and provenance -- the layer
                         this milestone delegates every validation to
unit\.Mass               the OTHER consumer of the same material contract,
                         included so a change to it cannot be seen from one
                         side only
unit\.Mesh, unit\.Volume, unit\.Siz
                         the mesh contracts the invalidation tests assert
                         against
unit\.Id, unit\.Unit, unit\.Quantity
                         the identity and unit machinery the view is built on
unit\.ReferenceModel, unit\.Analytic, refmod\., gui\.
```

`compile_fail` is excluded from the repeat for the reason the previous two
milestones recorded: each case is a real compilation, so repeating one measures
the compiler's determinism rather than BetterCAD's. They run unfiltered in all
three presets.
