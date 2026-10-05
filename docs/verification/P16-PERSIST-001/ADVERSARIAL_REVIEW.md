# P16-PERSIST-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the milestone before marking it complete
METHOD:   the brief's 27 attack questions against the final diff, then 12
          mutations of the serializer
```

## 1. The attack questions

| # | Attack | Answer |
| --- | --- | --- |
| 1 | Can generated Tet4 arrays enter the canonical save? | **No.** `MeshControlDefinition` has no member for them, and `VolumeMesh` cannot be default-constructed (ADR-030), so nothing in the serializer could reach one. Checked against the written bytes with a mesh in memory: `..._NoGeneratedMeshReachesTheFile`. |
| 2 | Can a fine mesh make the document materially larger? | **No**, measured: 745 → 1871 elements changes the file by **2 bytes**. `..._FileSizeDoesNotFollowMeshDensity`. |
| 3 | Can a `NodeId` be persisted as boundary identity? | **No.** A set holds `std::vector<FaceName>`; there is no field a node could occupy, and `NodeId` is mesh-local and not a `bettercad::Id`. |
| 4 | Can an `ElementId` be stored as region intent? | **No**, same reason. `BoundaryFacetSet` is the derived form, returned by value and never stored. |
| 5 | Can a Netgen surface marker be serialized canonically? | **No.** Nothing in `src/io/` includes a backend header, and `architecture.layering` fails the build if one escapes `src/meshing/<backend>/`. Asserted against the bytes too: `..._TheFileNeedsNoMeshingBackendToBeInterpreted` rejects `netgen`, `nglib`, `maxh`, `minh`, `grading`, `Ng_`. |
| 6 | Can a local control's ID change after load? | **There is no such ID.** A local control is keyed by its `FaceName` (P16-SIZE-001), so nothing is allocated on load. `..._LocalControlsKeepTheirFaceAndSize`. |
| 7 | Can a boundary-set ID change after load? | **No**, restored as written and asserted by value. Mutation **M4** adds one to it. |
| 8 | Can duplicate IDs silently overwrite a control? | **No.** Two sets sharing an identity is `AlreadyExists` from `MeshControl::validate`; two local controls on one face is `InvalidArgument` from the sizing validator; a repeated quality metric is refused by name — which is why `limits` is an array and not an object. |
| 9 | Can 10 mm load as 10 m? | **No.** Five quantities round-trip exactly, and the file is pinned directly: `10 mm` → `"target_size": 0.01`. Mutations **M2** and **M3** are a factor of 1000 in each direction. |
| 10 | Can NaN pass parsing and bypass validation? | **No** — and not for the reason expected. See finding **B1**: nlohmann refuses `NaN`, `Infinity` and `1e999` as invalid JSON, so the core's check is a second line of defence, not the only one. |
| 11 | Can a negative target size load? | **No**, `InvalidArgument` from the core. |
| 12 | Can a missing `GeometryReference` silently drop a control? | **No.** `face` is a required field; a local entry without one is "missing required field". Mutation **M10** skips an unreadable face instead of refusing. |
| 13 | Can an unresolved reference silently bind to the nearest face? | **No.** It is kept verbatim and asserted to still name the absent entity — `..._AMalformedReferenceIsNotTheSameAsAnUnresolvedOne`. |
| 14 | Can stale resolved facet IDs be restored after load? | **No.** No facet is written. The boundary-set test resolves against the mesh generated *after* the load and reports its facet count rather than comparing with a pre-save number. |
| 15 | Can a topology-changing edit become falsely "resolved" from saved mesh data? | **No.** Resolution is computed from intent plus current geometry on every request; there is nothing saved for it to be computed from. |
| 16 | Can save order depend on unordered map iteration? | **No.** Every collection is written through `orderedLocalSizing()` / `orderedBoundarySets()`, and `limits` is a `std::map` keyed by the metric enum. Mutations **M5** and **M6** substitute the stored vectors. |
| 17 | Can save/load/save produce different canonical files? | **No**, byte-identical, three passes. `..._SaveLoadSaveIsByteIdentical`. |
| 18 | Can a pre-P16 document fail because the meshing section is absent? | **No.** There is no meshing *section* — a control is an entry in `objects` — so an older file simply has none. A version-1 file loads too. `..._APreP16DocumentLoadsAndSimplyHasNoControls`. |
| 19 | Can an old schema default adopt a new modern default and change meaning? | **Not for the deflections**, which are always written explicitly rather than reconstructed. For `target_size`, absence is itself the canonical state — "BetterCAD's scale-relative default" — and is preserved as absence, so there is no number to drift. |
| 20 | Can a malformed load partially mutate an existing document? | **No**, structurally: `documentFromJson` builds a new `Document`. Verified with a fingerprint and a revision across five bad inputs. |
| 21 | Can persisted state require Netgen to load? | **No.** `io` links `BetterCAD::meshing`, which does not drag a backend into the parse path, and the hand-written reference file is interpreted field by field with no mesher involved. |
| 22 | Can command undo fail after load because IDs were not restored? | **No.** `..._CommandsUndoAndRedoExactlyAfterALoad` runs a global edit, a local edit and a removal against references that came out of the file, and undoes back to the file's own fingerprint. |
| 23 | Can a control created after a load collide with a loaded ID? | **No.** `last_allocated_id` is persisted and the loader refuses a value below an ID in use. Verified through the meshing path. |
| 24 | Can the file contain a pointer or process-specific identity? | **No.** Every value is an integer ID, a double, a string or a nested object of those. Proved most strongly by the fresh-process CLI tests: nothing that process reads was ever in another one. |
| 25 | Can cached quality or mapping data be mistaken for canonical state? | **No.** Neither is written. The threshold **policy** persists; the report is recomputed and `qualityDescribesCurrentPolicy()` is asserted after the load. |
| 26 | Can a corrupt optional cache prevent loading valid intent? | **N/A — there is no cache.** NOT IMPLEMENTED, deliberately: no measured performance requirement, and the brief forbids adding one without. Nothing in the tree caches a mesh to disk. |
| 27 | Can changing mesh density change the canonical saved file? | **No** — question 2's measurement. |

## 2. Findings

### B1 — a non-finite value never reaches the core, and my audit said it would (audit defect, corrected)

The audit, written from reading `readNumber`, claimed: *"A hand-written `1e999`
parses to infinity and passes. The guard is the core validator, not the
parser."*

**Measured, that is wrong.** nlohmann throws `out_of_range` for a number that
overflows a double and `parseJson` maps it to `ParseError`; `NaN` and
`Infinity` are not JSON at all. The first version of the test asserted
`InvalidArgument` from the core and failed with `4 == 0`.

The correction mattered more than the fact. The tempting fix was to relax the
expectation to "some error" — which would have passed while hiding *which
layer* refuses, and would have left the audit's claim standing. Instead the
test now asserts `ParseError` with `invalid JSON` for all five forms, the core
guard is shown separately, and the audit records the measurement and says it
replaced an assumption.

### B2 — the P16-CMD-001 boundary test failed on purpose, and was replaced where it was marked

`MeshControl_CannotYetBeSavedAndSaysSo` asserted that a control could *not* be
saved, and said in its own comment: *"When P16-PERSIST-001 lands, this test
fails and is replaced by its round trip. That is the point of writing it: the
boundary moves on purpose and visibly."*

It failed. It is now `MeshControl_NowSavesAndLoads`, and the replacement
records that the boundary moved rather than leaving someone to wonder why a
save had started working. This is the designed signal working, not a
regression — but it is recorded here because a failing test in an unrelated
file is exactly the thing that gets "fixed" by deletion.

### B3 — a quality threshold matrix that exercised four of eighteen metrics (test defect, fixed)

The first version of `..._EveryQualityMetricRoundTrips` used one pair of
bounds (warning 0.4, failure 0.2) for every metric. Only **four** were
accepted, because bounds must follow each metric's direction — for
lower-is-better the failure bound must be the larger — and eleven metrics are
`ContextOnly` and take no bound at all. The guard that was supposed to catch a
missing file key was silently covering a fifth of the enum.

Fixed: direction-appropriate bounds, an exact count of the 7 classifiable
metrics, each asserted to serialize under a key that is not the `"unknown"`
fallback, and the other 11 asserted to be refused by the core so that a
`ContextOnly` threshold is shown to be unpersistable rather than assumed so.

### B4 — a block cannot demonstrate mesh growth, for the third time in this project (test defect, fixed)

The file-size test first used the 30 x 20 x 10 mm block and compared 10 mm
against 1.2 mm: both gave **24 elements**. A planar boundary is exactly
representable, so a volume target has nowhere to act — recorded by
P16-SIZE-001, hit again by P16-CMD-001's payload test, and hit a third time
here. Fixed with a cylinder, and the reason is written into the fixture so the
next person does not repeat it.

### B5 — two API signatures were guessed and were wrong (test defects, fixed)

`resolveBoundarySet(map, mesh, set)` is really
`resolveBoundarySet(set, map)`; `ResolvedBoundarySet::facets` is really
`mapping.facets`; `MeshQualityReport::invalidCount` is really
`invalidElements`. All three failed to compile, which is the harmless way for
a wrong guess to go — noted because the fix was to read the headers rather
than to keep guessing.

### B6 — `nameIn`'s "unknown" fallback is a silent failure mode (accepted, guarded by test)

The enum-table helper returns `"unknown"` for a value missing from its table.
A metric added to `QualityMetric` without a file key would therefore serialize
as `"unknown"` and fail to load — silently on write, loudly on read.

A table-size `static_assert` cannot catch a *wrong* entry, and the helper is
the codebase's established idiom (duplicated per translation unit in
`DatumJson.cpp` and `BooleanJson.cpp`), so changing it would be refactoring
shared infrastructure during a feature milestone. The guard is instead a test
that round-trips every threshold-capable metric and asserts the file never
contains `"unknown"`.

### B7 — `io` gained a dependency on `meshing`, which was designed in (no defect)

Adding `BetterCAD::meshing` to `io`'s `PRIVATE_LINK` is a new module edge, so
it was checked rather than assumed. P16-ARCH-001's layering table already says
it: *"meshing sits above features ... and below io, which must serialize its
controls."* `architecture.layering` passes: 439 files, 0 violations.

### B9 — the mutation harness left a mutation applied to a production file (process defect, caught)

The first mutation run was orphaned when its session ended. `mutate.sh` died
**without its EXIT trap firing**, which left mutation M3 applied to
`src/io/json/MeshControlJson.cpp`:

```cpp
.targetSize = Length::fromSi(*size / 1000.0)   // a deliberate unit defect
```

together with a stale lock whose pid no longer existed.

**What makes this dangerous is that git could not see it.** The file is new,
therefore untracked, so `git status` shows `??` whatever the contents and
`git diff` shows nothing at all. Every routine cleanliness check would have
passed with a deliberate factor-of-1000 defect in the tree.

Caught by diffing the working file against the harness's own `pristine/`
snapshot, restored, rebuilt, and re-verified at 108 cases / 4061 assertions.
The remaining mutations were then re-run **detached**, so a session ending
could not orphan them again, and the driver puts its restore in a `finally`
block rather than a trap.

The project's own note on this harness said the opposite failure mode — that
it survives a stop and reverts your edits. Both are real and they are
opposites; the note now says so.

### B8 — the schema is anchored by a hand-written file (strengthening, not a defect)

`examples/models/meshed_plate.bcad` is written by hand, following
`plate.bcad`'s precedent. It is the backend-independence evidence that cannot
be argued with — a person wrote every field with no mesher present — and it
makes an incompatible future change to the persisted form break a test
deliberately rather than silently.

## 3. Mutation testing

Twelve mutations of `MeshControlJson.cpp`, each a single substitution, judged
by whether `[persist]` fails.

| # | Mutation | Must be caught by |
| --- | --- | --- |
| M1 | a local control's face reference is dropped on write | the local round trip |
| M2 | lengths written in millimetres | the unit matrix and the `0.01` assertion |
| M3 | a local size read with the wrong scale | the unit matrix |
| M4 | a boundary set gets a fresh identity on load | the set round trip |
| M5 | local controls written in stored order | the edit-order determinism test |
| M6 | boundary sets written in stored order | the same |
| M7 | reading skips the core's validation | every malformed-input case |
| M8 | a repeated metric silently keeps the last | the duplicate-metric case |
| M9 | an absent target size written as zero | the absence round trip |
| M10 | an unreadable face is skipped, not refused | the malformed-reference case |
| M11 | the linear deflection written as the default | the deflection round trip |
| M12 | the threshold policy dropped on write | the metric round trip |

```text
12 killed, every one BY A TEST     0 survivors
```

| # | Verdict | Failing assertions |
| --- | --- | --- |
| M1 | killed | 6 |
| M2 | killed | 14 |
| M3 | killed | 16 |
| M4 | killed | 7 |
| M5 | killed | 1 |
| M6 | killed | 1 |
| M7 | killed | 29 |
| M8 | killed | 1 |
| M9 | killed | 5 |
| M10 | killed | 2 |
| M11 | killed | 2 |
| M12 | killed | 9 |

**M11's first form was killed only by the COMPILER, which is weaker
evidence, and was rewritten.** Writing both deflections as defaults left the
`surface` parameter unused, so `-Werror=unused-parameter` rejected it before
any test ran — the mutation was never actually evaluated. Rewritten to write
only the *linear* deflection as a default and keep the parameter used, it
compiles and dies on two assertions in
`..._SurfaceDeflectionsRoundTripExactly`. Recorded because a compiler kill
reads identically to a test kill in a results file and is not the same thing.

**M5 and M6 each die on a single assertion**, which is worth noting rather
than glossing: the edit-order determinism test is the only thing standing
between canonical ordering and a file whose contents depend on the order
somebody happened to click. One assertion is enough, but it is exactly one.

Results and the harness are in
[qualification/mutation/](qualification/mutation/). The first run of the
harness was orphaned by a session ending and left mutation M3 **applied** to
the production file with a stale lock — see finding **B9**.
