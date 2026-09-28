# P15-PERSIST-001 — Adversarial review

**Aim: disprove the milestone.** The 26 attacks the brief names, plus 4 of my own.

**Result: 5 findings.** Three are defects I introduced and fixed — one caught by the
compiler, one only by reading the code after a clean build, one only by the shared-build
preset. Two are decisions recorded rather than crossed.

Three of the five automatic-FAIL gates were **mutation-tested**, because 36 tests
passing on the first run is not evidence on its own.

---

## Findings

### F1 — The assignment was written inside the configurations guard (DEFECT, fixed, and the build did not catch it)

My first patch placed the `material_assignment` writer **inside**
`if (!document.configurations().empty())`. The result compiles, links and passes any
test whose document happens to have a configuration.

A document with a material assignment and no configurations would have saved with the
assignment **silently dropped**.

Found by reading the patched region rather than by a failing test — and worth recording
because the failure mode is exactly the one persistence work produces: a save that
succeeds and loses data. No test I had written at that point would have caught it,
because none of my fixtures had configurations.

The tests written afterwards do catch it: `..._WritesNoAssignmentSectionWhenThereIsNone`
and `..._RoundTripsAnAssignmentByIdentity` both use documents with no configurations.

**The lesson is the sequencing.** A patch applied by script into a large function needs
its region read back, because "it compiles and the suite is green" says nothing about a
branch no fixture reaches.

### F2 — Three references bound into temporary documents (DEFECT, fixed, caught by the compiler)

`const MaterialDefinition& after = definitionOf(roundTrip(document), id);` binds a
reference into a `Document` temporary that dies at the end of the statement. GCC's
`-Wdangling-reference` caught two; grepping for the pattern found a third that it had
not flagged.

Fixed by naming the loaded document so it outlives the reference. Worth recording
because the two GCC found and the one it did not are the same bug, so the warning is a
help and not a guarantee.

### F3 — The reader dispatched on a dll-imported constexpr static (DEFECT, fixed before the freeze)

My read dispatch compared `*type == features::Material::kTypeName`. That binds a
reference to a dll-imported `constexpr static`, which **does not link in a shared
build**:

```text
DocumentJson.cpp:282: undefined reference to
    __imp__ZN9bettercad8features8Material9kTypeNameE
```

**The repository had already solved this and written down why.** Six lines above my
change, in the same function:

> *"The literal, not Component::kTypeName: binding a reference to a dll-imported
> constexpr static does not link in a shared build (found by the debug-shared preset).
> ComponentJson.cpp carries a static_assert that the two agree, so they cannot drift."*

I added the branch without reading the comment governing the branch beside it. Fixed by
following the established pair exactly: dispatch on the literal `"material"`, and put
`static_assert(Material::kTypeName == "material")` in `MaterialJson.cpp`, as
`ComponentJson.cpp` does for its own.

**This is the third time in P15 that only the shared preset would have caught a
DLL-boundary defect** — P15-MAT-001 on this same `constexpr static` pattern,
P15-PROV-001 on an out-of-line member of an unexported type, and now this. The
difference is when it was found: P15-PROV-001 lost a two-hour qualification to it,
whereas here the pre-freeze shared build caught it in twenty minutes and before the
clock started.

Worth noting that my pre-freeze check was **not sufficient on its own**: I grepped for
out-of-line members on unexported types, which is the P15-PROV variant, and this is a
different one. The check that actually worked was building the shared preset. That is the
one to keep.

### F4 — The diagnostic strings are not usable as file keys (DECISION, recorded)

`toString(MechanicalPropertyKind)` returns `"Young's modulus"` and
`"Poisson's ratio"` — display text, with an apostrophe, intended for a diagnostic and
liable to be reworded or translated.

Reusing those as persisted keys would mean **rewording a diagnostic silently breaks
every saved document**. So `MaterialJson.cpp` carries its own explicit key table
(`youngs_modulus`, `poisson_ratio`, …), separate from the diagnostics, and says why.
The same for the hardness scales, which happen to match `toString` today and are listed
separately anyway.

### F5 — Five tests asserting "materials cannot be saved" had to be replaced, not deleted (DECISION, recorded)

P15-MAT-001 through P15-PROV-001 each added a test requiring the save to fail **loudly**
with a diagnostic naming the type, so that the persistence gap could not become silent
data loss. Those five tests guarded exactly the gap this milestone closes, so they
cannot pass any more.

They were **replaced by the round-trip tests that now prove what they were protecting**,
not deleted. The intent each one carried is stated in the new suite:

```text
a material saves at all                 -> RoundTripsTheMaterialIdExactly
mechanical properties are not dropped   -> RoundTripsEveryFieldOfARichMaterial
thermal properties are not dropped      -> the same, plus KeepsAbsoluteTemperatures...
a cloned custom material is not dropped -> KeepsACustomMaterialLocalAndNever...
provenance is not dropped               -> RoundTripsProvenanceIncludingTheDate
```

Recorded because deleting a guard is how a guarantee quietly disappears, and the diff
shows five deletions that need this account beside them.

---

## The 26 attacks

### Identity and the allocator

**1. Can a MaterialId change after load?** No. Objects are restored through
`Document::restoreObject`, which preserves the ID — the mechanism every object kind
already used. Tested with three materials so the ID under test is not merely the first,
and the whole `materialIds()` list is compared.

**2. Can duplicate IDs overwrite one another?** No. `insertObject` refuses an ID already
in use with `AlreadyExists`, before touching any state. Tested by hand-editing a file to
contain the same object id twice: the load fails with "already in use". No last-wins, no
merge.

**3. Can the allocator reuse a loaded ID?** No. `last_allocated_id` is persisted, the
loader refuses a value below an ID in use, and `reserveIdsThrough` restores the counter.
Tested on a file whose IDs are deliberately **non-contiguous** — a material was deleted
before saving — and the next material created after load collides with none of the
three, including the deleted one.

### Names versus identity

**4. Can duplicate-name materials swap assignments?** They cannot share a *name* —
object names are unique — and two sharing a *designation* are tested: the assignment
names one of them by ID through save and load, and each keeps its own values.

**5. Can an unresolved A rebind to a same-designation B after load?** No — **mutation-
tested**. A loader that fell back to a same-designation material fails 3 test cases. The
fixture is the persistence form of the no-rebinding invariant: A and B share a
designation, P → A, delete A, then **two** round trips, because a loader that dropped an
unresolved assignment would show it on the second save rather than the first.

**6. Can an assignment be saved by name only?** No. The file holds
`"material_assignment": {"material": <id>}` and a test asserts the ID is present in the
text. Writing a name would fail the duplicate-designation test, since a name cannot
distinguish the two.

### Unknown, zero, and the property states

**7. Can Unknown become zero?** No — **mutation-tested**, and this is the gate that
matters most. Writing a property as `0` when Unknown fails **14 test cases**. An unknown
property is **absent** from the file, which is the one representation that cannot be
misread: there is no number there to mistake for a measurement. Tested over both
property enumerations and by asserting the keys are absent from the text.

**8. Can derived G or K become supplied after load?** They cannot be represented:
`MechanicalProperties` has no slot for either (ADR-027), so there is no key for them and
a file cannot claim one. Tested: after a round trip the derived shear modulus is still
`isDerived()` and not `isKnown()`, and the file never names `shear_modulus` or
`bulk_modulus`.

**9. Can the mechanical and thermal density diverge?** No, because there is only one.
Density is written in the mechanical section and thermal consumers read that same value
(P15-THERM-001). A test counts the `"density"` keys in the whole file and requires
**exactly one**, then checks `requireDensity` and
`requireTransientConductionProperties` return the identical SI value after load.

**10. Can hardness lose its scale?** No. Value and scale are written together, and all
four scales are round-tripped individually so the mapping is not accidentally constant.
A hardness with no scale in the file is rejected.

**11. Can an absolute temperature become a ΔT?** No. Melting temperatures and reference
temperatures are written in kelvin as absolute values. Tested with 1811 K and 293.15 K,
checking the exact SI value rather than a tolerance, because an interval reading would
not preserve it.

### Provenance

**12. Can provenance move to the wrong property?** No. Each record carries an explicit
`"property"` key naming what it describes, so the file is **not positional** and no
reordering can reattach a citation. Tested, including asserting
`"property": "density"` appears in the text. A second record for one property is
rejected as ambiguous rather than resolved by a first- or last-wins rule.

**13. Can a known value lose its provenance?** The whole `MaterialProvenance` is compared
as one value after the round trip, and the fields most easily lost — revision,
condition, notes, date — are then checked individually. All eight source kinds
round-trip individually.

**14. Can a known value with unknown provenance be changed by the round trip?** No. The
value stays Known and the provenance stays empty; the file has no provenance section at
all, because nothing was stated.

### Custom materials and the library

**15. Can a custom clone become a live library reference?** No. The origin key is
**provenance, not a reference** (ADR-025), and nothing consults the library at load
time. Tested by giving an imported material and its clone different densities — values
the library does not have at all — and checking each survives with its own.

**16. Can future library changes alter loaded snapshot values?** No, and the strongest
available proof is that a document whose origin names an entry **this build does not
have** loads completely, with its values intact. `"acme"/"unobtainium-7" rev 42` is in
the test and in no library.

### Derived state

**17. Can derived mass or inertia be trusted from disk?** They are not there. A document
with geometry, a material, an assignment and a *computed* mass is serialized, and the
text is checked against 24 forbidden substrings — `mass`, `volume`, `centre_of_mass`,
`inertia`, `ixx`…`iyz`, `shear_modulus`, `bulk_modulus`, `completeness`, `fea_ready`,
`effective_material` and the rest. The mass is computed **before** the save, so nothing
is absent merely because it was never asked for.

After load the mass recomputes to the same value, then follows a density edit, then
follows a geometry edit.

**18. Can completeness results be persisted and become stale?** No. The report is rebuilt
from what loaded: `ThermalTransient` reports the same missing conductivity before and
after, and supplying it after load makes the report `Ready` — so it is genuinely
recomputed and not replayed.

**19. Can an effective material cache become canonical?** The file holds the **direct**
assignment only, and `effectiveMaterial` is resolved after load. An unresolved
assignment loads as unresolved and `effectiveMaterial` is null, which a cache would have
had to contradict.

### Malformed input

**20. Can a malformed material partially modify the document?** No, and it is structural:
`documentFromJson` constructs a **new** Document and returns it, so a failed load cannot
reach an existing one. Tested through the file API — a good file, then a bad one — with
the original's material and assignment checked intact and the good file still loading
afterwards.

**21. Can non-finite values enter through deserialization?** No, on two levels. JSON has
no `NaN` or `Infinity` token, so a file containing one is not JSON and the parser refuses
it — tested for all three tokens. And a number large enough to become infinite as a
double (`1e400`) is caught by an explicit finite check, rather than becoming an infinite
density.

**22. Can invalid physical ranges bypass public validation?** No. `materialFromJson`
builds the definition and hands it to `features::Material::create`, the same entry point
a caller uses, so the file goes through exactly the same validation. Nineteen malformed
cases are tested, each differing from a valid file in one thing: a zero density, a
negative modulus, a Poisson ratio of 0.5, a negative conductivity, a negative melting
temperature, unknown property keys, an unknown hardness scale, a hardness with no scale,
an unknown source kind, two kinds of bad date, a provenance record for an unknown
property, two records for one property, a string where a number belongs, a number where
an object belongs, a half-filled origin key, an origin revision below one.

### Backward and forward compatibility

**23. Can old files gain fabricated default materials?** No. An absent section means
"none of those", which is what a pre-P15 document meant by not having one. Tested: a
document with only a sketch and an extrude loads with zero materials, no assignment, and
a mass request that fails **for want of a material** rather than succeeding with an
invented one. The file is checked to contain no occurrence of "material" at all.

**No version bump was needed**, and that is the format's own rule rather than my
judgement: "adding a kind, an object type or an optional field needs no bump. Changing
what an existing field means needs one." P15 adds an object type and an optional
section. A version-1 file still loads; a version-3 file is refused by name.

**24. Can an unsupported future property law be silently misread as a constant?** No. The
document format's existing policy is to **reject** an unrecognised key, which is exactly
the right answer here — a reader that ignored a temperature-dependent law would compute a
confidently wrong answer from a constant. Tested by inserting a plausible
`"law": {"kind": "table", …}` into a conductivity and requiring the load to fail naming
it.

### Determinism

**25. Can a second save differ from the first?** No. Two saves of one unchanged document
are byte-identical, and so is save → load → save, twice — which is the stronger claim,
since it says the loader introduces no normalisation of its own. Materials are ordered by
ascending ID (checked against a fixture where that is *not* alphabetical) and provenance
arrays by property enumeration order (built in reverse, so the order cannot come from
insertion).

**26. Can locale alter numbers or dates?** Dates are ISO 8601 from
`materials::toString`, assembled from three integers with no locale facet, no time zone
and no clock; a test asserts `"2024-03-17"` appears in the file, and a
locale-style `"17/03/2024"` is rejected on read. Numbers go through nlohmann's own
serialization, which the repository already relies on for every other quantity.

**Can Debug and Release serialize different engineering state?** Answered by the
three-preset qualification, not by argument — and byte determinism is asserted within
each preset.

### My own four

**27. Does the loader require an assignment to resolve?** It must not. An assignment
naming a deleted material is the legitimate `Unresolved` state, and a loader that
refused it would turn a valid document into an unopenable one. Tested that a well-formed
assignment to an absent material **loads**, while a structurally malformed one — a
string, a negative number, a wrong key, an extra key, an array — is rejected. That is
the distinction the brief's item 48 asks for.

**28. Does the file carry anything machine-specific?** No. The serialized text is checked
for `C:\Users`, `C:/Users`, `bc-build` and `OneDrive`, none of which appear. Provenance
strings are the user's own text and are written as given; nothing derives a path from
the implementation.

**29. Do demanding floating-point values survive re-saving?** Yes, bit-exactly, through
**two** round trips: `210e9`, `0.33`, `167.3`, `12e-6`, `2.65e-8`, each compared with
`==` rather than a tolerance, because a formatting loss that only appears on the second
save is the failure this catches.

**30. Does an empty provenance record round-trip as empty, or as a record?** Neither — it
is rejected on read. An empty record and an absent one mean the same thing
(P15-PROV-001), so writing one would create a state with two representations;
`provenanceFromJson` refuses a record that states nothing, and the writer never produces
one.
