# P15-ARCH-001 — Materials / Engineering Data Architecture

```text
STATUS:    PASS
MILESTONE: P15-ARCH-001
DATE:      2026-09-26
BASELINE:  d9739a6 (P14 qualified), clean tree, HEAD == origin/main
ADRs:      ADR-025, ADR-026, ADR-027, ADR-028
```

## SCOPE

Architecture only. This milestone decides the material and engineering-data
contracts, records them as ADRs, and validates every claim they rest on against
the tree at `d9739a6`.

```text
IN     audit of what exists; ownership, identity, definition/assignment,
       canonical/derived, missing-data, library, provenance, configuration,
       occurrence, persistence, downstream and layering contracts; ADRs; one
       architecture guard; evidence
OUT    the material library's actual engineering values; MaterialId and the
       property types themselves (P15-UNITS-001, P15-MAT-001); any solver
```

**No production material code was written.** The only source change is one test
that guards a contract ADR-026 cites and which was previously ungated. No new
module, no new type, no layer-table change.

## BASELINE

```text
branch        main
HEAD          d9739a6bb7a01fc4e8e76025b4410c61e8d73b45
origin/main   d9739a6bb7a01fc4e8e76025b4410c61e8d73b45
HEAD^{tree}   af64a31eedaf087cd0e765fdd948b40d7cf7bebb
working tree  ONE modified file: TODO.md
```

**The dirty file was classified rather than assumed.** `TODO.md` had been
rewritten to open P15 and mark `P15-ARCH-001` **CURRENT** — the scope decision
P14's closeout said was the user's to make. It compacts P14's completed-milestone
sections (759 insertions, 1524 deletions), which is what `CLAUDE.md` asks for:
"Completed milestones move to ROADMAP.md with their evidence links; TODO.md stays
a list of work still to do." Nothing was lost: the qualified list, all three
carried limitations and the windeployqt blocker all survive in the new Status
block, and the detail remains in `docs/verification/` and in git history.

**One real gap found in that change, and fixed here:** `ROADMAP.md` still said
`P14 | In Progress` and `v0.5 | Technical Drawings | In Progress — P14`. P14 is
qualified. That is my omission from the P14 closeout — I updated `TODO.md` and not
`ROADMAP.md` — and since `CLAUDE.md`'s precedence is
`evidence → TODO → architecture → roadmap`, the roadmap must follow.

## EXISTING MATERIAL AUDIT

**There is no material concept in BetterCAD.** Searched `include/`, `src/`,
`apps/`, `tests/`, `examples/` for material, density, Young, modulus, Poisson,
yield, ultimate, strength, thermal, conductivity, specific heat, expansion,
viscosity, resistivity, hardness, property.

```text
concept              exists?  where                          reusable for P15?
material (engineering)  NO    --                             n/a, nothing to reuse
"material" (93 files)   --    geometric prose only:          no
                              "Material in either body"       (Booleans.hpp)
                              "through all material"          (Hole.hpp)
MaterialRemoval         YES   drawing/Annotation.hpp:141     NO -- ISO 1302
                              surface-finish symbol, not     surface finish, a
                              engineering data               NAME COLLISION to
                                                             avoid, nothing more
density                 unit  core/units: Density = mass/    YES -- the quantity
                              volume, _kg_per_m3, _g_per_cm3 exists; no material
                                                             holds one
mass properties         YES   geometry::MassProperties       YES, as the derived
                              volume, area, centre of mass;  side. It has NO mass
                              "(uniform density)"            field, for want of a
                                                             density
Young / Poisson / yield  NO   0 hits each                    n/a
thermal / conductivity   NO   1 hit, a comment in            n/a
                              Dimension.hpp
viscosity / resistivity  NO   0 hits                         n/a
hardness                 NO   0 hits                         n/a
```

So P15 starts from zero on materials. There is no duplicate concept to avoid and
no existing ownership pattern for materials to copy — which is why the two
patterns below had to be found by analogy instead.

## EXISTING UNIT AUDIT

Much stronger than the material side, and it decides ADR-027's dimensional half.

```text
Quantity<Dimension D>          compile-time dimensional safety; Dimension is a
                               non-type template parameter, so mixing a Length
                               and a Pressure will not compile
base dimensions (5)            length, mass, time, temperature, angle
derived dimensions declared    area, volume, velocity, acceleration, force,
                               pressure, density
Quantity aliases (12)          Length Area Volume Angle Mass Time Temperature
                               Velocity Acceleration Force Pressure Density
literals                       including _kg_per_m3 and _g_per_cm3
finiteness                     isFinite(Quantity) exists
serialization                  bare SI doubles via .si() / ::fromSi -- no unit
                               string in the file; SI internally, converted at
                               boundaries
tests                          21 cases across ConversionTests,
                               FormatAndCatalogTests, QuantityTests
```

**Temperature is already a base dimension**, and `Dimension.hpp` says so on
purpose: "Temperature is included so that thermal quantities can [be expressed]".

### What P15-UNITS-001 must add, and the one real gap

```text
needed                         expressible today?   note
energy = force x length        yes, not declared    alias only
power = energy / time          yes, not declared    alias only
specific heat cp
  = energy / (mass x temp)     yes, not declared    needs energy
thermal conductivity k
  = power / (length x temp)    yes, not declared    needs power
thermal expansion alpha
  = 1 / temperature            yes, not declared    alias only
dynamic viscosity
  = pressure x time            yes, not declared    alias only
electrical resistivity         *** NO ***           needs a NEW BASE DIMENSION:
                                                    there is no electric current
                                                    in Dimension. P15 does not
                                                    need it; P19 might. It is a
                                                    base-dimension change, not
                                                    an alias
```

So P15's mechanical and thermal needs are all aliases over existing bases. Only
electrical properties require touching `Dimension` itself.

## IDENTITY AUDIT

```text
Id<Tag>                 strong typed IDs, one per concept
isDocumentObjectTag     11 tags already widen to ObjectId (sketch, feature,
                        parameter, body, component, mate, sheet, view,
                        dimension, annotation, ...)
IdAllocator             "Values start at 1 and are never reused, even after the
                        identified item is deleted, so a stale reference can
                        never silently resolve to a newer item."  -- verbatim,
                        Id.hpp:209-211
ConfigurationId         document-level state, NOT a document object, so it does
                        not widen; still from the one allocator so no ID is reused
DocumentId              a UUID, durable identity of a document
ObjectReference         {object, optional<document>, hint} where the hint is
                        "a locator, never identity, and never trusted: a
                        candidate found through it is accepted only if its own
                        identity matches `document`"
```

**Names are not identity, and cannot be.** `validateIdentifier` allows "letters,
digits and '_', and do not start with a digit", and `Document.hpp:41` states
"Names are unique across parameters and objects, so lookup by name is
[unambiguous]". `Document::uniqueName()` exists for collisions.

**This contradicted the brief, and the repository won.** The brief requires that
two materials may share a display name. Two document objects may **not** share a
name — and `"Aluminium 6061-T6"` is not even a legal object name, having a space
and a hyphen. So the design carries two strings: an identifier-style object name,
unique per document, and a free-text **designation** that may duplicate. Identity
is the `MaterialId` and is neither. Recorded in ADR-025.

## DOCUMENT OWNERSHIP AUDIT

```text
26 document object kinds   annotation chamfer circular_pattern combine component
                           coordinate_system datum_axis datum_plane dimension
                           draft extrude fillet hole linear_pattern loft mate
                           mirror revolve rib sheet shell split sweep
                           variable_fillet view
there is NO Part object    a part IS a document; ComponentDefinition::part is an
                           ObjectReference (Component.hpp:60)
there is NO body object    BodyId exists and widens to ObjectId, but nothing
                           declares kTypeName "body" (0 hits). A body is a
                           regeneration RESULT, and its handle is the ObjectId of
                           the feature that produced it: regenerator.body(feature)
a solid or a face          no persistent identity at all -- the ground ADR-012
                           and ADR-024 stand on
```

This constrained ADR-026 more than any preference did: the only things that can
carry a persistent material assignment are the document itself, a feature, and a
component.

## CONFIGURATION AUDIT

Verbatim from `core/document/Configurations.hpp`:

> "a configuration overrides the values of free parameters, and everything else
> follows from the equations that were already there"
>
> "What a configuration never does is change the document's canonical state."

The only non-parameter override is suppression, and it is **typed**:
`suppressComponent` refuses an ID that is not a component, because "a suppression
override on a sketch would be a statement about nothing".

**So configuration cannot select a material variant today**, and not because of a
policy choice: a `MaterialId` is not a parameter value, and parameters are
dimensioned scalars. Carrying one would need a new typed override kind. ADR-026
therefore makes material assignment configuration-independent, and says what
would have to change if that is ever wanted.

## PERSISTENCE AUDIT

```text
canonical, persisted       feature definitions, parameters, sketch entities and
                           constraints, references, placements as intent, sheets,
                           views, dimensions, annotations, configurations and
                           their overrides
derived, NEVER persisted   volume, surface area, centre of mass -- 0 hits for
                           centerOfMass / centreOfMass / surfaceArea across every
                           writer in src/io/json/
                           a solved transform (ADR-005)
                           projected drawing curves (ADR-011)
quantities                 written as bare SI doubles
format version             2, reader accepts 1 and 2 (ADR-024)
```

`MassProperties` also carries `volumeRelativeError` and `areaRelativeError`
beside the values, so a derived quantity already travels with a statement about
its own quality — the shape ADR-028's provenance follows.

**This contract was honoured in practice and guarded by nothing.** See
ARCHITECTURE GUARD below.

## LAYERING AUDIT

```text
core 0   sketch 1   features 2   assembly 3   drawing 4   io 5   renderer 6
                                                                 scripting 6
```

`standards` is absent from the table because it lives *inside* core as
`core/standards/`; the checker derives a file's module from its path. It **fails
the build** on an unknown module — "unknown module '<x>' (add it to the layer
table)" — and on any dependency that does not point downward.

So `core/materials/` needs no table change, and an analysis module above
`features` may consume materials while materials can never reach it.

## THE TWO PATTERNS THE DESIGN IS BUILT FROM

Found by audit, since materials had no precedent of their own.

**`core/standards/` — curated immutable reference data.** "Tabulated data from
published standards, with no geometry", transcribed from sources recorded in
`docs/verification/P12-HOLE-001/` and "checked there against independent copies".
Compiled into the binary. And it **refuses to guess**: `HoleDeviation` omits JS
because "copies of ISO 286-2 disagree on whether its odd tolerances of grades 7
to 11 are rounded to whole micrometres, which BetterCAD cannot settle from the
sources it has."

**How a document consumes it: it stores the designation, not the value.**
`HoleClearance{MetricThread bolt, ClearanceSeries series}` — "its diameter comes
from ISO 273, so `diameter` stays zero". Resolved at regeneration, never
persisted.

**That second pattern was examined and deliberately NOT followed for materials**,
which is the central decision of this milestone. It is safe for ISO 273 because
the table is tiny, published, effectively frozen and compiled in, and because a
clearance diameter is a modelling input the user chose by designation. A
material's density is a physical measurement that gets *corrected*. If a document
stored only a key and the library were consulted at solve time, a library revision
would silently change the mass and stresses of every saved document naming it —
the same class of failure as ADR-024's silent rebind, one level up. Reasoning in
full in ADR-025.

## THE ANSWERS

Every required question, answered. None is TBD.

```text
Does a material live globally, in a document, or both?
    BOTH, with a strict split. A built-in library is immutable reference data in
    core/materials/, modelled on core/standards/. A material USED by a document
    is a document object the document owns. Using a library material IMPORTS it.

What gives a material stable identity?
    MaterialId, a strong Id<Tag> widening to ObjectId, allocated by the
    document's allocator, which never reuses a value even after deletion.
    A library entry has a structured MaterialLibraryKey {library, entry,
    revision} -- never its designation.

Can two materials have the same display name?
    YES for the DESIGNATION ("Steel" and "Steel" with different values are two
    materials, distinguished by MaterialId).
    NO for the object NAME: document object names are unique and are identifiers.
    So a material carries both, and identity is neither.

Can a library material be edited directly?
    NO, and no rule is needed to forbid it: library entries are constexpr
    reference data with nothing to mutate. A user edits the DOCUMENT's copy, so
    one document's edit cannot reach another.

Does assigning a material copy it or reference it?
    An assignment REFERENCES a document-owned material by MaterialId.
    That document-owned material is itself a COPY of the library entry, taken at
    import, with the library key and revision recorded as provenance.
    So: reference within the document, snapshot across the library boundary.

What happens if a referenced library material changes?
    NOTHING happens to the saved document, by construction: no library lookup
    occurs at load time or solve time. Provenance records the key and revision,
    so a later milestone can OFFER to re-sync. Re-syncing is a user action.
    A missing library also changes nothing -- there is no reference to fail, and
    therefore no chance of a name-based fallback to "the nearest material".

How is "property unknown" represented?
    Three states: Known / Unknown / Derivable. Unknown holds no Quantity at all,
    so it cannot be read as a number. Never 0, never NaN, never a sentinel.
    property() may return Unknown; requireProperty() returns Result<Quantity> and
    fails with a diagnostic naming the material, the property and the consumer.

How are temperature-dependent properties represented later?
    By replacing a property's VALUE representation with a law (constant,
    piecewise, interpolated, analytic) while leaving identity, assignment and the
    three states untouched. What makes that safe is that no consumer reads a raw
    double: everything goes through requireProperty(), so a later
    requireProperty(material, kind, state) adds a parameter rather than
    restructuring callers. Evaluating a constant at any state yields the constant.

Can one part/body have more than one material?
    ONE material per part in P15. Per-body is DEFERRED with its mechanism named:
    a body's only persistent handle is its producing feature's ObjectId, so a
    later override can be keyed by that. Per-solid and per-face will never be
    offered -- neither has persistent identity, and offering it would rebuild the
    defect ADR-024 removed.

Can an assembly occurrence override the part material?
    NO in P15, explicitly deferred, not overlooked. A part's material lives in the
    part document and an occurrence lives in the assembly document; overriding
    across that boundary needs the external references ADR-003 defers.
    CONSEQUENCE, STATED: an assembly's mass properties use each part's own
    material, and two occurrences of one part always share a material. For "the
    same bracket in steel and in aluminium", P15's answer is two parts.
    When external references arrive, the override belongs on ComponentDefinition
    beside `suppressed` and `placement`.

Does configuration affect material assignment?
    NO, and this is forced by audited semantics rather than chosen. A
    configuration overrides free PARAMETER values; a MaterialId is not one. The
    resolution is the same material under every configuration. A SUPPRESSED
    component keeps its material -- suppression is "not in this build", not
    "material deleted". Configuration still changes MASS, through geometry.

What exactly is persisted?
    CANONICAL: material identity and object name, designation, canonical property
    values, per-property and per-material provenance (including the library key
    and revision an imported material came from), the part's material assignment,
    and custom materials in full.
    NOT PERSISTED: volume, surface area, centre of mass, mass, inertia, any
    derivable elastic constant (G, K), solver matrices and every analysis result.

Which quantities are canonical?
    density, Young's modulus, Poisson's ratio, yield strength, ultimate tensile
    strength, specific heat capacity, thermal conductivity, coefficient of
    thermal expansion, dynamic viscosity.

Which quantities are derived?
    From geometry: volume, surface area, centre of mass.
    From geometry + material: mass, centre of mass of a uniform part, inertia.
    From other properties: shear modulus G = E/(2(1+nu)) and bulk modulus
    K = E/(3(1-2nu)) -- Derivable, never stored, so there is no second source of
    truth to contradict E and nu.

How do P17/P18/P19 consume the data without duplicating it?
    Through one contract, storing nothing: effectiveMaterial(document),
    requireProperty(material, kind), requireMechanicalProperties(),
    requireThermalProperties(). A solver that cannot obtain a property FAILS with
    a diagnostic. No solver may hold a default, typical or fallback material.
    Enforced by layering: materials sits below analysis, and the checker fails
    the build on an upward dependency.
```

## ADRs

```text
ADR-025  A material is a document object; the library is reference data it is
         imported from                    ownership, identity, library boundary
ADR-026  A material assignment is intent; mass is derived
                                          assignment, multi-material, occurrence,
                                          configuration
ADR-027  An engineering property is known, unknown, or derivable -- never zero
                                          missing data, canonical/derived, units,
                                          temperature extension
ADR-028  Provenance is per property; a solver holds no material data of its own
                                          provenance, downstream boundary, module
                                          ownership
```

Numbering continues from the repository's highest, which was checked: `ADR-024`.

## MECHANICAL VALIDATION

Every claim the ADRs rest on, checked against `d9739a6`. No ADR depends on
infrastructure that does not exist.

```text
claim                                                    checked            result
Id<Tag> + isDocumentObjectTag hosts a new object ID      Id.hpp:39,90,158   11 tags exist
IdAllocator never reuses, even after deletion            Id.hpp:209-211     verbatim
object names are identifiers (letters, digits, '_')      Naming.cpp:29-32   confirmed
object names are unique per document                     Document.hpp:41    confirmed
uniqueName() exists for collisions                       Document.hpp:267   confirmed
Density exists as a Quantity, with literals              Units.hpp:22       confirmed
temperature IS a base dimension                          Dimension.hpp:19   confirmed
electric current is NOT a base dimension                 Dimension.hpp:14-20 GAP confirmed
isFinite(Quantity) exists                                Quantity.hpp:175   confirmed
MassProperties has NO mass field                         Body.hpp:20-30     confirmed
derived geometry appears in no JSON writer               src/io/json/       0 hits
there is no Part document object                         Component.hpp:60   a part IS a document
there is no body document object                         0 kTypeName "body" confirmed
a body's handle is its producing feature's ObjectId      regenerator.body() confirmed
configuration overrides free PARAMETER values only       Configurations.hpp confirmed
suppression is typed and refuses a meaningless target    Configurations.hpp:73-75 confirmed
core/standards/ is compiled immutable reference data     core/standards/*   confirmed
a document stores a standard's DESIGNATION, not a value  HoleFeature.hpp:47-49,91-96 confirmed
ObjectReference = durable identity + untrusted locator   ObjectReference.hpp:48-57 confirmed
the layer table is core 0 .. renderer 6                  CheckLayering.cmake:23-38 confirmed
the checker fails the build on an unknown module         CheckLayering.cmake:69-70 confirmed
standards needs no layer entry (module = core)           0 layer_standards  confirmed
no existing material concept to collide with             audited            0 hits
```

## ARCHITECTURE GUARD

One test added, and only because ADR-026 cites a contract that nothing enforced.

`DocumentFile_NoDerivedGeometryIsPersisted` — the canonical/derived serialization
boundary. It regenerates the bracket model, **requires the body to have positive
volume and surface area** so that there is derived data available to leak, saves,
and asserts the file contains none of `volume`, `surface_area`, `surfaceArea`,
`centre_of_mass`, `center_of_mass`, `centerOfMass`, `centroid`, `mass`,
`inertia`, `moment_of_inertia` — then asserts it *does* contain
`"type": "extrude"`, so it is reading a real, non-empty document.

```text
result                              PASS
vacuous?                            no -- the fixture is asserted to HAVE
                                    derived geometry, and the file is asserted
                                    to contain intent
over-strict across the corpus?      no -- none of the ten words appears in any
                                    of the 32 committed .bcad models
implements any P15 behaviour?       no -- no material type, no new module; it
                                    pins a property of the EXISTING serializer
```

P15 is about to lean on this: mass cannot go stale precisely because neither
factor is persisted. The rule deserved a test before a milestone depended on it.

**No other guard was added, deliberately.** Every remaining candidate — a
`MaterialId` strong-type separation test, a property-state test — would require
introducing P15 production types, which this milestone forbids. A token test
would be volume, not evidence.

## ADVERSARIAL REVIEW

Twenty questions, each answered by a mechanism rather than an intention.

```text
 1 Two materials with the same name collapse into one identity?
   NO. Identity is MaterialId. The designation is free text and duplicable; the
   object name is unique but is a lookup handle, not identity.

 2 Renaming a material changes its identity?
   NO. MaterialId is allocated once and independent of both strings.

 3 Deleting material A makes an assignment adopt material B?
   NO. IdAllocator never reuses a value, so A's ID never names anything else.
   The assignment becomes unresolved, exactly as P14 references do.

 4 A library update silently changes a saved document?
   NO, structurally: no library lookup at load or solve time. This is the
   milestone's central decision (ADR-025).

 5 "Unknown density" becomes zero?
   NO. Unknown holds no Quantity and the property type does not convert to
   double.

 6 Missing E becomes zero stiffness?
   NO. requireProperty returns Result and the run fails naming the property.

 7 Mass is persisted and goes stale?
   NO. Mass is derived and never written -- now guarded by a test, not a habit.

 8 A solver invents a missing property?
   NO. No default, typical or fallback material is permitted anywhere above the
   materials module (ADR-028), and layering makes a private table a build error
   only if it tries to reach downward -- so the rule is also stated as a hard
   prohibition, because a solver CAN legally define its own constant. FLAGGED as
   the one item resting on discipline plus review rather than on the compiler.

 9 P17 keeps a second material database?
   Prohibited by ADR-028 and reviewable. Same caveat as 8.

10 A document-local material mutates a built-in entry?
   NO. Library entries are constexpr; there is nothing to mutate.

11 Configuration switching silently changes material?
   NO. A configuration overrides parameter values only, and a MaterialId is not
   one.

12 An occurrence overrides a part material accidentally?
   NO. There is no occurrence override in P15, and its absence is explicit.

13 A material assigned to an object whose semantics are undefined?
   NO. The assignment is document-level -- there is exactly one target and it is
   the part. The repository's own precedent for refusing a meaningless override
   is suppressComponent's typed refusal.

14 Different properties use incompatible unit systems?
   NO. Quantity<Dimension> is compile-time dimensional; a Pressure cannot be
   stored where a Density belongs. Serialization is SI throughout.

15 Provenance says one source while values came from another?
   This is the NORMAL case, not a bug, and the model states it: per-property
   provenance overrides a per-material default, and the effective provenance of
   any value is always answerable.

16 Temperature-dependent data later forces a change to identity?
   NO. Only a property's VALUE representation changes; identity, assignment and
   the three states are untouched, because no consumer reads a raw double.

17 Save/load loses material identity?
   NO. A material is a document object, and document object IDs already round
   trip -- Document_SaveLoad_PreservesStableIds is an existing qualified
   contract.

18 Duplicate display names break lookup?
   NO. Lookup is by MaterialId, or by the unique object name. Nothing looks up a
   material by designation, and ADR-025 forbids it.

19 Library absence triggers a name-based fallback?
   NO. There is no library lookup to fail, so there is no fallback path to
   write -- the failure mode is designed out rather than guarded.

20 Layering lets materials depend on FEA/thermal/CFD?
   NO. core/materials is inside core; the checker fails the build on an upward
   dependency and on an unknown module.
```

```text
questions:             20
findings:               1
blocking findings:      0
resolved:              19
deferred limitations:   3  (per-body material, occurrence override, electrical
                           property dimensions)
```

**THE ONE FINDING, recorded rather than smoothed over.** Questions 8 and 9 — a
downstream solver defining its own material constants — are **not** preventable
by the layering checker. Nothing stops P17 writing
`constexpr Pressure kSteelE = 210_GPa;` inside the analysis module: that is a
downward dependency on `core/units`, which is legal. ADR-028 prohibits it in
words and the review must enforce it. A mechanical guard is conceivable — a
source check for property-shaped constants above the materials module, in the
style of `CheckLayering.cmake` — and it is **not** built here because the
analysis modules do not exist yet, so it would have nothing to check. Recorded so
that the milestone which creates the first analysis module knows it inherits this
obligation.

## KNOWN LIMITATIONS

```text
per-body material                DEFERRED. Mechanism named: a body's persistent
                                 handle is its producing feature's ObjectId.
                                 Per-solid and per-face will never be offered.
occurrence material override     DEFERRED. Needs the external references ADR-003
                                 defers. Consequence stated: an assembly uses
                                 each part's own material, and two occurrences of
                                 one part always share a material.
electrical properties            NOT EXPRESSIBLE. Dimension has no electric
                                 current base dimension. A base-dimension change,
                                 not an alias. P15 does not need it; P19 might.
configuration-selected material  NOT SUPPORTED, and would need a new typed
                                 override kind beside suppression.
anisotropic elasticity           OUT OF SCOPE. The canonical set is isotropic
                                 (E, nu). Independent constants would be a
                                 different property set with its own ADR.
the "no second database" rule    rests on ADR-028 plus review, not on the
                                 compiler. See the adversarial finding.
library packaging                a large library may need to leave the binary.
                                 Whatever it does, the document never depends on
                                 it, so that is a packaging decision and not an
                                 architectural one.
```

## RESULT

```text
TASK:            P15-ARCH-001 -- Materials / Engineering Data Architecture
RESULT:          PASS
DECISIONS:       ADR-025, ADR-026, ADR-027, ADR-028
AUDIT:           no material concept exists (0 hits); the unit system is strong
                 and Density already exists; one real unit gap (no electric
                 current base dimension); two patterns supplied the design
                 (core/standards/ reference data, and the document-stores-a-
                 designation convention, the latter examined and deliberately
                 NOT followed for materials)
VALIDATION:      24 claims checked against the tree; 0 ADR claims rest on
                 infrastructure that does not exist
SOURCE CHANGE:   one architecture guard test, for a contract ADR-026 cites and
                 nothing enforced. No material type, no module, no layer change.
ADVERSARIAL:     20 questions, 1 finding, 0 blocking, 3 deferred limitations
RESULT:          every required question answered; none TBD
TODO:            P15-ARCH-001 -> [x]; next P15-UNITS-001, NOT started
```

**P15-ARCH-001 is complete.** The next milestone is `P15-UNITS-001`, and this
evidence tells it exactly what to do: add energy, power, specific heat, thermal
conductivity, thermal expansion and dynamic viscosity as aliases over the
existing base dimensions, and decide separately whether electric current becomes
a sixth base dimension.

## REVISION

```text
2026-09-26  Audited at d9739a6. ADR-025..028 recorded. One architecture guard
            added. ROADMAP.md corrected: it still said P14 was in progress.
            RESULT PASS.
```
