# P15-MAT-001 — Material Definition / Identity / Library

```text
STATUS:    see RESULT.
MILESTONE: P15-MAT-001
DATE:      2026-09-26
BASELINE:  03ae23a, clean tree, HEAD == origin/main,
           HEAD^{tree} = cd05aa1a0c59cf3cbff7421f530ac634cb7be84f
```

## PREREQUISITES

Verified before any production code was written, by counting the checklists
rather than by reading the summary lines:

```text
P15-ARCH-001         16 of 16 [x]     ADR-025..028 accepted
P15-UNITS-001        20 of 20 [x]     including "Determinism PASS"
INFRA-QT-DEPLOY-001   8 of 8  [x]
```

The determinism gate that blocked P15-UNITS-001 is genuinely closed, and closed
in the way that matters here: it passed from a build tree outside the
synchronised folder (11500 = 2300 x 5, exit 0). This milestone is therefore the
first qualified entirely from an external build root, and needed no controlled
rerun.

## SCOPE

```text
IN     MaterialId; the material document object and its metadata; the built-in
       library foundation and its immutability; import from library to document;
       lookup, deletion, collision and duplicate-designation behaviour;
       deterministic enumeration; compile-time identity safety
OUT    property VALUES of any kind (P15-MECH-001, P15-THERM-001), the
       Known/Unknown/Derivable property type (ADR-027), assignment to geometry
       (P15-ASSIGN-001), mass properties (P15-MASS-001), commands and undo
       (P15-CMD-001), the file representation (P15-PERSIST-001)
```

No property value is stored anywhere in this milestone. That is deliberate:
ADR-028 requires a recorded source per value, and filling the library with
plausible numbers to make it look finished is how wrong numbers enter a model.

## EXISTING IDENTITY AUDIT

Audited before designing anything, because ADR-025 says to use the existing
pattern rather than invent one for materials.

```text
strong IDs            Id<Tag, Value>; tags give distinct types and a
                      diagnostic name                       core/Id.hpp
document objects      isDocumentObjectTag<Tag> makes an ID widen implicitly
                      to ObjectId; the reverse needs a checked lookup
allocation            one IdAllocator per document, monotonic from 1
reuse                 never: "Values start at 1 and are never reused, even
                      after the identified item is deleted, so a stale
                      reference can never silently resolve to a newer item"
save/load             lastAllocatedId() is persisted and reserveIdsThrough()
                      restores it, so loading cannot hand out a used ID
object storage        std::map<ObjectId, unique_ptr<DocumentObject>> --
                      ORDERED, so enumeration is ascending by ID with no sort
names                 validateIdentifier: letters, digits and '_', not
                      starting with a digit; unique across the document;
                      Document::uniqueName() for collisions
```

Two consequences decided the design rather than being worked around:

- **`"Aluminium 6061-T6"` cannot be an object name.** It has a space and a
  hyphen. This is why a material has two names, exactly as ADR-025 says.
- **A document object already has everything a material needs.** No registry, no
  second allocator, no material-specific ownership. `createMaterial` adds an
  object, and that is all.

No UUID, string key, hash or random ID was introduced.

## MATERIAL ID

```cpp
struct MaterialIdTag { static constexpr std::string_view name = "material"; };
template <> inline constexpr bool isDocumentObjectTag<MaterialIdTag> = true;
using MaterialId = Id<MaterialIdTag>;
```

`MaterialId` **widens to `ObjectId`**, and that is the intended relationship, not
a leak: ADR-025 decided a material *is* a document object. The brief's
alternative — a separate identity domain where `MaterialId{42} != ObjectId{42}` —
is therefore not what was built, and the architecture is the authority.

Allocation and reuse are the document's, unchanged: monotonic from 1, never
reused. Nothing about materials needed a new mechanism, which is the point.

## MATERIAL DEFINITION

`features::MaterialDefinition` — everything a material is, apart from its
identity and its object name. Every field is descriptive; none is identity.

```text
designation   free text engineering label, may duplicate      "Aluminium 6061-T6"
standard      free text, "" when none is cited                "ASTM B221"
family        free text grouping label                        "Aluminium Alloy"
notes         free text
origin        optional MaterialLibraryKey -- provenance of an import, never a
              live reference
```

`features::Material` is the document object: `kTypeName == "material"`,
`clone()`, `contentEquals()`, and `materialId()` narrowing its own ID. It
declares no dependencies — things depend on *it* (an assignment, and through
that a mass), which is the direction that keeps the graph acyclic (ADR-026).

**NAME.** The object name is the existing identifier rule, unchanged. Decided
and tested: empty is rejected, over-long is rejected, a leading digit is
rejected, a space or hyphen is rejected, and a duplicate is rejected with
`AlreadyExists`.

**DESIGNATION / STANDARD / FAMILY / DESCRIPTION.** Free text on the title-block
convention, which is the repository's existing treatment of descriptive text:
stored exactly as given, **not trimmed, not case-folded, not Unicode-normalised**,
and empty is a legitimate value meaning "none given". No normalisation is the
whole point — case-folding is precisely how `"Steel"` and `"steel"` would become
one material.

`family` is free text rather than an enumeration or a category ID. No ADR fixes a
material taxonomy; an enumeration would need extending, and its file
representation migrating, every time a user has a material BetterCAD's authors
did not anticipate. A family no built-in entry uses is accepted, and tested.

## BUILT-IN LIBRARY

`core/materials/MaterialLibrary.hpp`, on the `core/standards/` pattern:
compiled-in `constexpr` data, no geometry, in `core` — which needs no layer-table
change, exactly as ADR-028 predicted, and `architecture.layering` passes.

```text
MaterialLibraryKey { library, entry, revision }    owning strings
LibraryMaterial                                    one entry, immutable
builtInMaterials()                                 span, fixed order
findLibraryMaterial(library, entry)                Result, NotFound if unknown
findLibraryMaterial(key)                           also checks the revision
```

**The key owns its strings, which deviates from the ADR's sketch, deliberately.**
ADR-025 sketches `MaterialLibraryKey` with `std::string_view` members. That is
right for naming a compiled-in entry and wrong for this: a document records the
key as provenance, so it is persisted state, and a `string_view` is a pointer —
which is exactly what the persistence-readiness audit forbids a persisted field
from holding. The views would dangle the moment a key came from a file rather
than from the table.

Four entries, **metadata only**: `al-6061-t6`, `steel-s235jr`, `stainless-304`,
`abs`. The designations and standard numbers are labels for these materials, not
measurements of them. `abs` deliberately cites **no** standard rather than an
invented one, in the spirit of `core/standards/`, which records what it does not
know. A test walks every entry and requires its notes to say it carries no
property values, so the day someone adds a plausible number without a source,
that test fails.

**LIBRARY IMMUTABILITY.** There is nothing to mutate: entries are `constexpr`
with no setters, so "a document must not modify the library" needs no rule
enforcing it and no test attempting it. What *is* tested is the consequence the
brief asks for: importing an entry, editing the document's copy, and finding the
library entry byte-identical afterwards — and two documents importing the same
entry, where editing one leaves the other alone.

**LIBRARY IDENTITY.** An entry's identity is its key. Enumeration order is the
table's declaration order, fixed across builds and runs, and is presentation
only. Tested by enumerating ten times, by requiring keys to be unique, and by
round-tripping every entry through its key back to itself.

## DOCUMENT-LOCAL MATERIALS AND THE LIBRARY BOUNDARY

`importLibraryMaterial(document, name, entry)` **copies** the entry's metadata
into a new document-owned material and records `entry.key()` as its origin. The
document never consults the library again — not at load, not while solving —
which is what makes a saved document self-contained (ADR-025). Two imports of one
entry produce two materials with two IDs and one shared origin.

**A library identity and a MaterialId cannot collide.** The brief asks what
happens if both can be `42`. They cannot: a library entry has no numeric ID at
all. It is named by a structured key of strings and an integer revision, and a
`MaterialId` is a strong type over a `uint64_t`. The recorded `origin` is
provenance and does not resolve to a document object. This is tested rather than
argued.

## LOOKUP

```text
findMaterial(document, id)                   const Material*, nullptr if absent
materialIds(document)                        ascending ID
materialCount(document)
findMaterialsByDesignation(document, text)   ALL matches, ascending ID
```

`nullptr` is the entire answer for a material that is not there. There is no
default material, no first material, no nearest designation. A `MaterialId` that
does not resolve stays unresolved.

`findMaterialsByDesignation` returns **every** match, so 0, 1 and N are
distinguishable and N is never silently narrowed to 1. The comparison is exact:
`"Steel"`, `"steel"` and `" Steel"` are three different designations, and each is
tested to find only its own material. An empty designation is a value and finds
the materials that have none.

It is named `materialIds()` and not `materials()` because `materials` is the
namespace holding the library, and a function of that name in `features` would
hide it — a small thing, recorded because the next reader will wonder.

**LOOKUP LIFETIME.** A returned pointer stays valid until that material is
removed. The document holds objects by `unique_ptr` in a node-based `std::map`,
so adding or removing *other* objects does not invalidate it — tested by holding
a pointer across the creation, modification and removal of another material.

## DELETION, ID NON-REUSE, NO SILENT REPLACEMENT

```text
removeMaterial(document, id)   Result<void>, NotFound if absent
```

An unused material can be deleted. Nothing refers to a material yet, so there is
no reference-integrity check to make here; the contract recorded for
P15-ASSIGN-001 is that removing an assigned material is the assignment's problem
to report, and never a reason to leave the material silently in place.

The hard invariant, and the one a later assignment depends on:

```text
create A -> id N ; delete A ; create B    =>  B.id != N, and N stays unresolved
```

Tested in the strongest form the brief asks for: create a material, give it a
designation, standard, family, notes and origin, delete it, then create another
with **the same object name and the same every field** — indistinguishable except
for identity. The old ID still does not resolve, and the new material has a
different one.

**COLLISION BEHAVIOUR.** An object arriving with an ID already in use — the shape
a loader takes — is rejected with `AlreadyExists`, and the existing material is
left untouched. Nothing is overwritten silently. A duplicate object *name* is
rejected the same way, and that rejection consumes no ID: the next material gets
the ID the rejected one would have, which is tested because a create that
silently burned an ID would make later identity harder to reason about.

An `origin` that could not be resolved later — no library, no entry, or a
revision below 1 — is rejected at creation, because a half-filled key makes
provenance a lie.

## DETERMINISTIC ENUMERATION

Ascending `MaterialId`, from the document's ordered map. Tested by enumerating
ten times, by checking insertion order rather than alphabetical order is what
comes out, and by deleting the first material and requiring the rest not to
renumber.

**No unordered dependence.** The new code contains no `unordered_map`,
`unordered_set`, hash or pointer ordering — checked by grep, not by memory. The
only container it enumerates is the document's `std::map`.

## COMPILE-TIME ID SAFETY

Five new cases in `tests/compile_fail/IdsMisuse.cpp`, testing only what the
chosen design intends:

```text
compile_fail.ids.object-to-material-id      ObjectId does not narrow to MaterialId
compile_fail.ids.material-id-as-feature-id  a material is not a feature
compile_fail.ids.sheet-id-as-material-id    nor is a sheet a material
compile_fail.ids.integer-to-material-id     identity never comes from a bare number
compile_fail.ids.material-id-to-integer     nor does a MaterialId become one
```

The control translation unit, built with no case macro, calls
`takesObject(MaterialId::fromValue(3))`, so the **intended** widening is proved
to compile in the same file that proves the misuses do not. Without that, a
MaterialId that had accidentally stopped being a document object would still pass
all five negative cases.

## PERSISTENCE READINESS

The file representation is P15-PERSIST-001 and is **not** implemented here. What
was audited is that the data model can be persisted without losing identity:

```text
MaterialId          uint64 value, already persisted for every document object
designation, standard, family, notes   owning std::string
origin              owning strings + int
```

No field holds a pointer, an address, an iterator or a container position. The
one place a view could have leaked into persisted state was `MaterialLibraryKey`,
and it owns its strings for exactly that reason.

**A material therefore cannot be saved yet, and that is loud.** The document
writer rejects an object type it does not know with "objects of type 'material'
cannot be saved" rather than skipping it, so a material is never dropped
silently. `tests/io/DocumentFileTests.cpp` pins that: saving a document holding a
material fails, the message names the type, and no partial file or temporary is
left behind. Without that test the gap could regress into silent data loss the
day someone makes the writer tolerant. See KNOWN LIMITATIONS.

**Undo readiness** is audited the same way: a material can be removed and
reinserted under its own ID, with its name and definition intact — the shape an
undo of a delete takes — and it returns to its place in the order. Commands
themselves are P15-CMD-001.

## THREAD-SAFETY

None claimed, and none invented. `IdAllocator` documents "Not thread-safe", and
the document is not thread-safe either; materials add no concurrency guarantee
and no global mutable state. The library is `constexpr` reference data, so it
cannot be raced on, and no test depends on execution order.

## TESTS

53 new tests, all discovered by CTest, taking the suite from 2300 to 2353.

```text
tests/features/MaterialTests.cpp          37   the document object end to end
tests/core/materials/MaterialLibraryTests.cpp  10   the library
tests/compile_fail/IdsMisuse.cpp           5   identity, at compile time
tests/io/DocumentFileTests.cpp             1   saving refuses, loudly
```

Every test goes through the production API — `createMaterial`,
`importLibraryMaterial`, `setMaterialDefinition`, `findMaterial`,
`removeMaterial`, `Document::rename` — and none through a private back door.

## ADVERSARIAL REVIEW

Every question from the brief's attack list was answered against the code, not
from memory. Four findings, all real. Three were fixed before the first
qualification; the fourth was found BY it, and cost that run.

**1. `contentEquals` used `static_cast` on an unvalidated type.** The base class
documents that it is called only with an object of the same `typeName()`, and
`equivalent()` does check that first — but it is a *public virtual*, so any
caller can reach it directly, and a `static_cast` there is undefined behaviour
rather than a wrong answer. `Sheet`, the closest existing model, uses a checked
`dynamic_cast`. Now so does `Material`. Found by comparing against the
repository's convention rather than against the comment.

**2. A `MaterialId` from one document resolves in another.** Two fresh documents
both allocate from 1, so material 1 of A and material 1 of B are different
materials with equal IDs. This is inherent to document-local `ObjectId`s
throughout BetterCAD, and the established cross-document mechanism is
`ObjectReference` — durable identity plus an untrusted locator. It was
nevertheless **accidental as written**, which the brief forbids, so it is now
explicit: a test constructs exactly that situation, shows the ID resolving to the
other document's material, and records why a cross-document reference must carry
a `DocumentId`.

**3. Three cases had no test.** An empty designation as a search term; a
non-ASCII designation stored byte-for-byte with no normalisation; and a document
`clone()` preserving material identity and content. All three now exist. The
second matters most: it pins the no-normalisation decision, so a later
"helpful" Unicode fold cannot merge two materials without a test failing.

Answered and already covered, with the test that covers each:

```text
renaming changes the ID          RenamingDoesNotChangeItsId
two "Steel"s collapse            TwoMaterialsMayShareADesignationAndStayDistinct
name lookup picks one            ADesignationLookupReportsEveryMatchAndNeverPicksOne
deleted ID reused                ADeletedIdIsNeverHandedOutAgain
stale ID rebinds                 ADeletedIdStillDoesNotResolveAfterAnIdenticalMaterialIsCreated
document edit reaches library    EditingAnImportedMaterialLeavesTheLibraryEntryUntouched
library edit reaches documents   impossible: entries are constexpr
enumeration order alters identity  EnumeratesInAscendingIdOrderEveryTime
container order alters IDs       IDs come from the allocator, not a container
duplicate ID overwrites          AnIdAlreadyInUseIsRejectedRatherThanOverwriting
standard/designation as identity no API resolves by them
lower-casing merges materials    ADesignationLookupIsExactSo...DoNotMergeMaterials
empty name breaks lookup         ObjectNamesStayUniqueAndAreNotIdentity
MaterialId substituted for ObjectId  the five compile-fail cases
library/local ID collision       ALibraryIdentityAndAMaterialIdCannotBeConfused
pointer dangles after insertion  APointerFromLookupSurvivesEditsToOtherMaterials
global mutable library           there is none; the library is constexpr
```

**4. The shared build refused to link, and the gate is what found it.** This one
did not come from review at all, which is the point of having the gate: the first
qualification of this milestone failed at `debug-shared-ext build exit 1` with

```text
undefined reference to `__imp__ZN9bettercad8features8Material9kTypeNameE'
```

Two lines in the new tests used `Material::kTypeName` from outside the features
DLL. Binding a reference to a dll-imported `constexpr static` does not link, and
the repository already knew: `DocumentJson.cpp` carries the finding, attributed
to the same preset, and its answer is to compare against the literal and pin the
two together with a `static_assert`. That is now what the tests do — the
`static_assert` is a constant expression, so it needs no link-time symbol and
still catches the constant drifting from the literal.

`DatumTests.cpp` uses `DatumPlane::kTypeName` in a `CHECK` and links, because
that comparison constant-folds and the symbol is never materialised; a `REQUIRE`
and a comparison against a runtime value do not fold, which is why these two
lines failed and that one does not.

Worth recording plainly: the evidence written before that run predicted the
shared build would catch something here and named the wrong mechanism —
`dynamic_cast` needing `Material`'s type information exported. That prediction
was wrong about the cause and right about the preset. `dynamic_cast` across the
DLL boundary does work, which the 47 material tests passing under
`debug-shared-ext` now confirm; the export on the class is what makes it work.
The first, otherwise-complete qualification was discarded rather than cited,
because fixing this changed `tests/`.

## DETERMINISM

The contract that is actually promised, tested rather than assumed: the same
sequence of operations on a fresh document gives the same IDs, the same
enumeration order and the same metadata.
`Material_TwoDocumentsBuiltTheSameWayAgreeOnEverythingObservable` builds two
documents identically -- including one create-then-delete, so the allocator is
not left at a trivial state -- and compares every ID, name and definition.

What is **not** claimed is that two *unrelated* documents use the same IDs. They
are independent allocators and nothing in the architecture requires them to
agree; `Material_AMaterialIdMeansSomethingOnlyInsideItsOwnDocument` records the
opposite, that equal numbers in two documents are different materials.

Library enumeration and document enumeration are each traversed ten times in one
test and required to be identical, and both come from ordered containers, so
neither can vary between runs, builds or configurations.

## FULL REGRESSION

Three presets, each from the external build root
`%LOCALAPPDATA%\bc-build`, configured, cleaned, built with warnings as errors,
proved fresh by a no-op rebuild, and only then tested.

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2353/2353      0
release-ext            0        0      0          0        2353/2353      0
debug-shared-ext       0        0      0          0        2353/2353      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   11765 = 2353 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   11765 = 2353 x 5, 0 failures
qualification finished 02:07:03, 0 stage(s) failed
```

2353 = the 2300 of INFRA-QT-DEPLOY-001 plus the 53 added here. All three presets
discover the same 2353, so no test was lost or duplicated by the new
registration.

The harness was proved able to fail before it was trusted: `verify-harness.cmd`
points it at a preset that does not exist and requires a non-zero exit -- 3
stages failed, exit 3.

**No OneDrive replace fault, anywhere.** `Permission denied`, `cannot replace`
and `Not Run` appear in none of the logs. This is the first milestone qualified
entirely from a build tree outside the synchronised folder, and the fault that
cost six milestones a controlled rerun did not occur once across 23530 repeat
executions plus three full suites. No controlled-rerun policy was needed or used.

**Qualified tree = committed tree.** The eight source tree IDs recorded before
the first build and after the last test run are identical, and equal to the
committed tree:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include 6100a7aba9ab60a7b1727253c2697957cdbda0b1
src 84dcb495de10d4c32d4044278c5191d7a25a6719
tests 2bf63ab111c822ff3848117d44ae06aaaefda9f3
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

An earlier run of this same chain was **discarded, not cited**: its
`debug-shared-ext` build failed on the dll-imported constant (ADVERSARIAL REVIEW
4), and fixing that changed `tests/`, which voids a qualification. Its other two
presets and both of its repeat stages had passed; that is not evidence for
anything and is not used as such.

## KNOWN LIMITATIONS

- **A document containing a material cannot be saved.** The file representation
  is P15-PERSIST-001. The failure is loud, named and tested, and nothing is
  dropped or half-written — but it does mean materials are not usable end to end
  until that milestone. This is the largest limitation of the milestone and is
  recorded here rather than left for a user to discover.
- **No property values anywhere**, by design (ADR-027, ADR-028). A material at
  this milestone says what it is, not what it does.
- **No commands, so no undo**, though the data model is shown to support one.
- **No assignment to geometry**, so nothing yet refers to a material. The
  non-reuse invariant that a future assignment will depend on is nevertheless
  established and tested now, because retrofitting it would be far harder.
- **Text is not validated as UTF-8** at the document level. The repository
  validates encoding at the io boundary ("Expressions that are not UTF-8 cannot
  be saved"), and materials will meet that check when they are persisted.
- **`family` is free text**, so there is no taxonomy and no grouping guarantee.
  Deliberate; an enumeration needs an ADR and a migration path.

## RESULT

```text
TASK:            P15-MAT-001 -- Material Definition / Identity / Library
BASELINE:        03ae23a, clean, HEAD == origin/main
PREREQUISITES:   P15-ARCH-001 16/16, P15-UNITS-001 20/20, INFRA-QT-DEPLOY-001 8/8
IMPLEMENTATION:  MaterialId (widens to ObjectId, ADR-025); the Material document
                 object with designation, standard, family, notes and import
                 provenance; core/materials/ built-in library, 4 metadata-only
                 entries, immutable by construction; import-by-copy; lookup,
                 deletion, collision and duplicate-designation semantics
TESTS:           53 new, 2300 -> 2353. 37 document, 10 library, 5 compile-fail,
                 1 that saving refuses loudly
VALIDATION:      every invariant through the production API; identity proved
                 stable across rename and across each metadata edit separately;
                 a deleted ID proved not to rebind to an otherwise identical
                 material; the library proved unchanged by a document edit
ADVERSARIAL:     21 questions from the brief plus the repository's own
                 conventions; 4 findings, 4 fixed, 0 remaining. One was found by
                 the debug-shared gate and cost a complete qualification run
DETERMINISM:     11765 = 2353 x 5 in release-ext and again in debug-ext, 0
                 failures, no controlled rerun
REGRESSION:      3 presets from an external build root, 2353/2353 each, 0
                 warnings, fresh binaries, 0 stages failed
RESULT:          PASS
EVIDENCE:        docs/verification/P15-MAT-001/
NOT CLAIMED:     that a material can be saved. The file representation is
                 P15-PERSIST-001; saving refuses loudly and is tested to do so.
                 No property value exists yet, by design (ADR-027, ADR-028).
TODO:            P15-MAT-001 18 of 18 ticked. P15-MECH-001 NOT started.
```

## REVISION

```text
2026-09-27  Implemented, adversarially reviewed and qualified. Four review
            findings, all fixed: an unchecked static_cast in a public virtual; a
            cross-document MaterialId made explicit rather than left accidental;
            three missing tests; and a dll-imported constexpr static that would
            not link in a shared build, which the debug-shared gate caught and
            which voided the first, otherwise complete, qualification run. The
            evidence written before that run predicted the shared build would
            find something and named the wrong mechanism; that is corrected in
            ADVERSARIAL REVIEW 4 rather than quietly removed.
```
