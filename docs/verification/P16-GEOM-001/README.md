# P16-GEOM-001 — Geometry Preparation / Validity / Regeneration Boundary

```text
STATUS:      see RESULT, below
TASK:        P16-GEOM-001 -- geometry preparation / validity / regeneration boundary
PHASE:       P16 -- Meshing
DATE:        2026-10-01
```

## Baseline

```text
branch        main
HEAD          20b04b915fd3d0458eb769144e6716c55c361bb7
origin/main   20b04b915fd3d0458eb769144e6716c55c361bb7   (HEAD == origin/main)
tree          dcf38e056c727d25dea892378b1ba4a625f19feb
log           20b04b9 BetterCAD: add strong mesh data model
working tree  clean
build root    C:/Users/uqhas/AppData/Local/bc-build   (outside the synchronised folder)
compiler      GNU 16.1.0, C++23
OCCT          8.0.1
```

## Prerequisites

Verified before any production change, not assumed:

```text
P16-ARCH-001   25 boxes ticked, 0 open, PASS marker present
P16-DATA-001   26 boxes ticked, 0 open, PASS marker present
ADRs read      ADR-030, ADR-031, ADR-032, ADR-033
```

Nothing re-decided here. Mesh ownership, mesh identity, element types, the coordinate frame, the
canonical/derived split and the backend boundary all come from those ADRs unchanged.

## Scope

```text
IN SCOPE      the one geometry boundary for meshing; regeneration-state and
              currency gating; body eligibility; the geometry revision foundation;
              the configuration guard; the tolerance and healing contracts

NOT IN SCOPE, and NOT STARTED
              P16-SURF-001, P16-VOL-001, P16-SIZE-001, P16-QUALITY-001 and later.
              Nothing here meshes anything, calls OCCT, or chooses a backend.
```

## Regeneration audit

Full record: [AUDIT.md](AUDIT.md). The two findings that shaped the design:

**The hazard the brief anticipated does not exist here.** `body()` is documented as "the latest
successful build", which reads like a cache that survives a failure. It is not: `fail` and
`block` both call `bodies_.erase(id)`, and `RegeneratorTests` states it as a property — "Failed
and blocked items have no (stale) result". So no last-known-good body can be mistaken for
current.

**The hazard that does exist is sharper, and nothing covered it.** `state()` reports what the
last pass did, not whether the result still follows from the document:

```text
after an edit with no regeneration:
  state(feature)          UpToDate       <- true of that pass, useless here
  body(feature)           the old body   <- valid, closed, positive volume
  builtRevision(feature)  != revisionOf  <- the only thing that knew, and it
                                            was PRIVATE with no accessor
```

### The trap inside it

Comparing the feature's **own** revision is not enough: **editing a sketch does not change the
revision of the extrude that consumes it.** A guard built that way would have passed a test that
edited the feature directly and failed silently in practice. Currency has to walk the dependency
graph.

### One rule, not two

`regenerate()` and the new `isCurrent()` both call one private `dirtySources()`. A second copy
would be a second definition of staleness. The only term `regenerate()` adds is one a pass alone
can know — a parameter expression that failed in the pass being run — and it is added visibly at
the call site. So `isCurrent()` can only answer *current* where a pass would have found more
work, never *stale* where a pass would have found none; the header says so.

## Authoritative body source

```text
owner             features::Regenerator (its private bodies_ map)
queried by        regenerator.body(ObjectId)
currency proven   NOT by state(). By builtRevision() plus the dependency graph,
                  which is what isCurrent() does
precedent         partMassProperties(const Document&, const Regenerator&, ObjectId)
```

## Geometry preparation API

```cpp
Result<MeshableGeometry> requireMeshableGeometry(const Document&, const Regenerator&, ObjectId);
std::optional<GeometryIneligibility> geometryIneligibility(const Document&, const Regenerator&, ObjectId);
GeometryRevision geometryRevision(const Document&, ObjectId);
```

`MeshableGeometry` carries the source id, the authoritative `Body` **by value**, the solid count,
the kernel's volume, and the geometry revision. It carries no meshing settings and no nodes or
elements: preparation and meshing are separate steps.

**Read-only in every argument.** Nothing regenerates, heals, activates a configuration or saves.
`Meshable_PreparationDoesNotTouchTheDocumentOrTheRegenerator` checks the document revision, the
built revision, the body pointer and the body's topology are all unchanged after four
preparations — observable state, not just the signature.

`P16-SURF-001` and `P16-VOL-001` obtain geometry here and nowhere else, so there is no second
meshability rule to diverge.

## State and diagnostic matrix

| Geometry state | Body exists? | Current? | Meshable? | Diagnostic | Test |
| --- | --- | --- | --- | --- | --- |
| Current valid solid | yes | yes | **yes** | — | `Meshable_AcceptsACurrentBoxAndReportsItsAnalyticalVolume` |
| Object deleted / absent | — | — | no | `ObjectNotFound` | `..._RefusesAnObjectThatDoesNotExist` |
| Never regenerated | no | — | no | `NeverRegenerated` | `..._RefusesAFeatureThatHasNeverBeenRegenerated` |
| Makes no body (a sketch) | no | yes | no | `NoBody` | `..._RefusesAnObjectThatMakesNoBodyAndSaysSoDistinctly` |
| Regeneration failed | **no** (erased) | no | no | `RegenerationFailed` | `..._RefusesAFeatureBlockedByAFailedUpstreamSketch` |
| Blocked upstream | **no** (erased) | no | no | `RegenerationBlocked` | same |
| Stale after a profile edit | **yes** | **no** | no | `GeometryStale` | `..._RefusesGeometryThatIsStaleBecauseItsProfileMoved` |
| Stale under a configuration override | yes | n/a | no | `ConfigurationOverrideActive` | `..._RefusesEveryGeometryWhileAConfigurationOverridesAParameter` |
| Empty body, stored and up to date | **yes** | yes | no | `EmptyBody` | `..._RefusesAnEmptyBodyThatRegenerationNonethelessStored` |
| Multiple solids | yes | yes | **yes** | — | `..._AcceptsAMultiSolidBodyAndReportsTheSolidCount` |
| No solid / invalid BRep / non-positive volume | yes | yes | no | `NotASolid` / `InvalidBRep` / `ZeroVolume` | no fixture — see KNOWN LIMITATIONS |

Three of the eleven reasons fire with a **non-null body**, which is why meshability cannot rest
on a null check.

## Check order, and why it is this order

```text
1. active configuration overrides     refuse before looking at anything
2. the object exists
3. regeneration state: never / failed / blocked
4. CURRENCY
5. a body exists, and is not empty
6. the kernel's analyzer accepts it
7. it contains a solid                TOPOLOGY, before measurement
8. positive finite volume
```

Steps 3 and 4 precede 5 to 8 deliberately. A stale body is still a perfectly good closed solid,
so inspecting it yields a confident answer about a model the user has already replaced —
reporting `EmptyBody` or `ZeroVolume` would send someone debugging the wrong thing.

**This ordering is pinned by a test, and was not at first.** See finding F5 in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md): the original precedence test proved only that a
*good* stale body is refused, because the later checks pass either way. The fixture that pins it
needed a body that is **both** stale and geometrically unusable, which an `Intersect` operation
that misses provides.

## The configuration defect

```text
Configuration A (base)       effective width 20 mm
                             body volume 20 x 30 x 50 = 30000 mm^3
switch to "Wide"             overrides width -> 80 mm
body volume after the switch 30000 mm^3, UNCHANGED  <- the carried defect
meshing geometry request     REFUSED
diagnostic                   ConfigurationOverrideActive, naming "Wide" and
                             "does not yet rebuild"
```

The guard is P15-MASS-001's, reused in substance: refuse before anything else, so that no amount
of valid, current, closed, positive-volume geometry can talk it into answering.

The test asserts the **refusal**, and deliberately does not record what the stale volume is,
because that would turn a defect into a contract. It also checks the guard is not a latch: the
base configuration is meshable again, and a configuration that overrides **nothing** is accepted,
because over-refusing would be its own defect.

**Both behaviours are recorded, as §12 requires.** Today: a configuration changes effective
geometry, the geometry is stale, meshing refuses. After the defect is fixed: the configuration
change regenerates the geometry and meshing consumes the updated shape. This test is deleted with
the guard it covers; it is not the permanent architecture.

**Nothing regenerates behind the caller's back** to make the problem go away. The document's
regeneration semantics stay authoritative and preparation only verifies currency.

## Geometry eligibility, and the one definition of it

`features::bodyDefect(const Body&)` — **extracted, not written here.**
`features::Validation::checkBody` already defined body eligibility, and my first implementation
duplicated it in a different order. `validateDocument()` now keeps the sentences for a person and
`meshing` maps the typed reason: one rule, two presentations.

```text
Empty              -> EmptyBody
NoSolid            -> NotASolid          topology, before measurement
InvalidShape       -> InvalidBRep
VolumeUnavailable  -> ZeroVolume
NonPositiveVolume  -> ZeroVolume
```

An open shell is refused for having **no solid**, not for integrating to zero: closure and volume
are different properties and a shape can fail either alone. No shell is promoted to a solid here.

P16 requires a solid for both surface and volume meshing, because P16's surface mesh is the
boundary of the volume domain rather than a standalone sheet mesh (ADR-032). A future sheet or
shell meshing capability would need its own eligibility contract, and would not get it by
weakening this one.

### Extracting it found a production defect

The old positivity test was `properties->volume > Volume{}`. **`Inf > 0` is true**, so an
infinite volume counted as positive and `BodySummary::valid` was `true` for such a body.
`bodyDefect` requires positive **and finite**, which strengthens `validateDocument()` too. This is
finding F2, and it had been in qualified code since P12.

## Holes, voids and multiple solids

```text
hollow tube    V = pi (Ro^2 - Ri^2) h = pi (20^2 - 12^2) * 40
               the cavity survives preparation, and the arithmetic is what would
               catch using the outer bounding cylinder instead -- that would be
               2.78x larger
multi-solid    one extrude of two disjoint rectangles gives solids == 2, which is
               SUPPORTED: ADR-032 gives a mesh one region per solid. The count is
               reported so P16-VOL-001 can make the regions rather than letting a
               backend decide by accident
```

Nothing is rebuilt during preparation, so no hole can be filled and no cavity closed: a `Body`
copy shares the kernel shape.

## Transforms and coordinate frame

```text
frame              the document model frame of the body it came from (ADR-032);
                   no transform field, no per-node frame
location stripping NOT POSSIBLE in this layer: it never names a TopoDS_Shape, a
                   TShape or a Location. The architecture check confines OCCT to
                   src/(.+/)?occt/ and src/meshing/ has no such directory
test               a body built on the XZ plane away from the origin: the prepared
                   body's bounding box equals the regenerator's EXACTLY, and the
                   volume is invariant (20 x 30 x 4 = 2400 mm^3 wherever it sits)
```

## Tolerance contract

```text
closure          topological: topology().solids == 0. No tolerance.
positive volume  > 0 AND finite. No tolerance.
BRep validity    the kernel's own, via BRepCheck_Analyzer
```

**No epsilon was introduced**, so §32's prohibition is met by not needing one. "Positive and
finite" has no threshold to tune and therefore cannot reject legitimate micro-scale geometry or
accept a near-flat solid the size of a building — which a fixed SI threshold would have done.

Kept separate per §33: mesh quality thresholds are `P16-QUALITY-001`'s, and the mesh data model
has no coordinate tolerance either.

## Shape healing

```text
Does BetterCAD heal generated geometry?   NO -- ShapeFix and ShapeAnalysis appear
                                          in no source file in the repository
Does this milestone heal anything?        NO
Policy                                    REJECTION, not repair -- and it is the
                                          status quo rather than a new rule
```

A backend may repair geometry internally and nothing here can prevent that. This layer
establishes what BetterCAD considers acceptable **before** a backend is invoked; auditing what a
backend then does is `P16-VOL-001`'s, and that is recorded rather than claimed.

## Geometry revision and the mesh-invalidation foundation

`GeometryRevision` mixes the feature's own revision with the revisions of **every object it
transitively depends on**, in ascending ObjectId order, plus the active configuration.

```text
why not the object's own revision alone   editing a sketch does not change the
                                          extrude's revision, so the stamp would be
                                          identical across a change that replaced
                                          the geometry
why not Document::revision()              it moves for a material density or a
                                          rename, and §39 requires a material edit
                                          NOT to invalidate geometry
why ascending, not topological            two valid topological orders of one graph
                                          would stamp the same model differently
why not std::hash                         it is not required to agree between
                                          builds, and this value is compared across
                                          three presets
why not BRep bytes                        that fingerprints the kernel's internal
                                          representation, not the model
```

**This is the narrowing ADR-030 named in advance**: its mesh build stamp uses the document
revision as a conservative outer guard and says "P16-GEOM-001 may narrow it with measurement. It
may never widen it."

```text
G1 -> revision R1                                      stable over 8 repeats
material created and assigned                          document revision MOVES,
                                                       geometry revision UNCHANGED,
                                                       geometry still meshable
geometry parameter edit (the profile)                  isCurrent false -> REFUSED,
                                                       and the revision moves
successful regeneration -> G2, revision R2             R2 != R1, accepted again
```

A later mesh records the revision it was built from; a mesh whose recorded revision differs from
the current one is stale. The orchestration of that belongs to the mesher (ADR-030); this
milestone provides the foundation only.

## Lifetime

`MeshableGeometry` holds a `Body` by value and no reference to the document or the regenerator, so
it cannot dangle if either changes or is destroyed, and a later asynchronous mesher cannot
dereference state that moved underneath it. The snapshot is a value; the document remains the
authority.

## Analytical fixtures

Every expected value is closed-form and evaluated by hand. None was obtained by running BetterCAD
and recording what it said.

| Fixture | Expected V (mm³) | Derivation | Result |
| --- | --- | --- | --- |
| Box 20×30×50 | 30000 | `abc` | **PASS** within 1e-9 rel |
| Cylinder r=12, h=25 | `π·144·25` ≈ 11309.73 | `πr²h` | **PASS** |
| Hollow tube Ro=20, Ri=12, h=40 | `π(400−144)·40` ≈ 32169.9 | `π(Ro²−Ri²)h` | **PASS** |
| Two disjoint boxes 10×10×2 and 5×5×2 | 250 | `(100+25)·2` | **PASS**, `solidCount == 2` |
| Box on the XZ plane 20×30×4 | 2400 | invariant under rigid placement | **PASS**, bounding box identical to the regenerator's |
| Trapezoid after a 40 mm edge constraint | 45000 | `½(40+20)·30·50` | **PASS** |

The trapezoid case is worth a note: `addRectangle` adds four lines through four shared points and
**no** geometric constraints, so constraining the bottom edge to 40 mm does not widen a rectangle
— it stretches one edge and leaves the opposite edge at 20. My first expectation assumed a
rectangle and was wrong; the derivation is independent of which corner the solver moved, because
the opposite edge keeps its length and the height is untouched.

## Mutation proof

The central guard is the kind that passes every test while doing nothing, so it was mutated.

| Mutation | Tests failed | Proves |
| --- | --- | --- |
| Currency check disabled | **3** | the guard is load-bearing |
| Graph walk removed, own revision only | **4** | the graph walk specifically — the trap is real |
| Currency check moved to last | **1** | the check ORDER, via the stale-and-empty fixture |

Each mutation was reverted and the revert verified by grep in the same call, with marker counts
asserted — a mutation survived three builds earlier in this project because a revert was piped to
`/dev/null` and not checked.

## Adversarial review

Full record: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
ATTACKS              21 from the brief, plus 4 raised there
FINDINGS             6
PRODUCTION DEFECTS   1 -- F2, in existing qualified code, found by extraction
FIXED                6
OPEN                 1, recorded and out of scope (F6)
```

F6: `partMassProperties` has the same currency gap this milestone closed for meshing, so mass
properties can be computed from stale geometry after an unregenerated edit. Fixing it is a change
to P15 behaviour that this milestone does not authorize; `isCurrent()` now exists for whoever
takes that decision.

## Determinism

```text
eligibility result     identical over 8 repeats
diagnostic reason      identical
solid count            identical
volume                 EXACTLY equal across repeats (==, not a tolerance)
geometry revision      identical over 8 repeats, and computed with a written-out
                       mix rather than std::hash
```

## Files changed

```text
include/bettercad/features/Regenerator.hpp      + builtRevision, isCurrent, dirtySources
src/features/Regenerator.cpp                    + the three, and regenerate() now
                                                  seeds from the shared dirtySources
include/bettercad/features/Validation.hpp       + BodyDefect, bodyDefect
src/features/Validation.cpp                      checkBody now uses bodyDefect
include/bettercad/meshing/GeometryPreparation.hpp   new
src/meshing/GeometryPreparation.cpp                 new
src/meshing/CMakeLists.txt                       + the source, + geometry, + features
tests/meshing/GeometryPreparationTests.cpp          new -- 25 tests
tests/CMakeLists.txt                             + the test source
```

## Regression

Harness: `qualification/qualify.cmd`, carrying P16-DATA-001's delayed-expansion fix. This
milestone's determinism filter contains `|`, `(` and `)` -- the shape that killed P16-DATA-001's
first run before the fix -- so this is the fix's first real use, and the times file records that
the repeat stage selected 25 tests in each preset rather than dying at the parse.

Pre-freeze checks, all cleared **before** the expensive run:

```text
git diff --check                  clean
rebuild of the restored tree      exit 0, 0 diagnostics
new tests, --repeat until-fail:5  25/25, 125 executions, 33 s
predecessor suites                187/187 -- Regenerator, Dependency,
                                  MassProperties, Configuration, Validation, Loft,
                                  Extrude. This is the check that matters most,
                                  because the milestone changed Regenerator.hpp and
                                  Validation.hpp and TIGHTENED an existing rule
debug-shared-ext build            exit 0, 0 diagnostics, no DLL-boundary diagnostics
debug-shared-ext meshing tests    25/25
architecture.layering             405 files, 0 violations
```

| Preset | Clean build | Full ctest | Tests |
| --- | --- | --- | --- |
| `debug-ext` | 22m12s | 15m44s | **2900/2900** |
| `release-ext` | 24m22s | 15m28s | **2900/2900** |
| `debug-shared-ext` | 18m33s | 14m19s | **2900/2900** |
| repeat `release-ext` | — | 25 tests x5 | **25/25** |
| repeat `debug-ext` | — | 25 tests x5 | **25/25** |

```text
2900 = the 2875 P16-DATA-001 qualified, plus this milestone's 25 new tests
8950 test executions (2900 x 3, plus 25 x 5 x 2)
0 failures
```

**0 compiler warnings in all six build and rebuild logs**, checked strictly for `warning:` and
`[-W`.

**The binaries tested are the binaries built.** Each preset's second build exited 0 having
recompiled and relinked nothing: `grep -icE "Building CXX|Linking CXX"` over all three rebuild
logs returns 0.

## Result

```text
RESULT:      PASS
HARNESS:     "Qualification passed: every stage exited 0."
ELAPSED:     1h59m32s (11:45:08 -> 13:44:40, 2026-10-01)
TESTS:       2900/2900 in each of three presets; 8950 executions; 0 failures
DETERMINISM: 25 tests x5 in release-ext and debug-ext; 0 failures
WARNINGS:    0 in all six build and rebuild logs
TREE:        the eight qualified tree IDs are identical before the first build and
             after the last test run
EVIDENCE:    qualification/qualification-times.txt and the per-preset logs
```

Qualified source trees, recorded before the first build and unchanged after the last test run:

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           224d6e361d67aeefadd967cc4afe461c2d6bd7ea
src               90b34d8f905148e8b957b94fb4c29854229e6f26
tests             d2545bdede634a3c5c5e2419a6ca35821dd4fe3c
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt    13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

`docs/` and `TODO.md` are outside the fingerprint, so the evidence and the TODO closeout that
follow this run do not affect it.

## Known limitations

```text
NotASolid, InvalidBRep and the two volume paths have NO fixture: no feature was
found that stores a body with zero solids, a shape the kernel rejects, or a
non-positive volume. EmptyBody was in that category until the
Intersect-that-misses route was found, so these are recorded as NOT-YET-REACHABLE
rather than unreachable. They are kept on P15-MASS-001's stated grounds, and they
run the same function validateDocument() uses, whose own suite covers it.

Renaming a feature moves its own revision, so it moves the geometry revision too.
Invalidation is conservative there rather than wrong. Recorded by a test that
reports the behaviour instead of asserting a preference.

The configuration guard refuses ALL geometry while any override is active, not just
geometry the override affects. Correct while the carried defect stands, and
coarser than it will need to be afterwards.

GeometryRevision includes the active configuration's identity but not per-override
granularity, which is unnecessary while overrides are refused outright.

Nothing here meshes. The first mesh from geometry is P16-SURF-001's.

What a backend does to geometry after this boundary is outside this layer's
control, and is P16-VOL-001's to audit.

F6 is open: mass properties has no currency check.
```

## Revision

First issue, 2026-10-01.
