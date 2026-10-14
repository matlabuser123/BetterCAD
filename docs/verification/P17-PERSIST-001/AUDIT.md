# P17-PERSIST-001 — the persistence architecture audit

```text
SUBJECT:  what already exists, what P17 may reuse, and the four questions the
          brief asked me to DECIDE that the project had already decided
RAN:      before any production code was written
```

**The audit is the milestone's main finding, again.** Every one of the brief's
twelve architectural questions already had an answer in the tree, with a
recorded reason. P17 persistence adds **one object type to an existing
format**: no second file, no schema version bump, no new validation framework,
no new reference encoding, no new determinism mechanism.

## The capability table

| Capability | Existing type / API | Where | Reusable for P17? | Decision |
| --- | --- | --- | --- | --- |
| document file format | `.bcad`, transparent JSON, `nlohmann::ordered_json` | `io/DocumentFile.hpp` | yes | reuse unchanged |
| schema version | `kDocumentFormatVersion = 2`, `kOldestReadableDocumentVersion = 1` | same | yes | **no bump** — see below |
| object dispatch | `objectToJson` / `objectFromJson`, keyed on a string literal | `json/DocumentJson.cpp` | yes | add one branch each way |
| strict reading | `requireObject(value, path, allowedKeys)` | `json/JsonReader.hpp` | yes | **unknown key = reject** |
| path-named errors | `parseError`, `atPath`, `childPath`, `indexPath` | same | yes | reuse; every refusal names a path |
| ID reading | `readId`, cap `kMaxFileId = 2^53 - 1` | same | yes | reuse for all three ID types |
| face reference | `faceNameToJson` / `faceNameFromJson` | `json/ObjectJson.hpp` | **yes** | reuse — a second encoding would be a second contract |
| atomic save | temp file + rename | `io/DocumentFile.hpp`, `FileIo.cpp` | yes | reuse; carried limitation unchanged |
| save-failure path | `objectToJson` returns `Result<Json>` | `json/DocumentJson.cpp` | yes | reuse for the one refusal P17 needs |
| material persistence | `materialToJson`, P15 | `json/MaterialJson.cpp` | n/a | **not touched** — P17 stores no material |
| derived-state exclusion | structural: the definition has no derived member | `json/MeshControlJson.cpp` | yes | same argument holds for P17 |
| determinism | insertion-ordered keys, ordered accessors | same | yes | same mechanism |
| golden fixture | `examples/models/meshed_plate.bcad` + a test that reads it | `tests/io/MeshControlFileTests.cpp` | yes | add the P17 sibling |
| test layout | `tests/io/<Thing>FileTests.cpp`, one target | `tests/CMakeLists.txt` | yes | one new file, one line |

## The four questions the project had already answered

### 1. Does the schema version bump? NO.

`DocumentFile.hpp` carries the rule as a 40-line comment (P13-PERSIST-001) and
it is unambiguous: **bump only for a change an existing reader would get
WRONG.** Adding an object type is additive, and both directions already fail
safely without the version doing any work:

```text
new reader, old file   loads. An absent section means "none of those"
old reader, new file   REFUSES, by name: "unknown object type
                       'structural-analysis'" -- never a silent skip
```

The comment makes the point against itself: "the schema has grown a great deal
since this was written ... and the number has correctly never moved." A P17
bump would be the first unnecessary one.

### 2. What happens to unknown fields? They are REJECTED.

`requireObject(value, path, allowedKeys)` requires that *every* key be in the
allow-list. So BetterCAD's policy is strict rejection, not forward-compatible
ignoring, and P17 inherits it rather than inventing a policy. The brief
offered both and said to follow the existing one.

### 3. Where does validation live? In the core — **except for two rules.**

P16's reader ends in `MeshControl::create`, so the file goes through exactly
the API's validation. P17's reader ends in `StructuralAnalysis::create`, which
calls `validate(StructuralAnalysisDefinition)` — and that function **checks the
mesh handle and the solver settings and nothing else.** Measured, not assumed:
it is fourteen lines, and P17-CMD-001's probe C11 measured the same asymmetry
from the other side.

```text
validate(StructuralAnalysisDefinition) checks
    the mesh handle        -> a bad mesh handle is refused by create()
    validate(solver)       -> a bad algorithm or tolerance is refused by create()
but NOT the loads
but NOT the restraints
and NOT duplicate ids
```

So the reader must do three things itself, and each has a precedent:

```text
validate(StructuralLoad) per record        the core's own rule, called, not copied
validate(StructuralRestraint) per record   the core's own rule, called, not copied
duplicate LoadId / RestraintId             a FILE rule, and P16 put the
                                           equivalent one -- "the metric '{}' is
                                           given a threshold twice" -- in the
                                           serializer for the same reason
```

`validate(StructuralAnalysisDefinition)` was **not extended** to cover loads and
restraints. It would be a change to qualified behaviour that persistence does
not need, it would requalify the command layer for an unrelated reason, and
the asymmetry is load-bearing for P17-CMD-001's measured probe results.

### 4. Is the collection order sorted or preserved? PRESERVED.

P16 sorts its local sizing controls, and says why: P16-SIZE-001 declares that
order meaningless and P16-CMD-001's fingerprint ignores it. P17 is the opposite
case and says so just as explicitly — `StructuralAnalysisDefinition` carries
"**ORDER IS THE USER'S AND IS PRESERVED**" over both vectors, and P17-CMD-001's
probe C8 exists because an edit that silently reordered loads was a real
defect. So the file preserves it.

Determinism is unaffected: the order is part of the canonical state, so equal
intent still gives equal bytes.

## The one thing that is genuinely new

`NodalForceLoad` carries a `meshing::MeshStamp` and a `meshing::NodeId`. It is
the only canonical type in the P17 definition that holds mesh-local state, and
three authorities say independently that it must not reach a file:

```text
ADR-031                 "A NodeId or an ElementId is mesh-local, reused across
                        generations and never persisted"
P17-LOAD-001 (qualified) "MESH-LOCAL BY DECLARATION ... What it is NOT: durable
                        point-load intent on the model"
the brief               "NodeId persisted in the structural schema" is an
                        automatic FAIL
```

They agree, so there is no conflict to resolve — only a consequence to choose.
See [NODAL_FORCE_POLICY.md](NODAL_FORCE_POLICY.md); the cost was **measured**
rather than estimated.

## What the audit found that is NOT there

```text
toJson(StructuralResult)            does not exist
serialize(GlobalStructuralSystem)   does not exist
any structural serializer at all    does not exist
```

Searched across `include/bettercad/structural/` and `src/structural/`: the only
match for "serialize" in the whole module is a comment in its `CMakeLists.txt`
saying the module sits "BELOW IO, which will serialize its analysis intent".
So the brief's §123 concern — a debug serializer accidentally reachable from
canonical save — has nothing to check. There is no result serializer to keep
out of the save path, in this milestone or before it.
