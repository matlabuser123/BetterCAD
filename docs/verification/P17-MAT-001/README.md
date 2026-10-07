# P17-MAT-001 — Structural Material Resolution

```text
STATUS:   PASS
MILESTONE: P17-MAT-001, the third milestone of P17 — Structural FEA
SCOPE:    resolving and validating the material a linear-elastic solve
          consumes. No D matrix, no B matrix, no element, no gravity vector.
```

## Baseline

```text
HEAD at start      57b4ddf2b87cba8f6f5ffe47a44235d6320b7d85
origin/main        57b4ddf2b87cba8f6f5ffe47a44235d6320b7d85
working tree       clean, 0 porcelain lines
P17-ARCH-001       PASS, 20/20        P17-DATA-001   PASS, 19/19
P15 / P16          QUALIFIED
and the eight paths still fingerprinted fcf2ea16 -- P17-DATA-001's qualified
tree -- so both predecessors were demonstrably qualified on THIS tree
```

## The finding that shaped the milestone

The checklist asks P17 to validate `E > 0`, validate `-1 < nu < 0.5` and reject
NaN and infinity. **P15 already does all of it, at the point of entry**, which
means an unusable value cannot be in a document at all:

```text
createMaterial            refuses it
setMaterialMechanical     refuses it, and leaves the previous value intact
isAvailable               treats an out-of-range value as ABSENT, so a
                          completeness query cannot call such a material Ready
requireLinearElasticConstants / requireDensity
                          validate again at consumption, naming every gap at
                          once and naming the missing INPUT
```

That was discovered by eleven failing tests, not by reading: the first draft
created materials with `nu = -1`, `E = 0` and NaN and asserted the resolver
refused them, and every case failed at `createMaterial` instead. So the honest
claim is not "P17 validates these" — it is "**P15 makes them unrepresentable**,
and here is the proof". The boundary tests now assert that guarantee, and the
delegation is proved through the reachable half of P15's contract: a required
property that is absent.

`ADVERSARIAL_REVIEW.md` finding F1; the full audit in
[P15_AUDIT.md](P15_AUDIT.md).

## What this milestone therefore adds

Three things, and they are the three P15 cannot know:

```text
StructuralAnalysisMode       WHICH consumer this analysis is. Intent, so it is
                             a field of StructuralAnalysisDefinition: a density
                             on the material does not make a problem a
                             self-weight problem, the user saying so does
resolveStructuralMaterial    one solver-ready view per mode, carrying the
                             material's id and revision as references back to
                             P15, and a density exactly when the mode needs one
the integration proof        that an E or nu edit leaves the P16 mesh CURRENT
                             and makes the structural result STALE
```

The density requirement is **read from P15's table** rather than hardcoded:

```cpp
const PropertyRequirement required = requiredProperties(consumerFor(mode));
const bool needsDensity = std::ranges::find(required.mechanical,
                                            MechanicalPropertyKind::Density) != ...
```

One line, and it is what makes the two impossible to disagree.

## The consumer mapping, measured

```text
LinearStatic             -> ConsumerKind::FeaLinearStatic
                            requires { YoungsModulus, PoissonRatio }
LinearStaticWithGravity  -> ConsumerKind::FeaLinearStaticWithGravity
                            requires { Density, YoungsModulus, PoissonRatio }
both                        require NO thermal property
```

One material with E and nu and no density is `Ready` for the first and
incomplete for the second, with `missingMechanical == { Density }`. That is what
consumer-specific completeness is for: a material characterised for stress is
not blocked for want of a conductivity.

Gravity is **selectable, not assembled** — `P17-LOAD-001` owns the body-force
vector. Resolving a material for that mode today is what proves the data is
there before the physics needs it.

## Units

```text
E     ElasticModulus = Pressure, Pa internally
nu    PoissonRatio, dimensionless
rho   Density, kg/m^3 internally
```

`200 GPa`, `200000 MPa` and `2e11 Pa` all resolve to the same `2e11` — the
factor-of-1000 test. The types carry the dimensions, so a `Length` cannot be a
modulus and a bare `double` cannot be either, asserted at compile time.

## The hard requirement, measured

```text
E 210 -> 190 GPa:
  Mesher::currency                  Current
  mesh().stamp()                    UNCHANGED
  geometryRevision                  UNCHANGED
  MeshControlDefinition             UNCHANGED, compared by value
  staleReasons(before, after)       { Material }   <- exactly one
  resolved E                        190 GPa, from P15
```

Four independent things asserted not to have moved, one asserted to have moved.
Asked through **P16's own currentness API**, not by comparing a pointer.

And the adversarial case, which is why the source stamp carries a revision at
all:

```text
same MaterialId, same designation "Steel", E 210 -> 205 GPa
  source.material          UNCHANGED
  source.materialRevision  1 -> 2
  staleReasons             { Material }
```

Invalidation is **dynamic, not imperative**: there is no `markMeshStale` or
`invalidateResult` hook anywhere in `src/` or `include/` — the one textual
occurrence is a comment in `Mesher.hpp` saying "UI-owned markMeshStale() is not
authority". A material edit moves a revision and the next currentness query
notices.

[INVALIDATION_MATRIX.md](INVALIDATION_MATRIX.md), which also records the
density-only decision and its cost.

## Tests

```text
tests/structural/StructuralMaterialTests.cpp     19 ctest entries
  the mode chooses P15's consumer, and the requirement table is READ
  a built-in-style and a custom material resolve to exact canonical values
  no assignment / deleted body refuse
  missing E, missing nu, and an incomplete CUSTOM material refuse
  density required only by the gravity mode, from both sides
  P15 refuses every unusable E, nu and rho AT ENTRY, including NaN and both
    infinities, and refuses the EDIT too
  the resolver still asks P15 rather than assuming validity
  any finite positive E accepted; nu accepted at nextafter(-1) and
    nextafter(0.5), and at 0.49 -- no invented threshold
  units canonical and equivalent inputs agree
  identity and revision preserved, provenance not copied
  an E edit leaves the mesh current and stales the result
  a nu edit and a reassignment behave the same way
  the same id with a changed modulus is a different solver input
  a view is not equal across modes
  resolution is deterministic over 16 repeats and read-only
  every MaterialProblem is reached
```

## Zero-match protection

```text
StructuralMaterial 19   StructuralData 21   StructuralInput 10
compile_fail.structids 12   architecture. 14
```

Each checked with `-N` before running.

## Adversarial review

```text
QUESTIONS                 31  (27 from the brief, 4 of my own)
FINDINGS                   3
PRODUCTION DEFECTS         0
CLAIMS CORRECTED           1  the validation happens at P15's entry, not at
                              P17, and the evidence says so rather than
                              claiming credit for it
TEST DEFECTS OF MINE       2  both from assuming a state was constructible, or
                              an API behaved a way I had not read
GATE-BLOCKING              0
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
A material is assigned per DOCUMENT in P15 -- there is no inheritance and no
  per-body assignment. resolveStructuralMaterial takes the body anyway, and
  uses it to establish that the target exists: the question being asked is
  "what is this body made of", and a per-body assignment would change the
  answer and not the call.

A density-only edit stales a no-gravity result that did not use the density.
  A decision with a stated cost: materialRevision is a per-material counter and
  there is no per-property revision in P15, so the alternative would be a second
  revision mechanism for material state. One unnecessary re-solve is the
  cheaper error.

The structural resolver's own range checks sit behind a boundary that cannot
  currently be crossed, because P15 refuses the values at entry. Kept, because
  the same call would refuse an out-of-range value if an import path or a file
  ever produced one -- and the delegation is tested through the reachable half.

Gravity is selectable and not assembled. A material can be resolved for
  LinearStaticWithGravity; nothing computes a body force. P17-LOAD-001 owns it.

The "no derived state in the analysis definition" assertion is now a bound plus
  trivial copyability, and the second half has a half-life: P17-LOAD-001 giving
  the definition a collection of loads will break it legitimately, and the
  instrument will have to change again.

This MinGW toolchain has no ASan/UBSan. Inherited and recorded.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then
two repeat stages. One uninterrupted detached run, 13:07:38 to 15:55:45,
2 h 48 min, 17 stages, `qualify.cmd exit 0`.

```text
PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3453/3453
release-ext            0       0      0         0           0   3453/3453
debug-shared-ext       0       0      0         0           0   3453/3453

REPEAT (5x each of 734 selected tests, back to back)
release-ext            0                            734/734   665.61 s
debug-ext              0                            734/734   711.57 s

warnings, all three clean builds   0   (-Werror and 22 warning flags,
                                        605 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       10 DLLs
```

**3453** is P17-DATA-001's 3434 plus this milestone's 19 material tests.
**605** objects is 603 plus the structural material translation unit, compiled
once into the library and once into the test binary.

**Cross-preset equivalence**, which matters because every validation in this
milestone is delegated to another layer and an optimiser is exactly the thing
that could reorder a floating-point comparison:

```text
debug-ext          All tests passed (1994 assertions in 50 test cases)
release-ext        All tests passed (1994 assertions in 50 test cases)
debug-shared-ext   All tests passed (1994 assertions in 50 test cases)
```

Identical, including the boundary cases at `nextafter(-1.0, 0.0)` and
`nextafter(0.5, 0.0)` — the two values immediately inside the exclusive bounds,
which would be the first to disagree if a comparison were reordered or a
constant folded differently.

## The qualified tree is the committed tree

```text
| WHEN                              | WHOLE FINGERPRINT                        |
| frozen, before the first build    | 4d4698b3d0220e5fa490104c7fc6110d9d9443a1 |
| recorded by the harness after the | 4d4698b3d0220e5fa490104c7fc6110d9d9443a1 |
|   last test of the last preset    |                                          |
| recomputed before the commit      | 4d4698b3d0220e5fa490104c7fc6110d9d9443a1 |
```

Component for component at all three readings:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            9cb0ee3b279afea572e3d8c56f3dac62cf53ff5d
src                7e262539eb737df2699ab6bff080a016f64dda84
tests              1140337fb3f41a2fc1d94dbf203701744fd6eaaa
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

The first two readings are the harness's own, written into
`qualification/qualification-times.txt` at both ends of the run. The third was
recomputed by the same `read-tree` + `add -A` method immediately before
`git add`.

The whole value is a function of the eight paths **plus the base tree HEAD
pointed at**, which was `57b4ddf` throughout. The invariant that survives a
moving HEAD is the component list, checkable against the published tree in one
command:

```bash
git fetch origin
for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do
  echo "$p $(git rev-parse "origin/main^{tree}:$p")"
done
```

What moved after the freeze: `docs/verification/P17-MAT-001/`, `TODO.md` and
`ROADMAP.md` — all documentation, none inside the fingerprint, none configured,
compiled, linked or read by a test.

## Result

```text
RESULT:   PASS
TESTS:    3453/3453 in debug-ext, release-ext and debug-shared-ext, each
          from clean; 0 warnings over 605 objects; 734 x 5 repeats in two
          presets; 17 stages, 0 failed
TREE:     4d4698b3d0220e5fa490104c7fc6110d9d9443a1, identical at all three
          readings
EVIDENCE: this directory
```

## Revision

First issue, 2026-10-07.
