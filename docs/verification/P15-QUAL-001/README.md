# P15-QUAL-001 — Full P15 Qualification

**STATUS: PASS. P15 — Materials / Engineering Data — is QUALIFIED.**

Qualified on the FIRST attempt against an already-committed tree: 2814/2814 in Debug,
Release and Debug-shared each from clean, 36582 test executions, 0 failures, **0 warnings
in all six build and rebuild logs**, and canonical output byte-identical across the three
presets. 13 of 13 predecessors PASS with no defect waived. Full detail at the end.

## FINAL HEAD / TREE / ORIGIN

```text
FINAL_P15_HEAD   8d4b23c8aa058b28e537054c994ee587fe7b9041
FINAL_P15_TREE   1f6f2951389e7146eade2ec8a0679a64b484be5f
origin/main      8d4b23c8aa058b28e537054c994ee587fe7b9041   (equal)
branch           main
working tree     clean at the moment of the freeze
```

Qualification ran against an **already-committed tree**. The nine cross-milestone gates this
milestone adds were committed and pushed first, so nothing tracked changed after the freeze
except `docs/` and `TODO.md` — see QUALIFIED TREE VS COMMITTED TREE.

```text
environment
  branch / build     main, the -ext presets, BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%\bc-build
                     (OUTSIDE the synchronised folder, per INFRA-QT-DEPLOY-001)
  CMake              4.4.2, Ninja
  compiler           GNU 16.1.0, C++23, -Werror and the full warning set
  platform           Windows AMD64
  test framework     Catch2 3.16.0 (fetched)
  dependency prefix  %LOCALAPPDATA%\bettercad-deps\gnu-16-mingw-amd64
```

## SCOPE

Audit, reverification, cross-milestone integration, adversarial qualification, regression,
evidence freeze. **No features were added.** The only code this milestone contributes is
`tests/qualification/P15MaterialsQualificationTests.cpp` — nine gates that cross milestone
boundaries and that the brief requires as final gates rather than as inherited evidence.

## PREDECESSOR MILESTONE AUDIT

```text
Milestone         TODO      Evidence  RESULT  Regression  Adversarial  Limits  Final
P15-ARCH-001      16/16 [1] yes       PASS    n/a [2]     yes          yes     PASS
P15-UNITS-001     20/20     yes       PASS    yes         yes          yes     PASS
P15-MAT-001       18/18     yes       PASS    yes         yes          yes     PASS
P15-MECH-001      18/18     yes       PASS    yes         yes          yes     PASS
P15-THERM-001     14/14     yes       PASS    yes         yes          yes     PASS
P15-ASSIGN-001    16/16     yes       PASS    yes         yes          yes     PASS
P15-MASS-001      17/17     yes       PASS    yes         yes          yes     PASS
P15-CUSTOM-001    13/13     yes       PASS    yes         yes          yes     PASS
P15-PROV-001      14/14     yes       PASS    yes         yes          yes     PASS
P15-CMD-001       15/15     yes       PASS    yes         yes          yes     PASS
P15-PERSIST-001   17/17     yes       PASS    yes         yes          yes     PASS
P15-CLI-001       17/17     yes       PASS    yes         yes          yes     PASS
P15-REFMOD-001    21/21     yes       PASS    yes         yes          yes     PASS
                  -----
                  216 ticked, 0 open
```

`[1]` P15-ARCH-001 lives in TODO.md under `# DONE — P15-ARCH-001`, not a bare heading. A
first pass of this audit reported it missing because the matcher looked for the bare form.
**The repository was right and the audit script was wrong**; it is recorded because an audit
that silently mis-reads its own input is worth less than one that says where it slipped.

`[2]` P15-ARCH-001 produced ADR-025 to ADR-028 and no executable behaviour, so it carries no
three-preset regression. That is correct for an architecture milestone, not a gap.

No row is inconsistent. No predecessor defect is waived.

## TODO AUDIT

216 ticked boxes across the thirteen milestones, **0 open**. The 27 boxes that remain open in
TODO.md all belong to this milestone and are closed by it.

Marker search across all P15 production code and headers
(`src/core/materials/`, `src/features/material/`, `src/io/json/MaterialJson.cpp`,
`include/bettercad/core/materials/`, `include/bettercad/features/Material*.hpp`,
`include/bettercad/features/MassProperties.hpp`, `apps/bettercad_cli/Material*`):

```text
TODO FIXME HACK TEMP XXX "NOT IMPLEMENTED"      1 hit
```

The single hit is `ThermalProperties.hpp:107`:

> NOT IMPLEMENTED HERE, and deliberately so: P15 stores constant properties.

Classified: **a documented scope statement, not a defect.** Temperature-dependent property
laws are explicitly outside P15, the contract for them is recorded, and a file containing one
is *rejected* rather than misread as a constant (P15-PERSIST-001). It contradicts no checked
requirement.

## ADR AUDIT

Five ADRs, all present, all referenced by production code AND tests:

```text
ADR   subject                                                    refs in code/tests
025   a material is a document object; the library is reference data      18
026   an assignment is intent and mass is derived                         19
027   a property is known, unknown or derivable                           30
028   provenance is per property; solvers hold no material data           22
029   electrical resistivity is a scoped strong type                       6
```

Each decision the brief names is implemented and consistent across code, tests, persistence
and the CLI:

```text
MaterialId                       ADR-025  Id<MaterialIdTag>, widens to ObjectId, never back
library vs document ownership    ADR-025  import COPIES; nothing consults the library later
clone semantics                  ADR-025  new identity, values copied, origin carried over
unknown properties               ADR-027  Known / Unknown / Derived, never zero
constant vs future T-dependent   ADR-027  constants stored; a T-law file is REJECTED
assignment target / precedence   ADR-026  one optional MaterialId on the Document
configuration / occurrence       ADR-026  assignment is configuration-INDEPENDENT
derived vs supplied              ADR-027  G and K derived, with NO SLOT to store them
persistence semantics            ADR-024  save intent, recompute derived; version unchanged
```

**No stale ADR contradicts final behaviour.** The one place where a later milestone could
have drifted is the derived pair: ADR-027 gives G and K no storage slot, and the persisted
key table still has 9 entries for 11 property kinds *because of that*. P15-CLI-001
deliberately declined to lift that table into core for exactly this reason.

## ARCHITECTURE

Each P15-ARCH invariant re-checked against the final tree, with a bypass search:

```text
identity != display name          a rename changes the name and nothing else; tested
library material != local clone   the library is a compiled-in constant; the clone is a
                                  document object with its own ID
unknown != zero                   Known/Unknown/Derived; UNKNOWN printed, never 0
derived != canonical              no slot for G/K; no mass, centroid or inertia persisted
geometry is the volume authority  partMassProperties integrates the regenerated body
density + geometry give mass      m = rho V, refused if either is absent
provenance representable          per property, two-level, eight source kinds
saved model is library-independent an origin key for an entry THIS BUILD LACKS still loads
consumers use canonical APIs      require*() entry points, not field reads
```

Bypass search over the final tree:

```text
OCCT outside src/core/geometry/occt/          none (one CMakeLists listing the dir)
material data in renderer/scripting/drawing/assembly   none
Qt outside src/renderer/ and apps/bettercad/  none
a second material store, registry or cache    none (one static_assert on a type name)
```

## UNITS

`Quantity<Dimension>` with five base exponents and no electric current. Every material
property is either a `Quantity`, a scoped strong type, or a value+scale pair:

```text
Density, ElasticModulus/Stress, ThermalConductivity, SpecificHeatCapacity,
ThermalExpansionCoefficient, Temperature, Mass, Volume, MassMomentOfInertia,
VolumeSecondMoment          Quantity<D>, SI internally, converted only at boundaries
PoissonRatio, Elongation    scoped strong types, dimensionless, explicit construction
Hardness                    a value AND its scale; a bare number cannot be constructed
ElectricalResistivity       a scoped strong type (ADR-029), ohm metres, no Quantity
```

Non-finite values are refused by the property layer; a dimensionally incompatible assignment
does not compile. **124 compile-fail cases across 9 groups** guard this, including
`UnitsMisuse`, `MechanicalMisuse`, `ThermalMisuse`, `MassMisuse` and `IdsMisuse`.

There is **no affine unit conversion anywhere**: a `UnitScale` is a ratio, so an absolute
temperature cannot be shifted by a display conversion, and `degC` is not a unit the catalog
has. P15-CLI-001 declined to add one rather than implement it in the CLI alone.

## SAME-DIMENSION SEMANTIC AUDIT

Seven properties share `Quantity<pressure>` — E, G, K, yield, UTS, compressive and shear
strength. **The type system cannot tell them apart**, and P15-CLI-001 found that out when two
`storedAndValid` overloads collided because `ElasticModulus` and `Stress` are the same type.

What keeps them distinct is a NAME, in three independent tables:

```text
enumerator            MechanicalPropertyKind, 11 values
persisted key         5 distinct keys for the 5 STORED pressure properties
                      (no shear_modulus, no bulk_modulus -- ADR-027)
CLI code              7 distinct codes (the 5 stored plus the 2 derived)
consumer requirements distinct sets; a UTS does NOT substitute for a yield strength
```

Gate `P15Qual_PressureDimensionPropertiesAreNeverInterchangeable` gives the five stored ones
**five different values** and requires each to survive on its own slot through the file and
through the CLI, and requires a yield-strength consumer to report the *yield* strength
missing when only that one is absent.

## MATERIAL IDENTITY

```text
MaterialId stable                         tested
rename does not change the ID             tested; the whole definition is unchanged too
property edits do not change the ID       tested
provenance edits do not change the ID     tested
clone gets a NEW ID                       tested
undo restoration gets the ORIGINAL ID     tested through CommandHistory
save/load preserves the ID                tested
duplicate designations stay distinct       tested (names cannot duplicate at all)
a deleted ID never silently rebinds       THE FINAL GATE, below
```

167 tests across the suite mention identity, rename, clone, duplicate, rebind or unresolved.

## NO-REBINDING

The brief asks for two materials both **named** "Steel". That is impossible, and the
impossibility is the product's: an object name is an identifier, unique across objects and
parameters, re-checked on every rename. The real duplicate-label case is the **designation**,
which is free text. The gate uses it, with **different densities**, so a rebind would change
the mass and not merely the identity.

```text
A = SteelA, designation "Steel", 7800 kg/m^3      id A
B = SteelB, designation "Steel", 2700 kg/m^3      id B
P -> A                                            mass 7.8 kg (100^3 mm)

delete A                  -> Unresolved, material = A, effectiveMaterial() == nullptr
                             a mass request FAILS; it does not use B's density
save / load               -> Unresolved, material = A
save / load again         -> Unresolved, material = A
CLI material-effective    -> exit 1, material_unresolved, names material:A, never SteelB
create C, designation "Steel"
                          -> Unresolved, material = A
undo the delete           -> Resolved, material = A, mass 7.8 kg again
```

**Mutation-tested.** A three-line change making a dangling assignment rebind to any surviving
material fails **22 assertions** in this gate. The mutation was reverted and the file verified
byte-identical to the commit.

## MECHANICAL

Density, E, nu, the four strengths, elongation and hardness with its scale; an isotropic
model. Validation: E > 0, `-1 < nu < 0.5` with both ends excluded (at 0.5 the bulk modulus
divides by zero, at -1 the shear modulus does), non-finite refused, Unknown left Unknown.
Every problem is reported, not the first.

### Derived moduli

`G = E / [2(1+nu)]` and `K = E / [3(1-2nu)]`, validated against **hand-computed** values for
three (E, nu) pairs including `nu = 0`, where G = E/2 and K = E/3:

```text
E = 70 GPa,  nu = 0.33   G = 26.315789473684212 GPa   K = 68.62745098039216 GPa
E = 200 GPa, nu = 0.28   G = 78.125 GPa               K = 151.51515151515152 GPa
E = 90 GPa,  nu = 0      G = 45 GPa                   K = 30 GPa
```

They are **Derived** and not Known, and E and nu come back Supplied — so the distinction is
real rather than a label everything wears. A *supplied* G cannot conflict with them because
**ADR-027 gives it no slot**; the inconsistency the product does detect is an ultimate
tensile strength below the yield strength, which is reported and never silently corrected.

## THERMAL

One canonical density, living with the mechanical properties, read by thermal consumers —
there is no second density to diverge. Conductivity, specific heat, expansion, melting
temperature and electrical resistivity, with per-property reference temperatures.

Stored values satisfy their defining relations, checked against arithmetic done by hand:

```text
dL = alpha L0 dT     23e-6 /K, 2 m, 100 K  ->  4.6 mm
Q  = m cp dT         5 kg, 900 J/(kg K), 40 K  ->  180 000 J
q  = -k grad(T)      k stored in W/(m K), which is what the consumer asks for
```

Absolute temperatures stay absolute: 933 K is 933 K, and no offset exists anywhere that could
shift it. No missing value is inferred.

## ASSIGNMENTS

One optional `MaterialId` on the Document (ADR-026). Four states — Unassigned, Resolved,
Unresolved, Invalid — never collapsed into one. Resolution happens in one place, by ID, and
nothing resolves by designation, name, position or similarity.

`Unassigned` is a resting state and not a fault. The direct canonical assignment and the
derived effective material are separate calls: `materialAssignment()` for the state,
`effectiveMaterial()` for the material, `requireEffectiveMaterial()` to fail with a reason.

**Configuration-independent**, and tested by switching none → Tall → Plain → Tall → none with
the identity, assignment and designation unchanged at every step. There is no per-occurrence
assignment to check because `ComponentDefinition` has no material field — P15-ASSIGN-001 has
a compile-fail case proving the field is *absent* rather than merely unused, which is a
stronger statement than any runtime check.

### Regeneration

A geometry edit leaves the assignment untouched and the mass follows the new geometry. A
**failed** regeneration keeps the canonical assignment and **refuses** a derived answer: a
200 mm bore through a 120 mm cylinder gives `'Bore' (hole, object:6) has no body`, not the
previous mass.

## MASS PROPERTIES

Geometry is the authority: `partMassProperties` integrates the regenerated body. No bounding
box, no display mesh — and no OCCT outside `src/core/geometry/occt/`.

Reference frame stated, both tensors supplied (centroidal and about the origin), products of
inertia in the negated-product matrix convention, with the convention documented so a caller
assembling a matrix cannot get the sign wrong silently.

**Assembly aggregation is out of scope and is marked N/A, not PASS**: no aggregation exists in
`include/` or `src/`, `ComponentDefinition` has no material field, and a Document holds one
material. P15-CLI-001 and P15-REFMOD-001 both declined to invent it.

## INDEPENDENT ANALYTICAL VALIDATION

Every expected value comes from `tests/reference/Analytic.hpp`, whose only includes are
`<array> <cmath> <cstddef> <numbers> <vector>` — **no BetterCAD header, so it cannot call the
kernel, `massProperties` or the CLI even by accident**. See
[P15-REFMOD-001/ANALYTICAL_TABLES.md](../P15-REFMOD-001/ANALYTICAL_TABLES.md) for the
per-quantity tables.

```text
asymmetric cuboid    200x300x500 mm at 2700   V = 3e7 mm^3, m = 81 kg EXACTLY
                     Ixx/Iyy/Izz = 2.295 / 1.9575 / 0.8775 kg m^2, three DISTINCT
solid cylinder       r 50, h 400 at 7800      Iaxis = m r^2/2, Itrans = m(3r^2+h^2)/12
hollow tube          Ro 60, Ri 40, h 300      V = pi(Ro^2-Ri^2)h = 1884955.59 mm^3,
                                              44% less than the bounding cylinder
transformed body     90 deg about X + offset  I' = R I R^T moves the axis moment zz -> yy;
                                              Ixz = -0.735 kg m^2 about the origin
assembly aggregate   N/A -- no aggregation exists
```

**Worst relative error anywhere in the suite: 3.28e-14**, against a tolerance of **1e-10** —
four orders of margin, and orders below any real defect: a transposed inertia axis on the
cuboid is a 17% error, a bounding-cylinder volume on the tube is 80%.

## PROVENANCE

Per property, two-level (a property's own, falling back to a material default), with eight
source kinds, a free-text source, a standard, a reference, a revision that is metadata and
never version logic, and a validated ISO 8601 date.

**A known value with unknown provenance is not a missing value**, and both answers are
asserted: a yield strength with a number and no citation stays Known with an empty record.

A provenance edit changes no number and no identity — tested. Editing a *value* clears the
citation that described the old number, because a citation for a number that is no longer
there is a lie. Citations survive a file keyed by property name, so no reordering can
reattach one, and three different source kinds on three properties make a swap visible.

## COMPLETENESS / MISSING DATA / NO FABRICATION

Consumer-specific, never one global flag. The requirement sets, read out of the source:

```text
MassProperties              density
FeaLinearStatic             E, nu
FeaLinearStaticWithGravity  density, E, nu
FeaYieldStrength            E, nu, yield
ThermalSteady               k                       (conductivity ONLY)
ThermalTransient            density, k, cp
ThermoMechanical            E, nu, alpha
```

Three states are reachable and all three are exercised by the reference suite: **Ready**,
**Incomplete** (something required is absent) and **Invalid** (everything required is present
and two values contradict). Missing outranks invalid, because a caller told "incomplete" knows
to supply data and cannot act on "invalid" for a value that is not there yet.

### No-fabrication audit

A search of all P15 production code for `value_or(...)`, default densities, default moduli,
`0.3`, `7850`, `2700`, fallback or generic materials returned **one** hit:

```text
src/features/material/Materials.cpp:71
    material.setDefinition(definition).value_or(false)
```

Classified: **not a fabricated engineering value** — `false` is a *changed* flag, not a
property — and **not a swallowed error**: `setMaterialDefinition` validates the same
definition with the same pure function *before* touching the document, so the inner call
cannot fail if the outer one passed. "Unreachable" was a claim, so a gate now checks it:
`P15Qual_ARejectedDefinitionIsReportedAndNeverASilentNoChange` requires an error (not
`false`) for `nu = 0.5` and for a negative density, requires the material to be unchanged,
and requires a genuine no-op to report `false` **without** an error — which is what makes the
distinction meaningful.

Explicit missing-data fixtures, with no defaults inserted:

```text
FEA              E known, nu UNKNOWN      -> Incomplete, missing PoissonRatio; no nu = 0.3
ThermalTransient rho, cp known, k UNKNOWN -> Incomplete, missing ThermalConductivity; no k
Mass             density UNKNOWN          -> refused, naming density; never a zero mass
```

## CUSTOM MATERIALS

Create from scratch, import a library entry, clone, edit, remove a property. A clone always
gets a new identity; the origin key is carried over because cloning does not change where the
values came from, and that is not a live link — the library is a compiled-in constant and
nothing consults it on the document's behalf, not at load, not while solving.

**Deep independence**, gated: a library import given full values and a citation, cloned, then
the clone edited across metadata, mechanical, thermal, provenance **and** a property removed
outright. The source is byte-identical afterwards, the library entry unchanged, and both keep
their own state through a file.

## COMMANDS / UNDO / REDO

Five commands, each with execute, undo, redo, failure atomicity and identity preservation.

The brief's exact chain, through the production `CommandHistory`:

```text
Create A -> Edit A -> Assign P to A -> Edit A's density -> Remove the assignment
snapshot the canonical final state
undo all   -> canonical state EQUALS the initial state exactly; 0 materials
redo all   -> canonical state EQUALS the final snapshot; the SAME MaterialId, not a new one
```

The snapshot is materials, definitions and the assignment — deliberately **not** the mass,
because a snapshot containing it could not tell a restored value from a recomputed one.

### No derived state in the history

Command payloads carry no mass, volume, centre of mass, inertia, derived G or K, completeness
report or effective-material cache. Proved by behaviour rather than by inspection: after
undoing the density edit the mass follows the density *back*, which is only possible if it was
recomputed. 7.8 kg → 8.0 kg → undo → 7.8 kg.

## PERSISTENCE

Identity, metadata, mechanical, thermal, assignments, provenance, custom materials, the exact
known/unknown pattern, and library-versus-local semantics all round-trip. `save → load → save
→ load` leaves the canonical state stable and the two files **byte-identical**.

### Derived-state exclusion

A saved document that has had its mass, elastic constants, general report and consumer
completeness all computed first contains none of them. Twelve forbidden tokens are absent, and
so are the computed numbers themselves. After a load every derived value **recomputes** to the
same answer, and the derived moduli are still Derived and not Known.

### Malformed input

36 material file tests, including a 19-case malformed matrix, non-finite tokens, a `1e400`
overflow, duplicate IDs, a missing ID, a structurally malformed assignment, and an unknown
future field that is **rejected rather than ignored**. A failed load leaves the caller's
document untouched — load is atomic because it returns a new Document.

### Backward compatibility

A document from before materials existed loads with zero materials and **no fabricated
default**; version 1 still loads; version 3 is refused by name. The schema version did not
change, because adding an object type and an optional field needs no bump — and a golden-text
test asserts the exact bytes of a document with no materials, so no pre-P15 file's
representation moved by a single byte.

## CLI

Twelve verbs over the qualified core: list, show, effective, mass-properties, completeness,
and seven edits. The CLI introduces no material identity, default, validation, assignment
semantics or property derivation of its own, and holds no OCCT or JSON header.

### CLI / core differential

Each number is **parsed** out of the CLI's output and compared for **exact equality** with the
core's own double, then against closed form — exact rather than within a tolerance because the
CLI prints shortest-round-trip text, so the double recovered from stdout is the double the
core computed. A tolerance would have hidden a unit error; exact equality cannot.

Compared: material enumeration, MaterialId, density, E, nu, derived G, k, cp, the effective
assignment, mass, volume, centroid, inertia, the missing-property report and the state code.

### Exit codes

```text
answered, good                 0
answered, not ready            1, report on stdout
could not answer               1, stdout EMPTY, code on stderr
unreadable command line        2
edit refused by the document   1, code on stderr
mass unavailable for a body    1
```

Machine codes are stable and derived from core's own vocabulary. `material_ambiguous` has its
own `ErrorCode` so that "two materials share this designation" and "that object is a sketch"
cannot arrive indistinguishable.

### Multi-process E2E

Five processes sharing nothing but bytes on disk: the runner builds and saves eight reference
models; separate `bettercad-cli` invocations then inspect one, **edit** it, re-weigh it
(4.608 kg, closed form), gate another on a consumer requirement, and read the edit back. Each
step asserts its own exit code — there is no pipeline to mask one, no `|| true`, no shell. The
binary is `$<TARGET_FILE:...>`, so there is no PATH to be ambiguous.

## REFERENCE MODELS

Eight documents under six IDs; see
[P15-REFMOD-001/README.md](../P15-REFMOD-001/README.md).

```text
Model      Geom Assign Mass CM  Inertia Mech Therm Prov Missing Save CLI Analytic PASS
RM-MAT-01  yes  yes    yes  yes yes     yes  yes   yes  N/A     yes  yes yes      yes
RM-MAT-02  yes  yes    yes  yes yes     yes  yes   N/A  N/A     yes  yes yes      yes
RM-MAT-03  yes  yes    yes  yes yes     yes  yes   N/A  N/A     yes  yes yes      yes
RM-MAT-04  yes  yes    yes  yes per-part yes yes   N/A  N/A     yes  yes yes      yes
RM-MAT-05  yes  yes    yes  yes yes     yes  yes   yes  N/A     yes  yes yes      yes
RM-MAT-06  yes  yes    yes  yes yes     yes  yes   N/A  yes     yes  yes yes      yes
config     yes  yes    yes  N/A N/A     N/A  N/A   N/A  N/A     yes  N/A N/A      yes
```

`N/A` where the architecture legitimately excludes the check or another model carries it:
`Missing` is N/A for the five complete materials because an absent diagnostic is the correct
result there and RM-MAT-06 is the model that carries it; RM-MAT-04's aggregate mass, centre of
mass and inertia are N/A because no aggregation exists.

**A reference model found a production defect.** RM-MAT-03 was the first fixture in the
repository to combine a feature chain with a mass, and it showed `mass-properties` listing
consumed intermediate bodies as results — reporting the un-bored blank at 26.46 kg before the
real 14.70 kg. Fixed in P15-REFMOD-001 with `features::resultFeatures()`, the product's own
answer, and pinned by a mutation-verified regression test.

## HARNESS SELF-TEST

`verify-harness.cmd` was run before the final qualification. Pointed at a preset that does not
exist and a repeat filter that matches nothing, it required a non-zero exit: **2 stages
failed, exit 2**, and it confirmed that a zero-match repeat filter is counted as a failed
stage. A gate whose result has to be read out of a log by eye is not a gate.

## DEBUG / RELEASE / DEBUG-SHARED QUALIFICATION

Each preset from CLEAN: configure, remove every build output, rebuild with `-Werror`, rebuild
again to nothing, then run the whole suite. Qualified on the **first attempt**.

```text
Preset             Discovered  Passed  Failed  Skipped  Warnings  configure clean build no-op ctest
debug-ext                2814    2814       0        0         0      0      0     0     0     0
release-ext              2814    2814       0        0         0      0      0     0     0     0
debug-shared-ext         2814    2814       0        0         0      0      0     0     0     0

qualification finished 13:25:50, 0 stage(s) failed          (09:48:29 -> 13:25:50, 3h 37m)
```

2814 = the 2805 of P15-REFMOD-001 plus the nine cross-milestone gates this milestone adds.
All three presets discover the same 2814 and the same **426** tests of P15's own subject.

Counts are exact, taken from the ctest logs, not approximated.

### Fresh-binary proof

```text
Preset             source HEAD  source tree  binaries built   test count  build directory
debug-ext          8d4b23c      1f6f2951     10:06 (stage 10:07)     2814  %LOCALAPPDATA%\bc-build\debug-ext
release-ext        8d4b23c      1f6f2951     10:41 (stage 10:42)     2814  %LOCALAPPDATA%\bc-build\release-ext
debug-shared-ext   8d4b23c      1f6f2951     11:11 (stage 11:12)     2814  %LOCALAPPDATA%\bc-build\debug-shared-ext
```

Every binary's timestamp falls inside its own preset's build stage, after that preset's tree
was cleaned. The no-op rebuild stage then found nothing left to build in any of the three, so
the binaries under test are the ones this tree produces. The CLI is invoked by
`$<TARGET_FILE:bettercad_cli>` throughout — an absolute path, so there is no PATH to be
ambiguous and no installed or stale copy that could answer.

### The nine final gates, in all three presets

```text
P15Qual_DeletedMaterialNeverRebindsThroughAnySequence            passed x3
P15Qual_MissingPropertiesAreReportedAndNeverDefaulted            passed x3
P15Qual_PressureDimensionPropertiesAreNeverInterchangeable       passed x3
P15Qual_DerivedModuliMatchClosedFormAndAreNeverSupplied          passed x3
P15Qual_StoredThermalPropertiesSatisfyTheirDefiningRelations     passed x3
P15Qual_TheFullCommandChainUndoesAndRedoesExactly                passed x3
P15Qual_ThePersistedFileHoldsNoDerivedEngineeringAuthority       passed x3
P15Qual_CloneAndSourceShareNothingAfterAnEdit                    passed x3
P15Qual_ARejectedDefinitionIsReportedAndNeverASilentNoChange     passed x3
```

## DETERMINISM

```text
Gate / artifact                     Repetitions   Identical / equivalent?   PASS
whole suite, release-ext            5 x 2814      14070 passing executions  yes
whole suite, debug-ext              5 x 2814      14070 passing executions  yes
material identity (IDs, allocator)  5             identical                 yes
serialization (saved bytes)         2 builds x 3  byte-identical            yes
CLI structured output               3 presets     byte-identical            yes
reference-model analytics           3 presets     identical to 17 digits    yes
completeness ordering               5             identical                 yes
validation issue ordering           5             identical                 yes
```

`selects 2814` is written to `qualification-times.txt` by the harness for **both** repeat
stages, so the zero-match guard is satisfied with a recorded number rather than an assumption.
14070 = 2814 x 5 exactly, counted from the logs.

**36582 test executions in total, 0 failures.**

No result shopping: this is the first and only run of this tree, and every earlier run in the
phase that was voided is recorded as void in its own milestone.

## CROSS-PRESET EQUIVALENCE

Canonical outputs compared across Debug, Release and Debug-shared on this tree:

```text
artifact                                   debug vs release   debug vs debug-shared
reference-model numerics (17 digits)       IDENTICAL          IDENTICAL
material-show structured output            IDENTICAL          IDENTICAL
mass-properties structured output          IDENTICAL          IDENTICAL
material-completeness structured output    IDENTICAL          IDENTICAL
material_block.bcad bytes                  IDENTICAL          IDENTICAL
material_tube.bcad bytes                   IDENTICAL          IDENTICAL
material_custom.bcad bytes                 IDENTICAL          IDENTICAL
material_incomplete.bcad bytes             IDENTICAL          IDENTICAL
```

**Byte for byte, to the last digit.** Not "equal within a tolerance" and not "materially the
same" — the same bytes and the same doubles. No Release value was accepted as different
because of floating point.

## WARNING AUDIT

```text
build-debug-ext.log        0        rebuild-debug-ext.log        0
build-release-ext.log      0        rebuild-release-ext.log      0
build-debug-shared-ext.log 0        rebuild-debug-shared-ext.log 0
```

**0 warnings in all six build and rebuild logs**, with `-Werror` and the project's full
warning set — `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wold-style-cast -Wdouble-promotion -Wsuggest-override -Wduplicated-branches -Wlogical-op`
and more. So the figure is 0 unexpected warnings out of 0 total: nothing is documented as
tolerated because nothing was emitted.

**No replace fault anywhere** — zero occurrences of "Permission denied" or "cannot replace"
across every log. The external build infrastructure of INFRA-QT-DEPLOY-001 held for the whole
3h 37m.

## FINAL ADVERSARIAL REVIEW

**[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md)** — a fresh cross-milestone review, 22
attacks, 3 findings, **0 new production defects**.

## QUALIFIED TREE VS COMMITTED TREE

The source fingerprint is **eight paths**, computed by the harness from a scratch index so
that untracked new files count:

```text
apps  include  src  tests  examples  cmake  CMakeLists.txt  CMakePresets.json
```

`docs/` and `TODO.md` are outside it, and that is **checkable rather than asserted**: the only
occurrences of `docs/` anywhere in the build system are two comments, one in
`cmake/BetterCADBuildLocation.cmake` and one in `tests/CMakeLists.txt`. No target consumes
them, no test reads them, nothing is generated from them. `TODO.md` appears in no build file at
all.

The circularity is avoided rather than reasoned around: **the nine gates this milestone adds
were committed and pushed before the qualification started**, so the harness's recorded
candidate is `8d4b23c` — an already-pushed commit — and its eight recorded tree IDs are the
eight `git rev-parse HEAD:<path>` values of that commit. Writing this document and ticking
TODO.md afterwards cannot change any of them.

```text
committed, then qualified        apps include src tests examples cmake
                                 CMakeLists.txt CMakePresets.json
changed after the freeze         docs/verification/P15-QUAL-001/  TODO.md
in the fingerprint?              no                               no
```

## KNOWN LIMITATIONS

Carried out of P15, each recorded in its own milestone and none contradicting a checked
requirement:

1. **A configuration override does not rebuild geometry.** `mass-properties` therefore
   **refuses** under an active configuration that overrides a parameter, rather than reporting
   the base configuration's volume as if it were the override's. The refusal is pinned in
   process, through the CLI, and by a reference model. Fixing the regeneration defect removes
   the guard and its tests.
2. **A document save can still lose to a file synchroniser** (the carried FileIo replace
   defect). Outside P15's scope and arguably ahead of the next phase, because it can lose a
   user's work.
3. **No assembly mass aggregation**, and so no aggregate centre of mass or inertia, no
   per-occurrence material and no suppression-affects-mass case. Three verified architectural
   absences.
4. **Temperature-dependent property laws are not represented.** Constants are stored; a file
   containing a law is rejected rather than misread.
5. **The built-in library carries no property values** — four entries with designations,
   standards and families only.
6. **Command history is not persisted.**
7. **No locale is exercised**; the number-formatting audit is by construction.
8. **An ad-hoc `ctest -R` by hand is not protected against a zero-match filter** the way the
   harness is; the protection comes from `noTestsAction: error` on the test preset.

## FINAL RESULT

```text
TASK:            P15-QUAL-001 — Full P15 Qualification
RESULT:          PASS
FINAL HEAD:      8d4b23c8aa058b28e537054c994ee587fe7b9041
FINAL TREE:      1f6f2951389e7146eade2ec8a0679a64b484be5f
PREDECESSORS:    13 of 13 PASS, 216 ticked boxes, 0 open, none waived
SCOPE:           audit and reverification. The only code added is nine
                 cross-milestone gates in tests/qualification/.
TESTS:           2814/2814 in Debug, Release and Debug-shared, each from clean;
                 14070 = 2814 x 5 in both determinism presets; 36582 executions,
                 0 failures; 0 warnings in all six build and rebuild logs.
VALIDATION:      expected values from a header with NO BetterCAD include; worst
                 relative error 3.28e-14 against 1e-10; cross-preset output
                 byte-identical.
EVIDENCE:        this directory, ADVERSARIAL_REVIEW.md, qualification/ for all
                 20 stage logs and the tree fingerprints.
TODO:            updated -- 27 boxes ticked; P15 marked QUALIFIED.
```

**P15 — Materials / Engineering Data — is QUALIFIED.**

Thirteen milestones, from an architecture audit that found no material concept at all to a
reference suite validated against closed form, and a phase gate that re-ran the whole product
three times from clean and then five times over twice more.

**What this gate actually found.** No new production defects — but three findings worth more
than a clean sheet:

* The single no-fabrication hit in all of P15's production code was **not** a fabricated
  value and **not** a swallowed error, and I could say why. But "unreachable" was my reasoning,
  not the suite's, so it is now a gate: a rejected definition must report an error, a no-op
  must report `false` **without** one, and the material must be unchanged in both cases.
* **My own audit script mis-read its own input**, reporting P15-ARCH-001 as absent because it
  looked for a bare heading where the repository has `# DONE — P15-ARCH-001`. A phase gate that
  had trusted "row missing → BLOCKED" would have blocked P15 over a regex.
* **Two of the brief's mandatory fixtures cannot be constructed**, and the impossibilities are
  the product's: two materials cannot share a NAME, and a supplied shear modulus has no slot.
  Both substitutes are stronger than what was asked for — a duplicate designation with
  differing densities changes an engineering answer, which a duplicate name would not.

**What the phase found in itself, earlier.** One production defect, found by a fixture rather
than a review: RM-MAT-03 was the first thing in the repository to combine a feature chain with
a mass, and it caught `mass-properties` reporting a consumed intermediate — the un-bored blank
— as a body of the part. Fixed in P15-REFMOD-001 and pinned by a mutation-verified regression
test. 2764 tests had not found it.

**Three qualifications in this phase were voided and re-run** rather than argued around: a
dll-boundary defect found only by the shared build, one blank line at a file's end, and a test
of mine that was not idempotent. Each cost three hours and each is recorded in its own
milestone, including P15-CLI-001's void attempt, which is kept **with its diagnosis** because
it is evidence the determinism gate works.

## REVISION

First revision. Qualified on the first attempt against an already-committed tree; no
fingerprinted path changed after the freeze.
