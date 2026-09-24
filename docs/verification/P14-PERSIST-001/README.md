# P14-PERSIST-001 — Drawing Persistence

```text
STATUS:    PASS
MILESTONE: P14-PERSIST-001
DATE:      2026-09-24
BASELINE:  4d92bd5 (P14-CMD-001)
```

## TASK

Persist canonical drawing intent, and never treat generated drawing geometry
as authoritative persisted state.

## SCOPE

Authorized by `TODO.md`, `P14-PERSIST-001`. Not started here: `P14-CLI-001`,
`P14-EXPORT-001`, `P14-REFMOD-001` or anything later.

## THE RESULT, FIRST

**No production code was changed, and that is the finding rather than a
shortfall.** Drawing persistence was already correct: each P14 milestone
shipped its own serializer, its own deserializer and its own persistence tests
as it introduced its object kind. This milestone is the audit that no
single-type milestone could do — the whole file, with every P14 object kind in
it at once — and it found the contract already met.

What it adds is 18 tests and three recorded decisions. What it changes is
nothing.

## BASELINE

Clean at `4d92bd5`, `HEAD == origin/main`.

The audit began by establishing what already existed, because the brief's
first rule is not to create a second file format:

```text
src/io/json/SheetJson.cpp        P14-SHEET-001
src/io/json/ViewJson.cpp         P14-VIEW-001 / 002
src/io/json/DimensionJson.cpp    P14-DIM-001, extended by P14-TOL-001
src/io/json/AnnotationJson.cpp   P14-ANNO-001, extended by P14-TOL-001
                                 and P14-BOM-001
```

all four dispatched from `DocumentJson.cpp` inside the existing object
envelope, and all four already carrying tests:

```text
SheetFileTests            11 cases, including determinism and legacy loading
ViewKindTests             every view kind round-trips; an unknown kind refused
DimensionTests            round trip; THE FILE CARRIES NO MEASURED VALUE
AnnotationTests           round trip; the file carries no derived text
ToleranceTests            GD&T semantics round-trip; no measured number
BomTests                  BOM round-trips as intent; a saved quantity cannot
                          override the assembly
StableReferenceTests      the persisted form carries no index or handle
```

So the question this milestone had to answer was not "does a sheet
round-trip" — that was answered milestones ago — but the cross-cutting one:
**with every kind in one document, does the file as a whole hold intent and
only intent?**

## SCHEMA CONTRACT

No new file format, no new top-level section, no schema version bump. Drawing
objects are object KINDS inside the envelope that was already there:

```text
{ "id": <ObjectId>, "type": "sheet"|"view"|"dimension"|"annotation",
  "name": <string>, "data": { ... } }
```

which is what `ADR-010` requires — drawings live in the Document.

The format is strict in both directions, and that is pre-existing policy this
milestone follows rather than changes:

```text
unknown FIELD    refused (JsonReader::requireObject takes an allow-list)
unknown VERSION  refused outright; there is no forward compatibility
unknown TYPE     refused
```

## CANONICAL VS DERIVED INVENTORY

Taken from the serializers, key by key.

**Sheet** — `format`, `orientation`, `margins`, `scale` (the exact pair, never
the quotient), `convention` (only when it is not first angle), `title_block`
(only when non-empty), and `width`/`height` **only for a Custom format**.

> A standard sheet's size is NOT written: it is derived from the format, so
> the file cannot disagree with ISO 216. Also absent: the usable region, the
> border geometry, the sheet number and the sheet count.

**View** — `sheet`, `kind`, `subject`, `source`, `orientation`, `parent`,
`direction`, `spacing`, `section`, `hatch`, `detail`, `auxiliary`,
`hidden_line`, `scale` (only when the view overrides its sheet's), `x`, `y`.

> Absent: every projected curve, the HLR visible and hidden sets, silhouettes,
> section intersection curves, the projected bounds, and the merged/suppressed
> counts. A projected view stores no `x`/`y` at all, because its placement is
> derived from its parent (`ADR-018`).

**Dimension** — `view`, `type`, `from`, `to`, `ordinate`, `format`,
`tolerance`, `x`, `y`.

> Absent: the measured value, the formatted text, extension lines, arrowheads
> and text extents. A tolerance carrying an ISO 286 fit stores the
> **designation**, so H7 resolves through the standard on every call and is
> 15 µm at 8 mm and 35 µm at 100 mm.

**Annotation** (which is also the BOM table and the balloon, `ADR-022`) —
`view`, `type`, `target`, `text`, `height`, `extension`, `arm_length`,
`finish`, `frame`, `table`, `x`, `y`.

> `frame` carries the characteristic, the zone, the tolerance and the datums
> **as an ordered array of single capitals**, so `A|B|C` cannot come back as
> `B|A|C`. `table` carries only LAYOUT — `row_height`, `item_width`,
> `part_width`, `quantity_width`, `header` — which is how the table is drawn,
> never what it says. Absent: rows, quantities, item numbers, the grouped
> occurrence list, leader geometry and every scene primitive.

**Assembly state a drawing depends on** — a component's `placement` is intent
and is in the file; the solved transform is derived (`ADR-005`) and is not.

## TESTS

**18 new test cases**, all in `tests/drawing/DrawingPersistenceTests.cpp`.

```text
[persist][p14]   994 assertions in  18 test cases
```

None of them repeats a per-type test. Each needs the whole file, or a
property no single type can have on its own.

### The fixture

One document carrying **every P14 object kind at once**: two sheets; a base
view, a projected view, a section and a detail; an assembly view over two
occurrences of one part; a dimension whose tolerance is an ISO 286 H7 fit; a
datum feature symbol; a feature-control frame citing A, B and C in order; a
note; a BOM table; and two balloons on two different occurrences.

That is the point. A per-type test can say "a dimension carries no measured
value". Only this can say "**this file contains no derived geometry at all**".

### The mapping to the checklist

| `TODO.md` item | Test |
| --- | --- |
| Define canonical drawing schema | the inventory above, taken from the serializers key by key |
| Persist sheets / views / dimensions / annotations | `AWholeDrawingRoundTripsWithEveryIdAndEveryReference` |
| Persist tolerances / GD&T | the same, asserting H7 survives as a DESIGNATION and `A|B|C` in order |
| Persist BOM / balloon intent | the same, asserting two balloons name their own occurrences |
| Persist stable references | `EveryPersistedReferenceIsSemanticAndNeverPositional` |
| Keep generated geometry derived | `TheFileHoldsNoDerivedDrawingGeometryAnywhere`, `SolvedAssemblyTransformsAreNotInTheFile`, `TheFileCarriesNoCommandHistory`, `SavingAfterAModelEditLeavesNoStaleDrawingInTheFile` |
| Validate malformed-file rejection | `MalformedDrawingFilesAreRefusedAndNothingIsPublished` (10 cases), `MalformedDrawingSemanticsAreRefusedNotJustMalformedSyntax` (9), `ADuplicateObjectIdIsRefused` |
| Validate deterministic serialization | `TheSameDrawingSerializesToTheSameBytesEveryTime`, `SaveThenLoadThenSaveIsByteIdentical`, `NumbersAreWrittenTheSameWhateverTheLocale` |
| Validate full round trip | `AWholeDrawingRoundTrips...`, `ALoadedDrawingRegeneratesAndDrawsWhatItDrewBefore` |

Five more carry things the checklist does not name:
`AReferenceSavedWhileUnresolvedStaysUnresolvedAndThenRecovers`,
`AViewCycleInAFileLoadsAndIsThenDiagnosedByRegeneration`,
`AFailedLoadLeavesTheLiveDocumentUntouched`,
`AFailedSaveDoesNotDestroyThePreviousFile` and
`EveryCommittedModelStillLoads`.

## INDEPENDENT VALIDATION

**Round-trip equality is byte equality of the document's own serialization**,
compared after the saved document has been destroyed and a new one built from
the file. Not "the same fields where we thought to look": the same bytes. Two
drawings that render alike but differ in one ObjectId, one reference or one
datum's position fail it.

**Identity is compared as a set, not spot-checked.** Every ObjectId in the
document before the save is compared with every ObjectId after, and the object
count with it, so an ID that changed or an object that was silently dropped
cannot pass by being in a place nobody asserted.

**Derived state is searched for as JSON KEYS, not as substrings**, and that
distinction is the test working rather than being lucky. The first version
searched bare substrings and flagged `row` and `quantity` — which turned out
to be inside `row_height` and `quantity_width`, the BOM table's LAYOUT
settings, which are drawing intent and belong in the file. Searching for
`"row"` and `"quantity"` as keys finds nothing, and the layout keys are
asserted PRESENT, so the test now distinguishes the two rather than confusing
them.

**The regeneration after load is the production path**, `assembly::
registerHandlers` + `drawing::registerHandlers` + `regenerateAll`, with no
persistence-only route. Every drawing object is then required to be
`Regenerated` — so an object that loaded but was never validated would fail
here, which is exactly what `P14-REGEN-001` made possible.

**Determinism is tested three ways**, because each catches something the
others do not: five serializations of one document (catches anything
order-dependent within a run), a serialization of a deep CLONE (catches
anything address-dependent, since a clone rebuilds every container), and
save → load → save → load → save compared byte for byte (catches normalization
drift that needs a generation to appear).

**The locale test changes the C locale for real and says so if it cannot.**
Where a comma-decimal locale is installed it is applied, `printf` is shown
writing `1,25`, and the serialization is required to be unchanged. Where no
such locale exists the test `WARN`s rather than passing quietly — a check that
silently does nothing is worse than one that is absent.

## THREE DECISIONS RECORDED

**1. No schema version bump.** `kDocumentFormatVersion` is 1, and the loader
refuses any other version outright — there is no forward compatibility by
design. Bumping it for P14 would therefore have broken **all 32 committed
models** and required a migration, in exchange for nothing: drawing objects
are new object kinds in an extensible `objects` array, and a pre-P14 file
loads because it simply contains none of them. Proven, not assumed: every
committed model still loads, and none has acquired a sheet, view, dimension
or annotation.

**2. A view cycle in a file LOADS, and is then diagnosed.** A view naming
itself as its parent is structurally well formed and is not refused by the
reader. It cannot be: document-level validation cannot run per object during
a load, because objects are read one at a time and a view whose parent appears
LATER in the file would be refused for a forward reference that is perfectly
legal. `createView()` can check against the document; the reader cannot.

The brief allows "rejected **or** diagnosed", and the diagnosis is a good one
— better than expected. It comes from the **dependency graph**, not from a
drawing-specific walk: `View::dependencies()` names the parent, so a view that
names itself is an ordinary cycle and the regenerator's existing cycle
detection reports `dependency cycle: Top`. That only reaches drawing objects
because `P14-REGEN-001` put them in the graph properly. The view also refuses
to draw. Both halves are pinned by a test.

**3. Unresolved is not malformed, through the file.** A balloon whose
occurrence the active configuration suppresses is valid intent whose target is
not here now. It saves, loads, is still unresolved, and recovers when the
configuration comes back — naming the same occurrence throughout, with an
identical sibling present for a rebinding implementation to take instead.

## SAVE AND LOAD ATOMICITY

Both are met by infrastructure that already existed, and the audit's job was
to confirm it rather than to add any.

**Save** goes through `detail::writeFileAtomically`: write to `<path>.tmp`,
then `rename`, with the temporary removed on every failure path. So a failed
save cannot truncate the previous valid file — tested by saving into a
directory that does not exist and requiring the established file to be
byte-for-byte the size it was, and still loadable.

This is also the exact call whose `rename` produces the intermittent OneDrive
"cannot replace ... Permission denied" recorded in `TODO.md`. That is the
distinction the brief asks for: a **filesystem** failure, correctly reported
as a failure rather than swallowed. Serialization correctness is a separate
thing and is not implicated.

**Load** is parse-then-publish by API shape: `loadDocument` builds a new
`Document` and returns it, so a malformed file cannot half-populate a live
one. Tested with a live document carrying a full drawing, a failed load, and
the live document's serialization compared before and after.

## FULL REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **2115 / 2115** (269.55 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **2115 / 2115** (237.26 s) |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **2115 / 2115** (303.51 s) |

```text
qualification finished Thu 24/09/2026 14:20:27.81, 0 stage(s) failed
```

Baseline was 2097 ctest entries; this adds **exactly 18**, the number of new
test cases. Nothing was written and left undiscovered.

### The harness

`verify-harness.cmd` was run before the qualification and passes: pointed at a
preset that does not exist it reports `QUALIFICATION FAILED: 3 stage(s)
failed` and gives `qualify.cmd` exit 3.

### The qualified tree is the committed tree — and proves the central claim

Tree IDs from a scratch index, before the first build, after the last test run
and again before the commit — identical in all three:

```text
apps              b32ce14e7be30b1c05432f740c2d25607be73b39
include           494a26546a2feb0c36d34f14a50ccaddcc8ed430
src               ecb006508b42832b6ae347aa4ffe82b691e57b2a
tests             e5a27f7fa3eae1a9f94852b225099e5766753d0e
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

**`include` and `src` are byte-identical to the trees `P14-CMD-001` qualified**
(`494a2654…` and `ecb00650…`). Only `tests` differs. That is not a claim in
prose that no production code changed — it is the same hash, independently
computed, before and after. `apps`, `examples`, `cmake`, `CMakeLists.txt` and
`CMakePresets.json` go back unchanged to `P14-REGEN-001`.

A full debug run was also made on the same tree before the freeze
(**2115/2115**).

## DETERMINISM

```text
repeat release  exit 0   2114 / 2114, each test run five times (1137.03 s)
repeat debug    exit 0   2114 / 2114, each test run five times (1248.24 s)
```

The repeat selection is the widest of any P14 milestone, because the subject
is the file format itself: everything that reads or writes a document is in
scope, not only what draws.

Serialization determinism is asserted three ways, each catching what the
others cannot — five serializations of one document, one of a deep CLONE (a
clone rebuilds every container, so an address-dependent order would show), and
save → load → save → load → save compared byte for byte. The locale check
changes the C locale for real, shows `printf` writing `1,25`, and requires the
output to be unchanged.

**The OneDrive filesystem fault did not recur**, in either repeat preset — the
third milestone running. As before that does not close it: it is intermittent,
and `TODO.md` still carries the decision as due.

## ADVERSARIAL REVIEW

Run against the brief's twenty-one questions. **No production defect was
found. Two defects were found in my own tests and fixed, and three
architectural answers were recorded rather than assumed.**

| Question | Answer |
| --- | --- |
| Can projection geometry be serialized? | No — searched for as JSON keys across a file holding every kind |
| Can a measured value become authoritative? | No — and the H7 fit's resolved deviations are searched for too, not just the nominal |
| Can a BOM quantity be frozen in the file? | No — only `quantity_width`, a column width |
| Can a balloon store an item number? | No — it stores the occurrence; two balloons on two occurrences are asserted distinct after the load |
| Can save/load change ObjectIds? | No — every id compared as a set, plus the object count |
| Can parent ViewIds change? | No — all three derived views' parents asserted |
| Can datum order change? | No — `A|B|C` asserted element by element |
| Can references degrade to indices in JSON? | No — fifteen forbidden spellings searched |
| Can unresolved references silently resolve after load? | No — saved while unresolved, still unresolved after loading, recovers to the SAME occurrence with an identical sibling present |
| Can malformed input partially publish? | No — `loadDocument` returns a new document; asserted against a live one |
| Can unknown enums default silently? | No — format, orientation, view kind, dimension type, annotation type, characteristic and zone all refused |
| Can NaN/infinity enter the file? | No — `1e999` is valid JSON syntax and is refused by the validators |
| Can locale change numeric output? | No — tested under a comma-decimal locale |
| Can map iteration change bytes? | No — a deep clone serializes identically |
| Can Debug and Release differ? | No — the byte comparisons run in all three presets |
| Can load trust a stale cache? | There is no cache to trust |
| Can solved transforms be persisted? | No — asserted while the assembly HAS solved |
| Can command history leak into the file? | No — asserted with a non-empty history |
| Can duplicate IDs be accepted? | No |
| Can a failed save truncate the previous file? | No — write-to-temp then rename |
| Can save → load → save drift? | No — three generations compared |

### Defects found in my own work, and fixed

**The derived-state search was the wrong instrument.** It used bare substrings
and flagged `row` and `quantity` — which are inside `row_height` and
`quantity_width`, the BOM table's LAYOUT settings, and therefore intent. Fixed
to search JSON keys, and to assert those layout keys are PRESENT, so the test
now distinguishes a table's appearance from a table's contents instead of
confusing them.

**Two malformed cases were anchored on strings that were not in the file** —
a sheet format written `"A3"` rather than `"a3"`, and datums written as an
ordered ARRAY of single capitals rather than one string. Both anchors now
locate the JSON structurally, by its brackets, instead of depending on the
pretty-printer's indentation. The datum finding is a good one: the array form
is exactly what makes the order explicit and survivable.

### Not a defect, recorded as a decision

The view-cycle case, in full, is under THREE DECISIONS RECORDED above. It
loads by necessity and is diagnosed by the dependency graph.

## KNOWN LIMITATIONS

**The format has no forward compatibility, by design.** An unknown version is
refused outright and an unknown field is refused. A file written by a newer
BetterCAD will not open in an older one, and there is no "ignore what you do
not understand" path. That is pre-existing policy, followed rather than
changed here; whether it should soften is a decision nobody has authorized.

**A document-level check cannot run during a load.** Objects are read one at a
time, so a reference that is valid only once the whole document exists — a
view's parent appearing later in the file — cannot be checked by the reader.
Such files load and are diagnosed at regeneration. See decision 2.

**Command history is not persisted**, and `P14-PERSIST-001` did not make it
so: the milestone is about drawing intent. Whether undo should survive a
reload is a separate, unauthorized decision.

**Carried from `P14-STREF-001`, unchanged:** "no silent rebinding" is still
not met for chamfer faces, named by position in the chamfer's edge list. That
milestone stays open, and the persisted form of such a reference is exactly as
stable as the reference itself — which is the point of that open gate.

**Carried from `P14-REGEN-001`, unchanged:** `RegenerationReport::succeeded()`
can be true for a document whose assembly did not solve.

## RESULT

```text
TASK:            P14-PERSIST-001 -- Drawing persistence
IMPLEMENTATION:  NONE. No production code was changed. Drawing persistence
                 was already correct, built by the milestones that introduced
                 each object kind; this is the system-wide audit that no
                 per-type milestone could do, and it found the contract met.
                 Proven by tree ID: include and src are byte-identical to the
                 trees P14-CMD-001 qualified
TESTS:           18 new; 2115/2115 in debug, release and debug-shared, each
                 from clean; 2114/2114 five times over in both repeat presets
VALIDATION:      round-trip equality is BYTE equality of the document's own
                 serialization after the original was destroyed; identity is
                 compared as a set, not spot-checked; regeneration after load
                 goes through the production path and every drawing object is
                 required to be Regenerated
ADVERSARIAL:     0 production defects; 2 defects in my own tests, fixed;
                 3 architectural answers recorded rather than assumed
WARNINGS:        0 in all three builds
DETERMINISM:     five serializations, a deep clone, and three save/load
                 generations, all byte-identical; unchanged under a
                 comma-decimal locale; repeat gate clean in both presets
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-PERSIST-001 marked [x], all 15 items
NEXT:            P14-CLI-001 -- Headless Drawing Workflows. P14-STREF-001
                 remains OPEN on its chamfer gap.
```

## REVISION

Two revisions, both to the tests and neither to the code: the derived-state
search was narrowed from substrings to JSON keys after it flagged two table
layout settings, and two malformed-file anchors were rewritten to locate the
JSON structurally rather than to match the pretty-printer's indentation. No
production behaviour changed at any point in this milestone.

## FILES

```text
qualification/qualify.cmd               the harness
qualification/verify-harness.cmd        its exit-code regression; run and passing
qualification/run-qualification.cmd     the entry point and its repeat selection
qualification/qualification-times.txt   every stage, its exit code, and the tree IDs
qualification/build-*.log               three presets, 0 warnings each
qualification/rebuild-*.log             the fresh-binary proof
qualification/ctest-*.log               2115/2115 in each preset
qualification/ctest-repeat-*.log        2114/2114 five times over, debug and release
```
