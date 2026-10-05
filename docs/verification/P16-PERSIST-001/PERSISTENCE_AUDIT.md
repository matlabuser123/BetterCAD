# P16-PERSIST-001 — persistence architecture audit

```text
SUBJECT:  what the document format already does, what P16 must add, and what
          it must NOT add because the format already settles it
DATE:     2026-10-05
AT:       8ae2999
```

Written before any production code. Most of this milestone's questions turn
out to be already answered — by the format's own documented rules, by
P15-PERSIST-001's audit, and by P16-ARCH-001's layering decision. What is left
is genuinely P16's own mapping.

## 1. The format

```text
include/bettercad/io/DocumentFile.hpp   kDocumentFormat, the version constants
src/io/json/DocumentJson.cpp            the root, the header, the dispatch
src/io/json/JsonReader.{hpp,cpp}        the parse helpers and the strictness
src/io/json/ObjectJson.hpp              one declaration per object kind
src/io/json/<Kind>Json.cpp              one mapping per kind
src/io/FileIo.cpp                       writeFileAtomically
```

| Question | Answer |
| --- | --- |
| Persisted subsystem | one native JSON document, `.bcad`, `"format": "bettercad-document"` |
| Schema location | `src/io/json/`, one `*Json.cpp` per object kind, dispatched by `dynamic_cast` |
| Versioning model | a single integer with a **read range**: `kOldestReadableDocumentVersion` 1 to `kDocumentFormatVersion` 2; writes only the newest |
| Deterministic? | **conditionally** — `Json` is `nlohmann::ordered_json`, so key order is *insertion* order, not sorted |
| Strict or lenient parser? | **strict**: `requireObject` takes an allowed-key list and rejects anything else as `unknown field` at its path |
| Migration mechanism | in the readers, per version, keyed on the version number |
| Reusable for P16? | **yes, entirely** — P16 adds one object kind to the existing pattern |

## 2. Five things the format already settles

**No version bump.** `DocumentFile.hpp` states the rule itself: *"adding a
kind, an object type or an optional field needs no bump. Changing what an
existing field means needs one."* P16 adds an object type. The version stays
**2**, exactly as P15 added `material` without a bump.

**Backward and forward compatibility are defined.** A new reader on an old
file loads it and simply finds no meshing controls — there is no meshing
*section* to be absent, because a `MeshControl` is an entry in the existing
`objects` array. An old reader on a new file **refuses**: `objectFromJson`
ends in `unknown object type '<name>'`, a parse error naming it, never a
silent skip.

**Load atomicity is structural, not a precaution.** `documentFromJson`
returns `Result<Document>` and builds a **new** document, so a failed load
cannot reach an existing one. There is no partially-mutated state to prevent.

**Duplicate object IDs and the allocator are already handled.**
`insertObject` refuses an ID in use with `AlreadyExists`, and
`last_allocated_id` is persisted in the header and refused if below an ID in
use — so a control created after a load cannot collide with a loaded one.

**Quantities are bare SI doubles.** Lengths in metres, angles in radians,
written with `.si()`. There is no numeric+unit form and no display-unit intent
in the format, so `10 mm` persists as `0.01` and round-trips as the same
`Length`. P16 must not invent a mesh-only unit representation.

## 3. What P16 must reuse rather than write

**`faceNameToJson` / `faceNameFromJson` already exist** (`DatumJson.cpp`), and
they are complete: feature ID plus every field of the selector — `role` as a
**string** from a table, `entity`, `along`, `along_sketch`, `chamfer_edge`,
and `copies` — with unknown keys rejected and the selector validated on
reading. They even carry the ADR-024 legacy conversion.

This is the milestone's single most important reuse. The brief asks for
canonical `GeometryReference` persistence; the answer is that BetterCAD's
canonical face reference already has a qualified serialization used by datums,
drafts, shells, dimensions and annotations. P16 writes `faceNameToJson` and
nothing of its own.

**`MeshControl::create` is the validator.** P15's material mapping routes
through `Material::create` so that "the file goes through exactly the
validation" the API does. `MeshControl::create` already delegates to
`validate(MeshSizingControls)`, `validate(QualityThresholds)` and
`validate(NamedBoundarySet)`, and already refuses two boundary sets sharing an
identity. So the parser validates **syntax, type and required fields**, and
the core validates **positive sizes, finiteness, duplicate face controls,
duplicate set identities and every other canonical invariant** — with no
second copy of any rule.

That one decision answers the brief's §28, §29, §66 and §74 together, and it
is why there will be no sizing arithmetic anywhere in `MeshControlJson.cpp`.

## 4. The three hazards this format carries

**`ordered_json` means insertion order is the file's order.** Determinism is
not free. Every object must be built by inserting keys in a fixed literal
order, and **no collection may be written by iterating an unordered
container**. P16-CMD-001 already built what this needs:
`MeshControl::orderedLocalSizing()` sorts by `FaceName` and
`orderedBoundarySets()` by `BoundarySetId`, and `QualityThresholds::limits` is
a `std::map` keyed by the metric enum. Writing through those is deterministic
regardless of the order the edits happened in.

A consequence worth stating: the **stored vector order** of local controls is
not preserved across a save. It is not semantic — P16-SIZE-001 declares it
meaningless and P16-CMD-001's fingerprint ignores it — and writing the
canonical order instead is what makes save/load/save converge to identical
bytes. Two documents with the same meshing intent get the same file, which is
the stronger property.

**`kTypeName` must not be referenced from the dispatch.** `DocumentJson.cpp`
carries the warning twice: *"binding a reference to a dll-imported constexpr
static does not link in a shared build (found by the debug-shared preset)"*.
So the dispatch compares against the string literal `"mesh-control"`, and
`MeshControlJson.cpp` carries a `static_assert` that the literal and
`MeshControl::kTypeName` agree. This is the same family of defect as
P16-CMD-001's finding A10, which also surfaced only in the shared preset.

**`readNumber` does not itself reject non-finite values** — it checks
`is_number()` and returns `get<double>()`. The expectation from reading it was
that a hand-written `1e999` would arrive as an infinity and have to be caught
by the core validator.

**Measured, that is wrong, and in the reassuring direction.** nlohmann's
parser throws `out_of_range` for a number that overflows a double, and
`parseJson` already maps that to `ParseError` — its comment says so. The bare
literals `NaN`, `Infinity` and `-Infinity` are not JSON at all and fail the
same way. So the format cannot carry a non-finite number by any of these
routes, and the core's finiteness check is a **second** line of defence rather
than the only one.

Recorded as measured rather than as first assumed, because the difference
matters: a test written on the original belief would have asserted
`InvalidArgument` from the core and failed, and "fixing" it by loosening the
expectation would have hidden which layer actually refuses. The test asserts
`ParseError` with `invalid JSON` for all five forms, and the core's guard is
shown separately by handing `MeshControl::create` a non-finite `Length`
directly.

Routing through `MeshControl::create` remains a correctness requirement for
everything else the parser cannot judge: zero and negative sizes, two controls
on one face, two sets sharing an identity.

## 5. Scope

```text
IN     a MeshControlJson.cpp mapping: the body, surface controls, global and
       local sizing, the quality threshold policy, and named boundary sets
IN     registration in both dispatches, with the literal and a static_assert
IN     io gains a PRIVATE_LINK on BetterCAD::meshing -- which P16-ARCH-001
       designed in: the layering table says meshing sits "below io, which must
       serialize its controls"
IN     the round-trip, malformed-input, determinism and regenerate-after-load
       tests, and a fresh-process round trip

OUT    a generated-mesh cache. NOT IMPLEMENTED, and deliberately: there is no
       measured performance requirement, and the brief forbids introducing one
       without it. Nothing in the tree caches a mesh to disk today.
OUT    a version bump, a meshing-specific schema version, or a sidecar file
OUT    any new validation logic -- it would be a second copy of P16-SIZE's
OUT    command-history persistence: BetterCAD persists no undo stack
```

## 6. The one open design decision

`MeshControlDefinition::quality` is a `std::map<QualityMetric,
QualityThreshold>` over **18 metrics**. Persisting it needs a stable file key
per metric, and P15's mapping is explicit that enumerations are **strings, not
integers** — *"an integer would silently become a different case the day
someone inserts an enumerator in the middle"* — and that file keys must not be
the diagnostic strings, which are display text a later milestone may reword.

So an explicit table, separate from `toString(QualityMetric)`. The hazard is
that `nameIn` returns `"unknown"` for a value missing from the table, which
would serialize silently and fail to load. A table-size `static_assert` cannot
catch a wrong entry, so the guard is a test that round-trips a threshold on
**every one of the 18 metrics** — which fails loudly the day a metric is added
without its key.
