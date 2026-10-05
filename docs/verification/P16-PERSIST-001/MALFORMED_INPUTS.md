# P16-PERSIST-001 — malformed input and failure atomicity

```text
SUBJECT:  what a bad file does, which layer refuses it, and what the open
          document looks like afterwards
```

## 1. The matrix

Every case is run against a **real saved document** with its meshing section
replaced, so the rest of the file is valid and the refusal can only come from
the part under test. The valid file is loaded first in the same test, or every
rejection below could be for the wrong reason.

| Case | Expected | Actual | Layer | Document mutated? | PASS |
| --- | --- | --- | --- | --- | --- |
| truncated file | refuse | `ParseError` | parser | **no** | YES |
| unterminated JSON | refuse | `ParseError` | parser | **no** | YES |
| not JSON at all | refuse | `ParseError` | parser | **no** | YES |
| a JSON array instead of an object | refuse | `ParseError` | parser | **no** | YES |
| a file that does not exist | refuse | `IoError` | file layer | **no** | YES |
| `target_size: 0.0` | refuse | `InvalidArgument`, "positive" | **core** | no | YES |
| `target_size: -0.001` | refuse | `InvalidArgument` | **core** | no | YES |
| `target_size: 1e999` | refuse | `ParseError`, "invalid JSON" | parser | no | YES |
| `target_size: -1e999` | refuse | `ParseError`, "invalid JSON" | parser | no | YES |
| `target_size: NaN` | refuse | `ParseError`, "invalid JSON" | parser | no | YES |
| `target_size: Infinity` | refuse | `ParseError`, "invalid JSON" | parser | no | YES |
| `target_size: -Infinity` | refuse | `ParseError`, "invalid JSON" | parser | no | YES |
| `target_size: "large"` | refuse | "expected a number" | parser | no | YES |
| an unknown field (`netgen_maxh`) | refuse | "unknown field" | parser | no | YES |
| `surface` missing | refuse | "missing required field", names `surface` | parser | no | YES |
| `body: 0` (the invalid handle) | refuse | `InvalidArgument` | **core** | no | YES |
| two local controls on one face | refuse | `InvalidArgument` | **core** | no | YES |
| two boundary sets sharing an identity | refuse | `AlreadyExists`, "identity" | **core** | no | YES |
| one metric given a threshold twice | refuse | "twice" | parser | no | YES |
| an unknown quality metric | refuse | "unknown quality metric" | parser | no | YES |
| a malformed face role | refuse | "unknown value" | parser | no | YES |
| a side face naming no entity | refuse | `ParseError`, path names `local` | parser | no | YES |
| a well-formed reference to an **absent** face | **LOAD** | loads, reference kept verbatim | — | n/a | YES |

## 2. Two things the matrix settles that were not obvious

### A non-finite number cannot be expressed at all

The expectation from reading `readNumber` — which only checks `is_number()`
— was that `1e999` would arrive as an infinity and have to be caught by the
core validator. **Measured, it never gets that far**: nlohmann throws
`out_of_range` for a number that overflows a double, `parseJson` maps that to
`ParseError`, and the bare literals `NaN` and `Infinity` are not JSON.

So the core's finiteness check is a **second** line of defence rather than the
only one. Recorded as measured rather than as first assumed, because a test
written on the original belief would have failed and "fixing" it by loosening
the expectation would have hidden which layer actually refuses.

### Unresolved is not malformed, and the distinction is enforced

This is the brief's §77, and it is the most consequential row in the matrix:

```text
{"role": "side", "entity": 9999}   well formed, names no current face
                                   -> LOADS, kept verbatim, unresolved

{"role": "sideways"}               not a role this format has
                                   -> REFUSED, "unknown value"

{"role": "side"}                   a side face must name an entity
                                   -> REFUSED, validate(FaceSelector)
```

A reference to a face that does not currently exist is **legitimate
engineering intent** — the user asked for that face, and a model edit removed
it. Dropping it, or binding it to the nearest current face, would silently
change what was asked for. A reference whose *encoding* is wrong is corruption
and must not load.

The first row is asserted positively: the control loads, and its stored
`FaceName` still names entity 9999 and not any live entity.

## 3. Where each layer draws the line

```text
PARSER          syntax, JSON types, required fields, unknown fields,
                a repeated quality metric, a malformed face selector
                -> ParseError, with the JSON path

CORE            positive and finite sizes, two controls on one face, two sets
(MeshControl::  sharing an identity, an empty set name, an invalid body handle
 create)        -> the validator's own code, carried through with the path

FILE LAYER      a missing or unreadable file
                -> IoError
```

No sizing rule is restated in `MeshControlJson.cpp`. That is the brief's §74,
and the reason the matrix above needed no new validation code: every "core"
row is a rule P16-SIZE-001 or P16-QUALITY-001 already owned, reached through
the file.

## 4. Diagnostics name the path

The format's `childPath`/`indexPath` machinery is used throughout, so a
failure says where:

```text
objects[2].data.sizing.netgen_maxh: unknown field
objects[2].data.sizing.local[0].face.role: unknown value 'sideways'
objects[2].data.quality.limits[1].metric: the metric 'tet_radius_ratio' is
    given a threshold twice
objects[2].data: mesh control: local sizing control 1 ... (the core's message,
    carried through atPath with the path prefixed)
```

Nothing leaks a pointer, an address or a backend handle into a message — the
diagnostics are built from field names and values only.

## 5. Failure atomicity

**Structural, not a precaution.** `documentFromJson` returns
`Result<Document>` and builds a **new** document, so a failed load has nothing
to half-mutate. The test proves the property rather than reading the
signature: with a document open and fingerprinted, four malformed inputs and
one missing file are attempted in turn, and after each one

```text
the open document's canonical fingerprint is unchanged
its revision is unchanged
```

A save that fails leaves the old file valid, because P16 writes through the
existing `writeFileAtomically` and adds no writer of its own. The carried
FileIo atomic-replace defect (transient Windows file locks) is untouched by
this milestone and was not expanded into: persistence here uses the canonical
save mechanism exactly as every other object kind does.
