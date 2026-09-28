# P15-PROV-001 — Adversarial review

**Aim: disprove the milestone.** The 24 attacks the brief names, plus 4 of my own.

**Result: 5 findings.** One was a defect in the tests that hid a real weakness —
found by mutation testing, not by a failing test. One is an implementation defect I
found and fixed. Three are conflicts between the brief and this tree, recorded with
what was done instead.

**No production defect survived.** Two of the milestone's three automatic-FAIL gates
were mutation-tested rather than only asserted.

---

## Findings

### F1 — The fixture was too sparse to expose a substitution, and mutation testing found it (TEST DEFECT, fixed)

The whole point of `requiredProperties()` is that a property is satisfied by itself
and by nothing else. To check that the tests actually test it, I mutated
`isAvailable()` to substitute an electrical resistivity for a missing thermal
conductivity and an ultimate tensile strength for a missing yield strength — the
"helpful fallback" a well-meaning implementation would add.

**It was caught by one test case and four assertions.** The cross-check test — the
one that removes each required property in turn and asserts both the table and the
runtime function object — did not notice.

The reason was the fixture. `characterised()` carried eight properties, not fifteen:
no electrical resistivity, no compressive or shear strength, no melting temperature.
So when the cross-check removed the thermal conductivity, there was no resistivity
present for a substitution to reach for, and the mutation stayed invisible there.

Fixed by making the fixture genuinely exhaustive — every stored property Known — so
that removing any one required property leaves every plausible substitute present.
The same mutation is now caught by **two test cases and eight assertions**, the
cross-check among them.

**The general lesson, recorded because it will recur:** a test for "X is not
substituted for Y" is only as strong as the fixture's Y. A sparse fixture makes the
substitution unreachable and the test vacuous, and it passes either way.

### F2 — `hasMaterialProperty` for a derived kind answered the wrong question (DEFECT, fixed)

Carried over from P15-CUSTOM-001 and re-examined here: `isAvailable(properties,
ShearModulus)` first returned whether a shear modulus was **stored**. It never is
(ADR-027), so it returned `false` for a material with a perfectly good E and ν —
from which BetterCAD can report G exactly.

True about storage, misleading about the question. A completeness report built on it
would have called a material incomplete for something it can supply. Changed to
report whether the **inputs** are present, and the header says so.

### F3 — Provenance persistence cannot be tested, and ADR-028 requires it (CONFLICT, recorded)

ADR-028 is explicit: "Provenance is canonical and persisted", and it rejects the
alternative — "a saved document would be unable to say where its numbers came from,
which is most of what makes it an engineering record".

**No material of any kind can be saved.** `src/io/json/DocumentJson.cpp` rejects an
object type it does not recognise, and "material" is not recognised; that is
P15-PERSIST-001's work. So the brief's item 37 — save a material with mixed
provenance, load it, compare every field — describes a test that cannot exist, because
there is no file format to round-trip through.

The brief anticipates this and says to "record that conflict and handle according to
qualified contract. Use repository truth." So:

```text
verified now      provenance is CANONICAL state on MaterialDefinition, so
                  P15-PERSIST-001 cannot write a material without also deciding
                  what to do with it
                  it survives a value-semantics round trip exactly, including
                  across documents and outliving the source document
                  nothing in it depends on a pointer, an address, a container
                  position or a clock
                  the save gap stays LOUD, with provenance present
BLOCKED           a file round trip, on P15-PERSIST-001
```

The dangerous failure here is not the absence but a future writer that knows about
materials and omits provenance: the values would still be there, so nothing would
look wrong. A new io test therefore saves a material with a full provenance record —
kind, source, standard, reference, revision, condition and a fixed date — and requires
the save to fail loudly with nothing half-written, so whoever implements
P15-PERSIST-001 has to deal with it rather than discovering it.

### F4 — `ElasticModulus` and `Stress` are the same type, which broke the first implementation (CONFLICT WITH THE BRIEF'S ASSUMPTION, and it is the point)

The first draft of `Completeness.cpp` had one `storedAndValid` overload for
`MaterialProperty<ElasticModulus>` and another for `MaterialProperty<Stress>`. **It
does not compile:** P15-UNITS-001 made them the same type, because "stress and
elastic moduli are pressures dimensionally".

That is not an inconvenience, it is the concrete demonstration of the brief's item 55.
The type system **cannot** tell a Young's modulus from a yield strength from a shear
strength, so nothing but the semantic property kind can, and a requirement expressed
in types would be satisfied by any pressure at all. The one overload is now named
`storedAndValidPressure` and says this in a comment, and a test asserts
`std::is_same_v<ElasticModulus, Stress>` before proving that four present pressures
still leave a modulus requirement unmet.

### F5 — A clone of an import is still indistinguishable from a direct import (LIMITATION, carried from P15-CUSTOM-001, now partly addressed)

P15-CUSTOM-001 recorded that `cloneMaterial` carries `origin` over, so a clone of an
imported material and a direct import are byte-identical in canonical state.

P15-PROV-001 does not fix this and does not pretend to. What it adds is the machinery
that makes fixing it a field rather than a redesign: provenance is now a first-class
per-property record with a `SourceKind`, so `clonedFrom` can be added beside `origin`
without migrating anything. Recorded again rather than quietly dropped.

---

## The 24 attacks

### Fabrication

**1. Can a missing thermal conductivity silently receive a default?** No, and this
was **mutation-tested**: substituting a resistivity for it fails two test cases. A
material with no conductivity reports `Incomplete` with `ThermalConductivity` in
`missingThermal`, and `requireThermalConductivity` fails.

**2. Can a missing Young's modulus silently receive 200 GPa?**
**3. Can a missing Poisson ratio silently receive 0.3?** No. A full audit of the tree
for fabrication shapes found:

```text
value_or in src/ and include/       12 hits, every one classified below
value_or in the material path        1 hit: setDefinition(...).value_or(false),
                                     a "did anything change" flag, not a value
valueOr on MaterialProperty          does not exist -- 1 textual hit, in the
                                     comment saying it deliberately does not
getOrDefault, defaultMaterial,
"generic steel", "typical value",
"default density", "default nu"      0 hits each
"default modulus"                    1 hit: my own header comment saying there is none
```

The other eleven `value_or` hits are: five `setDefinition(...).value_or(false)` change
flags (components, mates, sheets, views, materials); two
`expression().value_or(std::string{})` for a diagnostic message; three
`Direction3D::from*(...).value_or(<the input direction>)` where a re-normalisation
falls back to the direction it was given; and one
`literalDiameter(d).value_or(d.diameter)` falling back to the definition's own value.
**None of them is an engineering property, and none invents a constant.**

A test walks every consumer against a material with nothing known, requires every
requirement to appear as missing, requires all five `require*` entry points to fail,
and requires both derived moduli to be `Unknown` with `value()` empty — not zero.

**4. Is a derived value a fabricated one?** No, and the distinction is recorded:
`E known` + `ν known` → `G = E/(2(1+ν))` is a mathematical derivation authorised by
ADR-027, computed on request and never stored. `E missing → assume 200 GPa` would be
fabrication, and there is no code path that could do it. A derived property reports
`SourceKind::Calculated` and names its inputs, so a number is never traceable to an
equation and nothing else.

### Substitution

**5. Can an ultimate tensile strength substitute for a yield strength?** No —
mutation-tested (F1). `FeaYieldStrength` requires `YieldStrength` specifically; a
material with a 310 MPa UTS and no yield reports `missingMechanical ==
{YieldStrength}`.

**6. Can a shear modulus substitute for a Young's modulus because the dimensions
match?**
**7. Can a yield strength substitute for a Young's modulus for the same reason?** No.
This is F4's subject: the two are literally the same C++ type, so the requirement
cannot be expressed in types and is expressed in property kinds. The test supplies
four pressure-dimension properties — yield, UTS, compressive, shear — and a Poisson
ratio, and `FeaLinearStatic` still reports `{YoungsModulus}` missing.

**8. Can an electrical resistivity satisfy a thermal conductivity?** No — both
describe transport and neither is the other. Tested, and mutation-tested.

### Unknown versus zero versus unsourced

**9. Can an unknown property be represented as zero?** No. `MaterialProperty::value()`
returns `std::optional` and there is no `valueOr`, so an Unknown property cannot
become a number away from the point of use. `isAvailable()` additionally requires the
value to be **in range**, so a report cannot call a material Ready on a value
validation would refuse.

**10. Can a known value with no source be mistaken for a missing value?** No, and
this separation is most of the milestone. A material with a density and no citation is
`Ready` for `MassProperties`; `materialPropertyProvenance` returns an empty record and
`SourceKind::Unspecified`. Tested directly.

**11. Can a missing citation block FEA unintentionally?** No. `requiredProperties()`
returns property kinds only — a test asserts no consumer requires provenance — and
`isBlocking()` returns `false` for both provenance issue kinds. A material with a
self-contradictory citation and an orphan citation is still `Ready` for every consumer
whose values are present. Tested.

### Identity and metadata

**12. Can changing provenance change the MaterialId?** No. `setMaterialProvenance`
and both `setMaterialPropertyProvenance` overloads go through `setMaterialDefinition`,
which cannot reach the ID. Tested through three provenance edits, with the assignment
checked `Resolved` afterwards.

**13. Can provenance change a mass, or a derived G or K?** No, and this is tested
through a **real mass calculation** rather than by comparing a definition to itself:
a box with a custom material, mass and inertia recorded, then four provenance records
attached, then mass, volume, inertia tensor and derived shear modulus all compared
exactly. `setMaterialProvenance` reads no value at all, so it is true by construction
as well.

**14. Can editing a cloned property leave stale library provenance attached?** No, and
this is the attack the brief calls most important. **Mutation-tested:** disabling the
clearing fails three test cases. The policy is:

```text
value changes        that property's own citation is CLEARED
value unchanged      its citation survives, even when a sibling changed
material default     never cleared -- it describes the material, not a value
```

Cleared rather than set to `UserEntered`, because asserting a kind would itself be a
claim BetterCAD cannot support: the new number may well have come from a newer
datasheet. Cleared means "nobody said", which is true. The user re-states the source
explicitly in a second call.

**15. Can property removal leave orphan provenance?** No, and it falls out of the same
mechanism: removal sets the slot to Unknown, which differs from the old value, so the
citation goes with it. Tested for both halves, and the general report is then checked
to contain no orphan issue. When an orphan *is* created deliberately — by citing a
property that has no value — it is reported, non-blocking, naming the property.

### Persistence and determinism

**16. Can source metadata be lost on save/load?**
**17. Can save/load change a SourceKind?** Untestable today: nothing saves. See F3.
What is verified is that provenance is canonical state, that a material with values
and no provenance is **not** content-equal to the same material with provenance — so
a writer that dropped it would produce a document that does not compare equal to what
it saved — and that the save gap stays loud.

**18. Can dates serialize locale-dependently?** No. `Date` is BetterCAD's own value
type, and `toString` is `std::format("{:04}-{:02}-{:02}", …)` over three integers:
no locale facet, no time zone, no `<chrono>` formatting. Tested against fixed
expected strings including `"0001-01-01"`.

**19. Can the current time make anything nondeterministic?** No. Nothing in the
implementation reads a clock — there is no `now()`, `today()` or `system_clock`
anywhere in the material code — and every date in every test is a fixed literal. A
helper named `fixedDate` is the only way the tests construct one, which makes an
accidental `now()` conspicuous.

**20. Can issue ordering differ across presets?** No. Both provenance maps are
`std::map` over the property-kind enumerations; the property lists are built by
walking those enumerations; issues are sorted by kind, then mechanical-before-thermal,
then enumeration order. Nothing is built from an unordered container, a hash or a
pointer. Tested: provenance inserted in reverse order enumerates in enumeration order,
and reports repeat exactly over eight passes. Confirmed across all three presets by
the qualification.

### Requirements that overreach or fall short

**21. Can a general missing-property list wrongly block a consumer that does not need
those properties?** No, and the two reports are deliberately different shapes.
`generalCompleteness` lists every absence and raises **no** `MissingProperty` issue,
and its state is `Ready` when nothing is *wrong* — because no real material has every
property and a state that was always `Incomplete` would carry no information.
`consumerCompleteness` reports only that consumer's requirements. Tested on a material
with a conductivity and nothing else: the general report lists eleven missing
mechanical properties and four missing thermal ones, and `ThermalSteady` is `Ready`.

**22. Can steady thermal accidentally require a specific heat or a density?** No —
asserted exactly: `requiredProperties(ThermalSteady).mechanical` is empty and
`.thermal == {ThermalConductivity}`. This is the one most likely to be got wrong by
copying the transient set, so it is asserted by value rather than by count.

**23. Can transient thermal forget the density?** No — asserted exactly:
`.mechanical == {Density}` and `.thermal == {ThermalConductivity,
SpecificHeatCapacity}`. The density is in the mechanical half because it has one home
(P15-THERM-001), which is a place this could plausibly have been dropped.

**24. Can an invalid supplied G be silently corrected?**
**25. Can a derived G be serialized as a supplied G?** Neither can arise.
`MechanicalProperties` has **no slot** for a shear or bulk modulus (ADR-027: it would
be a second source of truth that could disagree with the first), so a supplied G
cannot exist to be corrected or persisted. Two compile-fail cases from P15-CUSTOM-001
pin the absence; a test here confirms the derived value tracks its inputs and is never
`Known`.

**26. Can a future CFD consumer require a second completeness framework?** No. A
requirement is a list of property kinds, so adding a consumer is one `case`. CFD is
deliberately **absent** — ADR-028 names P19 as a future consumer and defines no
requirements, so inventing them would be guessing at physics — and a compile-fail case
proves `ConsumerKind::Cfd` does not exist. Worth recording: a CFD consumer will also
need a dynamic-viscosity **property**, which no material carries yet; the unit exists,
the property slot does not.

### My own four

**27. Does the requirements table agree with the functions consumers actually call?**
Yes, and this is checked rather than assumed, because two tables that describe the
same thing will drift. For each of the six consumers that has a runtime entry point
today, each required property is removed in turn and **both** the report and the
function must object. `FeaYieldStrength` has no runtime function yet — no yield
consumer exists — and that is recorded rather than faked with a stub.

**28. Can provenance be attached to something that has no source?** A derived property
is refused explicitly: `setMaterialPropertyProvenance(…, ShearModulus, …)` fails with
a diagnostic saying it reports its inputs' provenance instead. Attaching a citation to
an absent value is *allowed* — a user may cite before entering — and reported as an
orphan.

**29. Does an empty provenance record differ from an absent one?** It must not, or
`hasOwnProvenance` and `effectiveProvenance` would disagree. Storing an empty record
erases the map entry instead, so the two are the same state by construction. Tested
via `empty()` on several shapes.

**30. Does a read-modify-write edit of one property destroy the others' citations?**
No — this was the risk the clearing policy created, and it is tested directly: editing
the yield strength leaves the density's and the modulus's own citations intact. A
policy that cleared on every write would have made the ordinary edit pattern
destructive.
