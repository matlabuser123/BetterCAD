# ADR-025 — A material is a document object; the library is reference data it is imported from

```text
STATUS:    Accepted
DATE:      2026-09-26
MILESTONE: P15-ARCH-001
TOUCHES:   ADR-002 (assemblies live in the document), ADR-003 (references are
           internal for P13), ADR-007 (one configuration system)
```

## Context

BetterCAD has **no material concept at all** today. That was established by
audit rather than assumed: every occurrence of "material" in `include/` and
`src/` is either geometric prose ("material in either body", "through all
material") or `drawing::MaterialRemoval`, which is the ISO 1302 surface-finish
symbol and has nothing to do with engineering data. So there is no existing
concept to extend and no duplicate to avoid — but also no established ownership
pattern for materials to copy.

Two patterns in the repository are close enough to decide from.

**`core/standards/` — immutable reference data.** `HoleTolerances.hpp`,
`ClearanceHoles.hpp` and `MetricThreads.hpp` hold "tabulated data from published
standards, with no geometry", transcribed from sources recorded in
`docs/verification/P12-HOLE-001/` and checked there against independent copies.
It is compiled into the binary. It also, importantly, **documents what it does
not know**: `HoleDeviation` omits JS because "copies of ISO 286-2 disagree on
whether its odd tolerances of grades 7 to 11 are rounded to whole micrometres,
which BetterCAD cannot settle from the sources it has". Reference data here
refuses to guess.

**How a document uses that reference data: it stores the designation, not the
value.** `HoleClearance{standards::MetricThread bolt, ClearanceSeries series}`,
and the comment is explicit — "its diameter comes from ISO 273, so `diameter`
stays zero". The number is resolved at regeneration and never persisted. The same
for `HoleThread` and for `HoleToleranceClass`.

That works for ISO 273 because the table is tiny, stable, compiled in, and
published. **It does not transfer to materials**, and the reason is the whole
point of this ADR: a material library is open-ended, user-extensible, and its
values are engineering data that a document's computed results depend on. If a
document stored only "Aluminium 6061-T6" and the library were consulted at solve
time, then correcting a density in a later build would silently change the mass,
the stresses and the thermal results of every saved document that named it. That
is the same class of failure as ADR-024's silent rebind, one level up: the
reference still resolves, and it means something else.

Two further audited facts constrain the design.

**Object names are identifiers, and unique per document.**
`validateIdentifier` allows "letters, digits and '_', and do not start with a
digit", and `Document.hpp` states "Names are unique across parameters and
objects, so lookup by name is [unambiguous]". So `"Aluminium 6061-T6"` cannot
even *be* an object name — it has a space and a hyphen — and two objects in one
document cannot share a name.

**`ObjectReference` is the established way to name something outside this
document**: `{ObjectId object, optional<DocumentId> document, string hint}`,
where the hint is "a locator, never identity, and never trusted: a candidate
found through it is accepted only if its own identity matches `document`".

## Decision

**A material definition is a document object.** It gets an `ObjectId`, a
`kTypeName`, a place in the dependency graph, undo/redo, persistence and naming
from machinery that already exists and is qualified.

```cpp
struct MaterialIdTag { static constexpr std::string_view name = "material"; };
using MaterialId = Id<MaterialIdTag>;          // widens to ObjectId
inline constexpr bool isDocumentObjectTag<MaterialIdTag> = true;
```

**Identity is the `MaterialId`, and never a name.** The allocator's existing
contract carries the weight: "Values start at 1 and are never reused, even after
the identified item is deleted, so a stale reference can never silently resolve
to a newer item."

**A material carries TWO names, because the repository forces it.**

```text
object name      an identifier, unique in the document   Al6061T6
designation      free text, may duplicate                "Aluminium 6061-T6"
```

The object name is what a CLI types without quoting, exactly as a parameter or a
configuration name is. The designation is the engineering label and is *not*
identity: two materials may carry the designation `"Steel"`, hold different
property values, and remain distinct because their `MaterialId`s differ.

**The built-in library is reference data in `core/materials/`, modelled on
`core/standards/`:** immutable, compiled in, no geometry, transcribed from
sources recorded in the milestone that adds it. A library entry is **not** a
document object and has no `ObjectId`. It is named by a structured key:

```cpp
struct MaterialLibraryKey {
    std::string_view library;   // which library, e.g. "bettercad"
    std::string_view entry;     // stable entry key, NOT the designation
    int revision = 0;           // the library's own revision of that entry
};
```

**Using a library material imports it.** A document-owned material object is
created from the library entry, the entry's values are copied in as the
document's own authoritative values, and the key and revision are recorded as
provenance (ADR-028). Assignments only ever name a document material.

## Consequences

**A saved document is self-contained for engineering purposes.** Its property
values are its own. A library correction, a library addition, a newer build, or
a missing library cannot change what a saved document computes.

**The brief's "missing external library" problem dissolves rather than being
solved.** There is no library lookup at load time or solve time, so there is no
`UnresolvedMaterialReference` for library data, and therefore no opportunity for
a name-based fallback to "the nearest material" or "generic steel". The failure
mode is designed out instead of guarded against.

**Re-syncing with the library is an explicit user action**, not a side effect.
Because provenance records the key and revision, a later milestone can offer
"this material came from bettercad/al-6061-t6 rev 2; the library now has rev 3"
— and the user decides. Nothing changes under them.

**Editing a library material is impossible, and needs no rule forbidding it.**
Library entries are `constexpr` reference data; there is nothing to mutate. A
user edits the *document's* material, which is a copy, so one document's edit
cannot reach another document — the test the brief asks for
("user edits Aluminium 6061-T6 density … must not mutate every other document")
passes by construction.

**Two materials with the same designation coexist; two with the same object name
do not.** That is inherited from the document's unique-name rule and is not
special to materials. It is a real constraint and is recorded as such: if a user
imports `"Steel"` twice, the second object needs a different identifier-style
name (`Document::uniqueName()` already exists for exactly this), while both may
keep the designation `"Steel"`.

**The library costs binary size, as `core/standards/` already does.** A large
library would want to move out of the binary later; that is a packaging decision
for the milestone that adds real data, and this ADR does not pre-empt it. What it
does fix is that whatever the library's storage, the *document* never depends on
it.

## Alternatives rejected

**Live reference to the library, storing only a key** — the ISO 273 pattern.
Rejected: it makes a document's computed mass and stresses depend on data outside
the document, so a library revision silently changes saved results. ISO 273 gets
away with it because it is a published, compiled, effectively frozen table and
because a clearance diameter is a *modelling input* the user chose by
designation; a material's density is a *physical measurement* that gets
corrected.

**Library reference plus version, resolved at load** (pattern B in the brief).
Rejected as the worst of both: the document still cannot open meaningfully
without the library, and a version mismatch leaves the user with a document that
will not compute rather than one that computes what it always did.

**Document-owned only, with no library** (option B). Rejected: it makes every
user retype engineering data, which is exactly how wrong numbers enter a model,
and it throws away the `core/standards/` precedent for curated, source-recorded
reference data.

**Global library only** (option A). Rejected: no custom materials, and no way to
record a supplier's measured values for a specific batch, which is ordinary
engineering practice.

**A material as document-level state rather than a document object**, like
`ConfigurationId`. Rejected: materials need naming, undo/redo, a place in the
dependency graph so that changing a density can invalidate a derived mass, and
individual deletion — all of which document objects already have and
document-level state does not.

**Identity by designation string.** Rejected outright: it collapses two
different "Steel"s into one, and renaming would change identity. It is the
defect ADR-024 was written to remove, in a new place.

## Validation

Each claim below was checked against the tree at `d9739a6`, not assumed.

```text
Id<Tag> + isDocumentObjectTag supports a new document-object ID   Id.hpp:39,90,158
IdAllocator never reuses an ID, even after deletion               Id.hpp (doc)
object names are identifiers: letters, digits, '_'                Naming.cpp:29-32
object names are unique per document                              Document.hpp:41
Document::uniqueName() exists for name collisions                 Document.hpp:267
core/standards/ is compiled, immutable, no geometry               core/standards/*.hpp
standards data documents what it does NOT know                    HoleTolerances.hpp
a document stores a standard's DESIGNATION, not its value          HoleFeature.hpp:47-49,91-96
ObjectReference = durable identity + untrusted locator             ObjectReference.hpp:48-57
there is no existing material concept to collide with              audited: 0 hits
```

## Invariants

```text
a material's identity is its MaterialId, never its object name and never its
    designation
a library entry is immutable and is never mutated by a document
a document's property VALUES are the document's own, always
no library lookup happens at load time or at solve time
a missing or changed library cannot change what a saved document computes
no material is ever resolved by designation
```
