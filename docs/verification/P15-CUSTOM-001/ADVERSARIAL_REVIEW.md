# P15-CUSTOM-001 — Adversarial review

**Aim: disprove the milestone.** The 23 attacks the brief names, plus 3 of my own.

**Result: 5 findings.** Two are defects in this milestone's own work, both fixed.
Three are places where the brief's assumptions do not hold in this tree — recorded
rather than worked around, because in each case the architecture's answer is the
better one and pretending otherwise would have meant building a second material
model beside the qualified one.

**No production defect survived.** Nothing in the implementation was found wrong by
these attacks; the two real defects were in the tests and in an API signature.

---

## Findings

### F1 — Two materials cannot share a NAME, and the brief assumes they can (ASSUMPTION, recorded + tested)

The brief's item 5 says: create `A name = "Steel"`, `B name = "Steel"`, require
`A.id != B.id`, "both remain valid".

In this tree the second creation is **refused**. An object name is unique across a
whole document — shared with parameters, even — because P15-MAT-001 made the name an
identifier that can be typed on a command line without quoting. What may legitimately
duplicate is the **designation**, the engineering label, and `MaterialDefinition`
keeps the two apart for exactly this reason.

So the milestone has two things to prove, not one:

```text
duplicate NAME          must be REFUSED, and change nothing
duplicate DESIGNATION   must be SAFE, and resolve by identity
```

Both are now tested. The refusal case also checks that no ID was consumed and that
`Document::uniqueName()` is the supported way to get a free one — so the document
never invents a name like `Steel_2` that the user did not choose.

**Had I followed the brief literally** I would have had to relax name uniqueness,
which would have broken every CLI lookup and contradicted P15-MAT-001.

### F2 — The built-in library has no property values, so the brief's central clone fixture cannot be written (ASSUMPTION, recorded + fixture relocated)

The brief's items 7, 14, 36 and 59 all rest on a library entry that carries
`density = 2700`, `E = 69 GPa`, `k = 167`. **The built-in entries carry metadata
only** — designation, standard, family, notes, and nothing else. P15-MAT-001 declined
to put numbers there because ADR-028 requires a recorded source for any property
value, and the entries say so in their own notes: "Metadata only: this entry carries
no property values."

So "clone the library's aluminium and check its density did not move" is not a test
that can exist: there is no density there to move.

Rather than invent library values to satisfy the fixture — which would have put
unsourced numbers into the shipping library, the exact failure ADR-028 exists to
prevent — the deep-independence fixture was **relocated to where properties actually
live**: a document-local material. That is `cloneMaterial`, which the brief itself
asks for at item 26, so nothing was skipped; the property-carrying clone test is the
document-to-document one.

The library half is still tested for what the library actually has: metadata copied,
provenance recorded, and every property of an imported material **Unknown, not zero**.
That last assertion is over the whole property enumeration, so a future library
revision that adds values will fail it — which is the right place to be reminded that
provenance has to come with them.

### F3 — `hasMaterialProperty` answered a question nobody asked for derived kinds (DEFECT, fixed in design)

First draft: `hasMaterialProperty(document, id, ShearModulus)` returned whether a
shear modulus was **stored**. It never is (ADR-027), so it returned `false` for a
material with a perfectly good E and nu — from which BetterCAD can report G exactly.

That is a true statement about storage and a misleading answer to the question a
caller is asking, which is "can I get a shear modulus out of this material". Changed
to report whether the **inputs** are present, i.e. `hasLinearElasticConstants`, and
the header says so. A caller wanting the storage question asks
`definition().mechanical` directly, and `isDerivedKind()` already tells them which
kinds have no storage.

### F4 — A test asserted `PoissonRatio::of(0.5)` would refuse construction (DEFECT, fixed)

The atomicity test originally asserted that `PoissonRatio::of(0.5)` and
`PoissonRatio::of(-1.5)` fail to construct. They do not: `PoissonRatio::of` carries
**no range check**, deliberately — P15-ARCH-001 keeps dimensional validity apart from
physical validity, and `-1 < nu < 0.5` is the latter, enforced by
`materials::validate` in the property layer.

The test compiled only because I had also mis-written `of()` as returning an optional,
so the error surfaced as a compile failure rather than as a false pass. Rewritten to
push both values **through the setter**, which is where they are actually refused —
and which is the stronger test, since it proves the whole definition is unchanged
afterwards rather than that a constructor declined.

### F5 — A clone of an imported material is indistinguishable from a direct import (LIMITATION, recorded)

`cloneMaterial` carries `origin` over unchanged, which is right: origin records where
the **values** came from, and cloning does not change them. But it means these two
materials are byte-identical in canonical state:

```text
imported from bettercad/al-6061-t6 rev 1
cloned from a material that was imported from bettercad/al-6061-t6 rev 1
```

Both hold values that came from that entry, so both say so, and neither is lying. What
cannot be answered today is "which document material did this one come from".

Not fixed here. Recording it needs a provenance model that distinguishes a library
source from a document one, which is **P15-PROV-001's** subject, and inventing half of
that model now would be the thing this milestone is meant to avoid. What P15-CUSTOM-001
does guarantee is that the state is *representable*: the clone is an ordinary document
material with its own ID, so a `clonedFrom` field can be added beside `origin` without
migrating anything.

---

## The 23 attacks

### Storage and aliasing

**1. Can editing a clone mutate the library source?** No, and not by accident either.
The entries are `static constexpr LibraryMaterial kEntries[]` with **no setters at
all**, only const accessors, and private `string_view` members over string literals.
`importLibraryMaterial` copies each field into a `std::string`. Tested by editing
every field of an import, renaming it and removing a property, then comparing the
library entry field by field, by `key()` and by `operator==`, and checking the table
is still the same four entries in the same order. Six compile-fail cases cover the
mutation paths a caller might reach for.

**2. Can nested property objects share mutable storage?** No — **audited, not
assumed**. `MaterialDefinition` is four `std::string`, one
`std::optional<MaterialLibraryKey>` (two owning strings and an int) and two structs of
`MaterialProperty<T>`, where every `T` is a `Quantity` (a double), a `PoissonRatio`, an
`Elongation` or a `Hardness` — all value types holding doubles. A grep of both property
headers finds no pointer, reference, view, `span` or smart pointer **in any member**;
the only `<span>` and `<string_view>` in them are includes for free functions' return
types. Copying a definition is therefore deep by construction.

Proven at runtime the hard way: the cross-document clone test destroys the source
document inside a scope and then reads every field of the clone, including its hardness
scale and thermal conductivity. A view or pointer would be dangling there.

Note that `MaterialLibraryKey` **owns** its strings, and that is a deliberate
P15-MAT-001 deviation from ADR-025's `string_view` sketch, made for exactly this
reason: it is persisted state, and persisted state must not hold a pointer.

**3. Can cloning preserve the source MaterialId?** Structurally no. The ID lives on
`DocumentObject`; `MaterialDefinition` has no identity field. `cloneMaterial` copies
only the definition and hands it to `createMaterial`, which allocates a fresh ID.
Compile-fail cases prove `definition.id` and `definition.materialId` do not exist.

**4. Can ordinary C++ copying create duplicate IDs?** No. `Material::clone()` — the
`DocumentObject` virtual that undo/redo uses — **does** keep the ID, which is exactly
the trap. `Document::addObject` refuses any object that already carries one ("use
insertObject()"), and `insertObject` refuses an ID already in use with
`AlreadyExists`, before touching any state. Tested through both doors, with the
existing material's values checked intact afterwards and `cloneMaterial` then shown to
work.

**5. Can an inserted duplicate ID overwrite or merge?** No: `insertObject` returns
`AlreadyExists` and returns before `objects_.emplace`. Tested; one material before, one
after, values unchanged.

**6. Can cross-document cloning share mutable storage?** No — see attack 2, tested by
outliving the source document.

**7. Can a library material be mutated through an iterator, pointer or reference?**
`builtInMaterials()` returns `std::span<const LibraryMaterial>`, so no mutable
reference can be bound to an entry; the compile-fail case proves binding one "discards
qualifiers". `findMaterial` returns a `const Material*`, so a document material cannot
be edited behind the document's back either — that would skip the revision bump.

### Identity

**8. Can two same-name materials collapse?** They cannot both exist — see F1. Two
same-**designation** materials can, and must not collapse:
`findMaterialsByDesignation` returns **all** matches in ascending ID order, and the
three-way fixture (one import, two customs, all designated "Aluminium 6061-T6") checks
that all three come back and that each resolves to its own values.

**9. Can local-first or name-first lookup choose the wrong material?** There is no
name-based material lookup to get wrong. The materials API offers `findMaterial(id)`
and `findMaterialsByDesignation` (all matches). No function returns "the" material for a
designation, so there is no preference order to be wrong about.

**10. Can a property edit mint a new identity?** No. One test performs a rename, a
metadata edit, a mechanical edit, a thermal edit and a **removal** on one material and
then checks the ID, the count and the enumeration. A second renames four times.

**11. Can a deleted ID rebind to an identical replacement?** No. Delete A, create B
with identical name, metadata and properties: `B.id > A.id`, the assignment still names
A and is **Unresolved**, and `findMaterial(A)` is null. The allocator only counts up.

### Property semantics

**12. Can removing E turn it into zero?** No — and this is the one gate I
**mutation-tested**. Changing `clearMechanical` to store a zero density instead of
Unknown made **6 test cases fail**. (The mutation also made the removal *fail*
outright, because a zero density is refused by validation — so the architecture resists
it twice.)

**13. Can invalid E partially overwrite the old E?** No. Validation runs before the
document is touched. Nine invalid values — E negative, density zero, density negative,
yield zero, nu = 0.5, nu = -1.5, k = 0, cp negative, and a mixed struct with two valid
fields and one invalid — each leave the **whole definition** equal to what it was, and
leave the document revision unmoved.

**14. Are multi-property edits atomic?** Yes, by construction rather than by promise:
these setters replace a whole `MechanicalProperties` or `ThermalProperties` struct, so
one bad field rejects the lot. Tested explicitly with valid density + invalid E + valid
yield: none of the three lands.

**15. Can clone of Unknown yield zero?** No. Tested over the whole property
enumeration on a deliberately partial material, and again on an imported one, where
**every** property is Unknown.

**16. Can clone lose the hardness scale?** No — 60 HRC stays 60 HRC, checked as value
and scale separately, and again after an unrelated edit. This is the case the
field-by-field mutation in attack 22 was built to catch.

**17. Can clone lose temperature metadata?** No. A density recorded at 293.15 K and a
conductivity at 373.15 K both survive the clone, and survive an unrelated edit to a
third property.

**18. Can removal ambiguously mean Unknown versus an inherited fallback?** There is no
inheritance, so removal has exactly one meaning. Four compile-fail cases prove the
alternative is unrepresentable: no `baseMaterial`, no `overrides`, no
`property.isInherited()`, no `PropertyState::Inherited`.

**19. Can override semantics mix copied and inherited values?** No, and this is the
milestone's central gate. **ADR-025 already chose snapshot semantics** — a document
owns its values, and nothing consults a library entry at load time or solve time — so
model B of the brief (base reference plus override map) is not merely unused, it is
absent. The compile-fail group proves the absence. A removal test then shows that
editing or removing one property leaves every other field of the definition equal,
compared member by member including the whole thermal half.

### Derived data

**20. Can changing density leave a stale mass?** No. Mass is derived and never stored
(ADR-026). Tested end to end: 2700 kg/m³ over a 30000 mm³ box gives 0.081 kg; editing
the density to 7850 gives 0.2355 kg with the same volume, the same material identity and
the same assignment, and the inertia moves in the same ratio. Removing the density turns
the mass into a diagnostic naming the material.

**21. Can changing E leave stale derived G or K?** No — they are computed on request,
so there is nothing to go stale. Tested: G follows an E edit and then a nu edit, and
`requireLinearElasticConstants` agrees.

**22. Can a supplied G be silently overwritten after an E edit?** **Unrepresentable.**
`MechanicalProperties` has no slot for a shear or bulk modulus (ADR-027: storing one
would be a second source of truth that could disagree with the first), so a supplied G
cannot exist to be overwritten. Two compile-fail cases pin the absence. This is a case
where the honest answer is that the question does not arise, not that it was tested.

**23. Can derived mass properties become copied canonical material state?** No.
`MaterialDefinition` has no mass, volume, centroid or inertia field; compile-fail cases
prove it. A related guarantee, tested rather than asserted: no edit can store a
property that **claims** to be `Derived` — `validate()` refuses it in every slot, for
mechanical and thermal alike, and through creation too, so a clone cannot be seeded
with one.

### Persistence and provenance

**24. Can persistence later distinguish a custom snapshot from a library origin?**
Partly. An imported material has an `origin` key with the library, entry and revision;
a from-scratch material has none, and none is invented for it — both tested. What cannot
be distinguished is a direct import from a clone of one; see **F5**.

Nothing in the canonical state depends on a pointer, an address or a container position
(attack 2), so all of it is serializable when P15-PERSIST-001 arrives. Until then the
gap stays **loud**: a new io test saves both documents of a cross-document clone and
requires each to fail with a diagnostic naming the type, with nothing half-written. A
writer taught to skip a material it considered "just a copy" would fail there.

**25. Can Debug and Release enumerate custom properties differently?**
`mechanicalPropertyKinds()` and `thermalPropertyKinds()` are spans over static arrays in
declaration order; `materialIds()` walks the document's ordered object map. No unordered
container, hash or pointer ordering is reachable. Answered by the three-preset
qualification, not by this argument — see the README's RESULT.

### My own three

**26. Is `cloneMaterial(document, document, ...)` safe, passing one document as both a
mutable and a const reference?** Yes, and deliberately so: the definition is copied out
**before** `createMaterial` touches the document, and the source pointer is never used
afterwards. So the answer does not depend on whether `objects_` invalidates pointers on
insert (it is a node-based `std::map`, so it does not — but the code does not rely on
that). The comment in the implementation says exactly this.

**27. Does removing an already-Unknown property report a spurious change?** No. It
returns `false` and the document revision does not move, which is the same contract
every other setter here has. Tested.

**28. Do property operations on a non-material object silently succeed?** No. An ID
that names a sketch fails with `NotFound` from `removeMaterialProperty`, because
`findMaterial` uses a checked `dynamic_cast`. `hasMaterialProperty` answers `false`
rather than failing, because "is there a density" does have an answer for a material
that is not there. Both tested.
