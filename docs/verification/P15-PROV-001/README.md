# P15-PROV-001 — Provenance, completeness and consumer requirements

**STATUS: PASS**, on the second qualification attempt. 2639/2639 in each of three
presets, 0 warnings, 34307 test executions, 0 failures. The first attempt failed on
the shared build and is recorded in [qualification-void/](qualification-void/README.md);
full detail at the end.

**TASK** — where each property value came from, what a consumer needs, what is
missing, and whether the supplied data contradicts itself. Authorized by the
P15-PROV-001 brief; the model was fixed by ADR-028.

---

## BASELINE

```text
git status --short   clean
HEAD                 49d6e51a4db1f8b62afc757279fbe3562acce9b9
origin/main          49d6e51a4db1f8b62afc757279fbe3562acce9b9   (equal)
HEAD^{tree}          bfbb382efcb237439278249ded0e42301d0ebbdd
git log -1           49d6e51 BetterCAD: implement custom material cloning and overrides
suite before         2589 tests
```

## PREREQUISITES

All eight verified complete in `TODO.md` — 132 `[x]`, **0** open boxes between them —
each recording `RESULT: PASS` with evidence:

```text
P15-ARCH-001 16   P15-UNITS-001 20   P15-MAT-001 18   P15-MECH-001 18
P15-THERM-001 14  P15-ASSIGN-001 16  P15-MASS-001 17  P15-CUSTOM-001 13
```

The external build path is still the qualified one: all three `-ext` presets are
defined and their build trees exist under `%LOCALAPPDATA%\bc-build`, outside the
synchronised folder (INFRA-QT-DEPLOY-001).

## SCOPE

Added: the per-property provenance model with a material-level default, a `Date`
value type, source kinds, consumer requirement sets, a structured completeness
report, consistency and traceability reporting, and the document-level entry points.
430 lines changed across 8 files, plus 2502 new — 1089 of product, 1413 of tests.

Not added, deliberately:

- **No CFD consumer.** ADR-028 names P19 as a future consumer and defines no
  requirements for it; inventing them would be guessing at physics. A compile-fail
  case proves `ConsumerKind::Cfd` does not exist.
- **No bibliographic management.** No author lists, no DOIs, no citation formatting.
  The goal is trustworthy engineering metadata, not a reference manager.
- **No temperature-dependent property tables.** ADR-027's extension path is a later
  milestone; `referenceTemperature` already records the state a constant was measured
  at, and that was P15-THERM-001's.
- **No provenance requirement in any consumer.** Traceability and numerical
  completeness are separate questions, and conflating them would block solvable work.
- Nothing from P15-CMD-001, P15-PERSIST-001 or later.

## EXISTING PROVENANCE AUDIT

The vocabulary the brief lists, searched across `src/` and `include/`:

| Concept | Existed? | Where |
| --- | --- | --- |
| material-level origin | YES | `MaterialDefinition::origin` — library key + revision (ADR-025) |
| "the state it was measured at" | YES | `MaterialProperty::referenceTemperature()` (P15-THERM-001) |
| `SourceKind` | no | new |
| a date type of any kind | **no, nowhere in the repository** | new `materials::Date` |
| `source` / `standard` / `reference` / `revision` | no | new |
| `CompletenessReport` / `ConsumerKind` | no | new |
| `requiredProperties` / `missingProperties` | no | new |
| per-consumer requirement functions | **YES — six of them** | `requireDensity`, `requireLinearElasticConstants`, `requireThermalConductivity`, `requireTransientConductionProperties`, `requireThermalExpansion`, plus `requireEffectiveMaterial` |

`MaterialProperty.hpp` already said where this milestone would land: "Provenance, in
ADR-028's sense of 'the state it was measured at'. The rest of provenance — source,
standard, revision, date — is P15-PROV-001."

**The most important audit finding is the last row.** Six functions already encode
what each consumer needs. A second table describing the same thing would drift, so
`requiredProperties()` is cross-checked against them by test rather than trusted —
see CONSUMER REQUIREMENT MODEL.

## PROPERTY PROVENANCE MODEL

ADR-028 chose the shape and this implements it exactly: **per property, with a
material-level default.**

```text
per property   its own PropertyProvenance, when it has one
per material   provenance.material, the default for properties that state none
effective      the property's own if non-empty, otherwise the material's
```

A property's own record **overrides** the material's, and the effective provenance of
a value is always answerable. So one material legitimately says three different
things about where its numbers came from — a density from a datasheet, a modulus from
a handbook, a conductivity measured internally — which ADR-028 calls "the normal case
and not a bug to prevent".

This is the **only inheritance anywhere in the material model**, and it is deliberate.
Property *values* never inherit (ADR-025, P15-CUSTOM-001); provenance does, visibly,
because a single datasheet should not have to be cited nine times.

### Why provenance is not a field on `MaterialProperty`

Provenance needs strings, and `MaterialProperty`'s factories are `constexpr`. Adding
a `std::string` member would silently strip that from three already-qualified
milestones' API surface for no benefit. Ordered maps beside the values are also sparse
by nature and deterministic to enumerate.

The cost is that a value and its provenance are **separable and can fall out of
step**, which is a real risk and is answered rather than ignored: removing a property
removes its provenance, editing a value clears the provenance that described the old
number, and an orphan citation is reported as an issue. All three are tested, and the
middle one is mutation-tested.

## SOURCE KIND

```text
Unspecified        nobody said -- the default, and NOT a defect
LibraryReference   imported from a BetterCAD library entry
ManufacturerData   a supplier's published data for their own product
Standard           a published standard; `standard` should name it
Handbook           nominal values for the alloy in general
Measured           a test of THIS material
Calculated         computed by the user from other measurements
UserEntered        typed in, no external citation -- honest, and not the same as
                   Unspecified: a person chose this number
```

Eight kinds, each with a distinct name (asserted). **Deliberately absent: a generic
`Imported`.** Where something came from is what the other kinds already say, and a
category meaning "from somewhere" carries no semantic value.

## MEASURED VS REFERENCE DATA

Resolved, and it is the reason the enumeration exists. A measured yield strength and a
handbook nominal of the same number are **not the same claim**: one is a property of
the material in hand, the other of the alloy in general.

```text
isMeasured(kind)       Measured only
isReferenceData(kind)  LibraryReference, ManufacturerData, Standard, Handbook
neither                Unspecified, Calculated, UserEntered
```

Predicates rather than a switch in every caller, and **not inferred from whether a
source string exists** — a citation is not a measurement, and an uncited measurement is
still one. Both directions tested.

## SOURCE, REFERENCE, STANDARD, REVISION, DATE

Every field optional; sparse metadata is the normal case and never a reason to refuse
data.

```text
source      "Alcoa 6061-T6 datasheet"     free text naming the source
standard    "ASTM B221"
reference   "Table 4"                     where in the source
revision    "Rev C"                       METADATA, NEVER ORDERED
date        2024-03-17                    materials::Date, ISO 8601
condition   "T6"                          ADR-028's "condition/temper"
notes       anything else
```

**Revision is never parsed, ordered or compared for precedence.** "Rev C" is not
greater than "Rev B" as far as BetterCAD is concerned; a revision string is equal only
to itself. Ordering them would need a scheme every supplier agrees on, and there is
none. Tested with `"2nd edition, 3rd printing"`, stored and returned verbatim.

**`Date` is new — the repository had no date type at all**, and no `<chrono>` use
anywhere. Its own value type, following the repository's habit of small explicit ones
(`Quantity`, `PoissonRatio`, `Hardness`), chosen for determinism: `toString` is
`std::format("{:04}-{:02}-{:02}", …)` over three integers, so it cannot pick up a
locale, a time zone or a clock. Validated on construction including leap years, so
`2026-02-30` cannot be stored and then formatted as though it were real — tested
across 1900, 2000, 2023 and 2024.

## PROPERTY EDIT PROVENANCE SEMANTICS

The policy, stated in the header and tested three ways:

```text
setMaterialMechanical / setMaterialThermal   replace VALUES; any property whose
                                            value CHANGED loses its own citation
removeMaterialProperty                      removes the value AND its citation
setMaterialProvenance                       replaces provenance ONLY
setMaterialPropertyProvenance               no value moves, ever
```

**Cleared, not reassigned.** A citation describes a number; when the number changes it
describes nothing that is there, and keeping it would make the document claim a source
it does not have — worse than a gap. It clears rather than asserting `UserEntered`,
because asserting anything would itself be a claim BetterCAD cannot support: the new
value may well have come from a newer datasheet. Cleared means "nobody said", which is
true. A user correcting a transcription re-states the source in a second, explicit
call.

**The material-level default is never cleared**, because it describes the material
rather than any one value.

**An edit that changes nothing loses nothing** — this matters more than it looks.
These setters replace a whole struct, so the ordinary edit pattern is read, modify,
write. A policy that cleared on every write would make editing one property destroy
every other property's citation. Tested directly.

## PROPERTY REMOVAL SEMANTICS

Removal takes the value and the citation together, and it falls out of the edit policy
rather than being a second mechanism: removal sets the slot to Unknown, which differs
from the old value, so the citation goes with it. **No orphan is possible from a
removal**, tested for both halves, with the general report then checked to contain no
orphan issue.

An orphan is still *reachable* deliberately — citing a property that has no value,
which a user may legitimately do before entering the number — and is reported,
non-blocking, naming the property.

## CUSTOM CLONE PROVENANCE

Provenance is part of the definition, so `cloneMaterial` carries it with the values —
every field including the fixed date — and independently: editing the clone's
provenance leaves the source's alone.

**An import now records its library entry as provenance**, which ADR-028 says the key
*is*: `SourceKind::LibraryReference`, the entry and revision in `source` and
`revision`, and the entry's standard. Every property the material later gains without
a citation of its own therefore traces to that entry by default.

**And an edited import stops claiming the library for the changed value** — the test
the brief calls most important. The per-property library citation is cleared; the
material-level one survives, because "this material came from that entry" remains
true even when one of its numbers has been corrected.

## COMPLETENESS REPORT

```text
CompletenessReport {
    CompletenessState state;            Ready | Incomplete | Invalid
    vector<MechanicalPropertyKind> presentMechanical, missingMechanical;
    vector<ThermalPropertyKind>    presentThermal,    missingThermal;
    vector<MaterialIssue>          issues;
}
MaterialIssue { IssueKind kind; optional<Mech>; optional<Therm>;
                optional<ConsumerKind>; string message; }
IssueKind: MissingProperty | InconsistentValues | OrphanProvenance | ProvenanceIncomplete
```

Semantic property identifiers, not strings: a GUI or CLI reads `kind` and the property
and can translate, and `message` is a fallback for logs. A compile-fail case proves an
issue cannot be compared to a string literal.

```text
Ready        every required property present and valid
Incomplete   a required property is missing -- not a fault, just not characterised
             that far yet (ADR-027)
Invalid      everything required is present, but the data contradicts itself
```

**Missing outranks invalid**: a caller told "incomplete" knows to supply data and
cannot act on "invalid" for a value that is not there.

**There is no single completeness flag, and four compile-fail cases enforce that.** A
report is not convertible to `bool`, has no `isComplete()`, and neither does a
`Material`; `materialCompleteness` has no overload without a consumer.

### Two reports, deliberately different shapes

```text
consumerCompleteness   only that consumer's requirements; the decision to run
generalCompleteness    every property, for inspection; raises NO MissingProperty
                       issue, and is Ready when nothing is WRONG
```

`generalCompleteness` is `Ready` on a material with a conductivity and nothing else,
while listing eleven missing mechanical and four missing thermal properties. No real
material has every property, so a state that was always `Incomplete` would carry no
information — and a caller must not be tempted to gate an analysis on it.

## CONSUMER REQUIREMENT MODEL

```text
Consumer                      Mechanical                        Thermal
MassProperties                Density                           --
FeaLinearStatic               YoungsModulus, PoissonRatio       --
FeaLinearStaticWithGravity    Density, YoungsModulus,           --
                              PoissonRatio
FeaYieldStrength              YoungsModulus, PoissonRatio,      --
                              YieldStrength
ThermalSteady                 --                                ThermalConductivity
ThermalTransient              Density                           ThermalConductivity,
                                                                SpecificHeatCapacity
ThermoMechanical              YoungsModulus, PoissonRatio       ThermalExpansion
```

Asserted **by value**, not by count, because the likely errors are specific:

- **`ThermalSteady` requires a conductivity and nothing else.** Copying the transient
  set would silently block a solvable problem.
- **`FeaLinearStatic` requires no density.** A static solve with no body force never
  touches a mass; the gravity variant is where the density enters.
- **`FeaYieldStrength` requires the yield strength specifically** — a UTS is a
  different stress with a different meaning.
- **`ThermalTransient` requires the density**, which lives in the mechanical half
  because it has one home, and is a plausible place to drop it.
- **No consumer requires a derived modulus.** Both are exactly determined by E and ν,
  so requiring the independent pair is the honest statement. Asserted over every
  consumer.
- **No consumer requires provenance.** Asserted.

### The cross-check that keeps one source of truth

Two tables describing the same thing will drift, so the table is checked against the
functions consumers actually call. For each consumer, **each required property is
removed in turn and both the report and the runtime function must object**:

```text
MassProperties              vs requireDensity
FeaLinearStatic             vs requireLinearElasticConstants
FeaLinearStaticWithGravity  vs requireLinearElasticConstants + requireDensity
ThermalSteady               vs requireThermalConductivity
ThermalTransient            vs requireTransientConductionProperties
ThermoMechanical            vs requireLinearElasticConstants + requireThermalExpansion
FeaYieldStrength            NO runtime function yet -- recorded, not faked
```

`FeaYieldStrength` cannot be cross-checked because no yield consumer exists; that is
recorded rather than papered over with a stub.

## MISSING PROPERTY REPORTING

```text
ThermalTransient            density known, cp known, k UNKNOWN
Result:                     Incomplete
Missing:                    ThermalConductivity          (and nothing else)

ThermalTransient            density UNKNOWN, cp UNKNOWN, k known
Result:                     Incomplete
Missing:                    Density, then SpecificHeatCapacity
                            -- mechanical before thermal, each in enumeration order

FeaLinearElastic            E known, nu UNKNOWN
Result:                     Incomplete
Missing:                    PoissonRatio

FeaYieldStrength            E known, nu UNKNOWN, yield UNKNOWN
Missing:                    PoissonRatio, then YieldStrength
```

No defaults used anywhere. Ordering is defined and repeats exactly over eight passes.

## NO FABRICATION AUDIT

Every fabrication shape the brief names, searched across `src/` and `include/`:

```text
value_or                     12 hits, all classified
  in the material path        1: setDefinition(...).value_or(false) -- a "did
                              anything change" flag, not an engineering value
  elsewhere                   5 more of the same change-flag shape;
                              2 expression().value_or(std::string{}) for a message;
                              3 Direction3D::from*(...).value_or(<the input>);
                              1 literalDiameter(d).value_or(d.diameter)
valueOr on MaterialProperty  DOES NOT EXIST -- one textual hit, in the comment
                             saying it deliberately does not
getOrDefault                 0
defaultMaterial              0
"generic steel"              0
"typical value"              0
"default density"            0
"default nu"                 0
"default modulus"            1 -- my own header comment saying there is none
```

**Not one of the twelve is an engineering property, and none invents a constant.** The
three `Direction3D` ones fall back to the direction they were given, which is the
input rather than a fabricated value.

Tested as well as audited: a material with nothing known reports every requirement of
every consumer as missing, all five `require*` entry points fail, and both derived
moduli are `Unknown` with `value()` empty — not zero.

### Derived is not fabricated

```text
E known + nu known -> G = E/(2(1+nu))     DERIVATION, authorised by ADR-027,
                                           computed on request, never stored
E missing -> assume 200 GPa                FABRICATION, and no code path can do it
```

A derived property reports `SourceKind::Calculated` and **names its inputs with their
own provenance**, so a number is never traceable to an equation and nothing else.
Attaching a citation to a derived property is refused with a diagnostic.

## NO SEMANTIC SUBSTITUTION

`ElasticModulus` and `Stress` **are the same C++ type** — both `Quantity<pressure>`
(P15-UNITS-001: "stress and elastic moduli are pressures dimensionally"). The first
implementation of `Completeness.cpp` did not compile because of it, which is the
concrete demonstration that requirements cannot be expressed in types.

So they are expressed in property kinds, and tested:

```text
four pressures present (yield, UTS, compressive, shear) + nu, no E
-> FeaLinearStatic Incomplete, missing {YoungsModulus}
-> requireLinearElasticConstants fails
-> the derived shear modulus is unavailable too

UTS present, yield absent      -> FeaYieldStrength missing {YieldStrength}
resistivity present, k absent  -> ThermalSteady missing {ThermalConductivity}
```

**Mutation-tested.** Substituting a resistivity for a conductivity and a UTS for a
yield fails two test cases and eight assertions.

## CONSISTENCY VALIDATION

`inconsistencies()` **wraps** `mechanicalInconsistencies()` rather than restating its
rules — P15-MECH-001 owns them and their tolerances, and a second copy would drift.
The rule is a yield strength above the ultimate tensile strength, with a documented
relative tolerance.

Reported, **never corrected**, and the values are checked unchanged afterwards. No
property is attached to the issue, deliberately: an inconsistency is a statement about
a *relationship*, so naming one of the two would be arbitrary. The message names both.

A consumer with every required value present but contradictory data is **Invalid**,
not Incomplete — there is nothing to supply, something to fix.

No thermal equivalent exists to wrap, and the implementation says why rather than
leaving an unexplained empty branch: no two thermal properties in this model constrain
each other.

### Derived G/K validation

**The classic inconsistency cannot arise.** ADR-027 gave `MechanicalProperties` no
slot for a shear or bulk modulus, so "a supplied G disagrees with E and ν" was
*designed out* rather than validated. What remains tested is that the derived value
tracks its inputs exactly and is never `Known`.

## PROVENANCE PERSISTENCE

**BLOCKED on P15-PERSIST-001**, and recorded as the brief instructs rather than
claimed.

ADR-028 requires provenance to be persisted. **No material of any kind can be saved**:
`src/io/json/DocumentJson.cpp` rejects an object type it does not recognise and
"material" is not recognised. So there is no file format to round-trip provenance
through and no round-trip test can exist.

Verified instead:

```text
provenance is CANONICAL state on MaterialDefinition, so P15-PERSIST-001 cannot
  write a material without deciding what to do with it
a material with values and no provenance is NOT content-equal to the same material
  with provenance -- so a writer that dropped it would produce a document that does
  not compare equal to the one it saved
it survives a value-semantics round trip exactly, across documents and outliving
  the source document (P15-CUSTOM-001's clone tests, extended here)
nothing in it depends on a pointer, an address, a container position or a clock
the save gap stays LOUD with a full provenance record present -- kind, source,
  standard, reference, revision, condition and a fixed date -- failing with a
  diagnostic naming the type and leaving nothing half-written
```

The dangerous failure is not the absence but a future writer that knows about
materials and omits provenance: the values would still be there, so nothing would look
wrong.

## DETERMINISTIC SERIALIZATION

No serialization exists to test, so determinism is established where it lives:

```text
provenance ordering    std::map over the property-kind enumerations
report ordering        kind, then mechanical-before-thermal, then enumeration order
dates                  ISO 8601 from three integers -- no locale, zone or clock
issue messages         std::format over stable inputs
```

Tested: provenance inserted in **reverse** enumeration order enumerates in enumeration
order, so the order comes from the key rather than from insertion; and reports repeat
exactly over eight passes. Nothing is built from an unordered container, a hash or a
pointer. Confirmed across all three presets by the qualification.

**No test reads a clock.** Every date is a fixed literal, constructed through a helper
named `fixedDate`, which makes an accidental `now()` conspicuous.

## MATERIAL IDENTITY, ASSIGNMENT AND ANALYSIS INTEGRATION

```text
provenance edits      MaterialId unchanged, assignment still Resolved, count 1
provenance edits      no number moves -- checked through a REAL mass calculation:
                      mass, volume, inertia tensor and the derived shear modulus
                      all compared exactly after four citations are attached
assignment failures   stay DISTINCT from completeness failures. "No material
                      assigned" and "the assigned one is gone" are the assignment's
                      diagnostics and make the call FAIL; "assigned but not
                      characterised" is a REPORT that succeeds and says Incomplete
completeness agrees   with what the mass calculation actually does: Incomplete and
                      the mass fails; supply the density and both change together
```

That third row matters: "choose a material" and "characterise it further" need
different things from the user, so they must not arrive as the same answer.

## ADVERSARIAL REVIEW

**PASS — 30 attacks, 5 findings.** One test defect found by mutation testing, one
implementation defect, three recorded conflicts. **No production defect survived.**
Full text: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
F1  the fixture was too sparse to expose a substitution        TEST DEFECT, fixed
F2  hasMaterialProperty answered the storage question for a
    derived kind -- true, and misleading                       DEFECT, fixed
F3  provenance persistence cannot be tested; ADR-028 needs it  CONFLICT, recorded
F4  ElasticModulus and Stress are the same type, which broke
    the first implementation -- and is the point               CONFLICT, recorded
F5  a clone of an import is still indistinguishable from a
    direct import                                              LIMITATION, carried
```

**F1 is the one worth carrying forward.** A test for "X is not substituted for Y" is
only as strong as the fixture's Y. Mine had eight properties, not fifteen, so when the
cross-check removed a conductivity there was no resistivity present for a substitution
to reach for — and the mutation stayed invisible in that test. A sparse fixture makes
the substitution unreachable and the test vacuous, and it passes either way.

## FULL REGRESSION

**The first attempt FAILED, and that is recorded rather than tidied away.**
`debug-shared-ext build` exited 1: two member functions declared in `Provenance.hpp`
and defined out of line on unexported structs were hidden under
`-fvisibility=hidden` and unresolvable across the DLL boundary. Debug and Release
could not have caught it -- they are static -- and this is the second time in P15 that
`debug-shared-ext` has caught a DLL-boundary defect the other two presets passed. Full
account, with the log and the root cause:
[qualification-void/](qualification-void/README.md).

Both `empty()` methods became inline in the header, restoring a convention every other
data struct in `core/materials/` already followed. The first qualification was
therefore VOID -- source changed after the freeze -- and the whole thing was re-run
from clean across all three presets, not merely the one that failed.

The second attempt:

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2639/2639      0
release-ext            0        0      0          0        2639/2639      0
debug-shared-ext       0        0      0          0        2639/2639      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   13195 = 2639 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   13195 = 2639 x 5, 0 failures
qualification finished 14:21:29, 0 stage(s) failed
```

2639 = the 2589 of P15-CUSTOM-001 plus the 50 added here, and all three presets
discover the same 2639. Both repeat stages show exactly 13195 passing executions, which
is 2639 x 5 with nothing skipped.

**34307 test executions in total, 0 failures**: three full suites plus two five-fold
repeats, 11:00:27 to 14:21:29 (3h 21m).

This milestone's central claims are ORDERINGS and ABSENCES, and an ordering claim is
exactly what can differ between Debug and Release. So the tests that carry them were
confirmed present and passing in **all three** presets rather than only where they were
developed:

```text
all 15 compile_fail.prov cases                                passed x3
the requirements table agrees with the require*() functions    passed x3
no property is substituted for another                        passed x3
a missing property is reported, never filled                  passed x3
provenance ordering comes from the key, not from insertion     passed x3
```

`verify-harness.cmd` was run first and required a non-zero exit from a preset that does
not exist: 3 stages failed, exit 3.

**No replace fault anywhere** -- zero occurrences of "Permission denied" or "cannot
replace" across every log of either attempt.

A pre-qualification smoke run of the whole suite on `debug-ext` reported 2638/2639. The
one failure was `cli.new.unicode-path`, and the cause was the invocation rather than the
tree: that test needs code page 65001, which `qualify.cmd` sets and an ad-hoc `ctest`
does not. Verified by rerunning it under `chcp 65001`, where it passes, and by all three
qualified presets passing it.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first build
and after the last test run are identical:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include 01c377dde25272ebe940af4efdefe2dffff7327a
src 6fd1456681ec1fbed582e781712b273c67870a71
tests e79ca630e751371f12cd0d6b8771622771941d79
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Only `docs/` changed after the freeze, and `docs/` is outside the fingerprint and cannot
affect the executable or the tests.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set.

## KNOWN LIMITATIONS

1. **Provenance is not persisted.** Blocked on P15-PERSIST-001; see PROVENANCE
   PERSISTENCE. The gap is loud, not silent.
2. **`FeaYieldStrength` has no runtime counterpart to cross-check against**, because
   no yield consumer exists yet. Its requirement set is asserted by value but is the
   one consumer whose table is not verified against a function.
3. **A clone of an imported material is indistinguishable from a direct import.**
   Carried from P15-CUSTOM-001. The machinery to fix it is now here — a `clonedFrom`
   field beside `origin` — but the decision is P15-PROV's successor's.
4. **No CFD consumer**, and a future one will need a dynamic-viscosity **property**,
   which no material carries. The unit exists; the slot does not.
5. **Provenance and value are separable.** The consequence is managed — removal takes
   both, an edit clears a stale citation, an orphan is reported — but a caller
   constructing a `MaterialDefinition` by hand can still create an orphan, and will be
   told about it rather than prevented.
6. **`revision` is not ordered**, so BetterCAD cannot tell a user that their citation
   is older than the library's. That is deliberate: there is no revision scheme every
   supplier agrees on.
7. **Traceability completeness is reported but not required.** The foundation for a
   certification-grade workflow is present — issue kinds, `isBlocking()`, the
   measured/reference distinction — but no consumer demands provenance, and adding one
   that does needs no change to the numerical requirement API.

## RESULT

```text
TASK:            P15-PROV-001
IMPLEMENTATION:  per-property provenance with a material-level default (ADR-028);
                 a validated Date value type, the repository's first; eight source
                 kinds with a measured/reference predicate; seven consumer
                 requirement sets; a structured completeness report with defined
                 ordering; consistency and traceability reporting; the document
                 entry points. 430 lines changed, 2502 added.
TESTS:           50 added (34 provenance, 1 persistence, 15 compile-fail);
                 2589 -> 2639
VALIDATION:      the requirements table cross-checked against the six require*()
                 functions consumers actually call, by removing each required
                 property in turn; a full no-fabrication audit of the tree; two
                 automatic-FAIL gates mutation-tested
RESULT:          PASS (second attempt; the first is recorded as void)
EVIDENCE:        this directory; qualification/ for all 20 stage logs and the tree
                 fingerprints; qualification-void/ for the failed first attempt
TODO:            updated -- 14 boxes ticked
```

**Two things this milestone got from the architecture rather than inventing.** ADR-028
had already chosen per-property provenance with a material-level default, so the model
was implemented rather than designed. And six `require*()` functions already encoded
what each consumer needs, so `requiredProperties()` is cross-checked against them
instead of becoming a second source of truth.

**Three things the tree decided against the brief.** `ElasticModulus` and `Stress` are
the same C++ type, so the first implementation did not compile -- which is the concrete
proof that a requirement cannot be expressed in types and must be expressed in
semantic property kinds. Provenance persistence cannot be tested because no material
can be saved at all, so the gate is BLOCKED on P15-PERSIST-001 and the save gap is kept
loud instead. And there is no CFD consumer, because ADR-028 names P19 without defining
its requirements.

**The most useful finding was in my own tests.** Mutation-testing a property
substitution was caught by one test case, not the cross-check -- because the fixture
carried eight properties rather than fifteen, so there was no substitute present for the
mutation to reach for. A test for "X is not substituted for Y" is only as strong as the
fixture's Y.

## REVISION

Second revision. The first qualification attempt failed on `debug-shared-ext build`;
see FULL REGRESSION and [qualification-void/](qualification-void/README.md). Written
against the tree qualified above; no source or test file changed after the second
freeze.
