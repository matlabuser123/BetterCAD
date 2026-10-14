# P17-PERSIST-001 — schema version and compatibility

```text
document schema before    version 2  (kOldestReadableDocumentVersion 1)
document schema after     version 2  -- UNCHANGED
P17 structural section    no version of its own
migration required        NO
```

## Why the version does not move

`DocumentFile.hpp` carries the rule as a forty-line comment (P13-PERSIST-001),
and it is not ambiguous:

> **Bump this only for a change that an existing reader would get WRONG.**

Adding an object type is additive. What makes additive growth safe is that both
directions already fail safely without the version doing any work:

```text
new reader, old file   loads. An absent section means "none of those", which
                       is what an older document meant by not having one.

old reader, new file   REFUSES. An unrecognised object type is a parse error
                       naming it -- "unknown object type
                       'structural-analysis'" -- never a silent skip, so no
                       data is lost to a reader that predates P17.
```

The version exists for the case those two rules cannot cover: a field that
changes meaning, a unit that changes, a default that flips, an ordering that
becomes significant. P17 does none of those — it adds a type and touches no
existing field.

The same comment makes the argument against a gratuitous bump better than this
file could: the schema has grown parameters, expressions, configurations,
datums, stable face references, components, placements, mates, mechanical
joints, suppression, materials and meshing controls, "**and the number has
correctly never moved, because every one of those was additive.**" A P17 bump
would be the first unnecessary one in the project's history.

## The fixture matrix

| Fixture / version | Expected | Actual | Re-save deterministic? | PASS |
| --- | --- | --- | --- | --- |
| `examples/models/meshed_plate.bcad` — a real pre-P17 document | loads; no structural analyses; its own semantics unchanged | exactly that | yes — re-saves at `"version": 2` with no structural section invented | ✓ |
| a current document with a full P17 section | loads, every field exact | 38 tests | yes | ✓ |
| a current P17 document with an **empty** analysis collection | loads; loads and restraints empty; solve refused by validation, not by the reader | exactly that | yes | ✓ |
| a hand-written file with an explicit `"loads": []` | accepted, then **normalised** on first save | exactly that; byte-stable from the first save onward | yes, after one normalising save | ✓ |
| a hand-written full P17 file never written by BetterCAD | loads as written; regenerates, meshes and solves | 2 tests | — | ✓ |

**No earlier P17 schema exists**, so no migration code was written. The brief's
§32 asks for migration "only if genuinely required" and warns against
artificial migration complexity; there is none to be required, and inventing a
version-1-structural-section migration for a section that has never shipped
would be exactly the complexity it warns about.

## The pre-P17 fixture is really pre-P17

`StructuralPersistence_APreP17DocumentLoadsAndSimplyHasNoAnalyses` does not
take that on trust:

```cpp
const std::string text = readFile(file);
// The fixture really does predate P17, or this proves nothing.
REQUIRE(text.find("structural-analysis") == std::string::npos);
```

and then checks three things rather than one:

```text
the document loads                                        yes
structuralAnalysisIds(loaded) is empty                    yes
its own semantics are unchanged -- the meshing control
  is still there                                          yes
re-saving does not invent a structural section             yes
re-saving keeps "version": 2                               yes
```

The last two matter because the brief's §79 asks for them specifically: loading
an old document and saving it must not fabricate an empty analysis, and must
not advance the version.

## Unknown fields are REJECTED, which is the existing policy

The brief offered both policies (§26) and asked for the project's own.
BetterCAD's is strict rejection, and it is enforced by the shared helper rather
than by P17 code:

```cpp
/// @p value must be an object whose keys are all in @p allowedKeys.
Result<void> requireObject(const Json& value, std::string_view path,
                           std::initializer_list<std::string_view> allowedKeys);
```

Every object in the P17 section goes through it, so an unrecognised key is a
parse error naming the key. Two cases test it directly — an unknown field on
the analysis (`max_iterations`) and an unknown field on the solver section
(`ordering`).

This means BetterCAD is **not** forward-compatible with a future P17 field, by
design and consistently with every other object kind: a newer file is refused
with a diagnostic rather than silently losing the field. That is the behaviour
the brief's §33 prefers ("prefer explicit failure over misinterpretation").

## Missing fields

There is no version of this schema in which a required field is absent, so a
missing one is a malformed file rather than an older shape. In particular the
solver section's three fields are **always written**, so reading an absent one
as "whatever this build defaults to" would be precisely the silent substitution
the always-write rule exists to prevent. Three cases test it: a missing
algorithm, a missing tolerance and a missing pivot floor are each refused by
name, never defaulted.

The two optional keys — `loads` and `restraints` — are optional in the schema
itself and absent means empty, which is a documented state and not a default
value.

## Opening a P17 document needs no solver and no mesher

`StructuralPersistence_TheFileNeedsNoSolverOrMesherToBeInterpreted`, the
brief's §191: the whole test loads a full P17 document and reads every field of
the intent with no `Mesher` and no `Regenerator` touched after the load, and no
mesh in existence. Reading reconstructs intent and does nothing else.
