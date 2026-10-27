# P19-DATA-001 - the deterministic semantic fingerprint

## What it is

`canonicalText(definition)` writes one `key=value` line per field of the definition,
in declaration order. Boundary conditions appear in the given order, each with its
index, id, kind, face name (feature, role, entity, path edge, path sketch, chamfer
edge, copies) and values. `fingerprint(definition)` is 64-bit FNV-1a over that
text, written `cfd-definition-v1:<16 hex digits>`.

```text
doubles          std::format("{}") - the shortest decimal that reads back as the same
                 double; -0.0 written as 0 because it compares equal to +0.0
enumerators      by name (steady, velocity_inlet, start_cap, final_only, ...); an out-of-range
                 face role is refused by validation, so two different values cannot both
                 be written as "unknown"
optionals        the value's lines, or "<key>=none"; an absent id is "-"
excluded         the object's name, id and revision; addresses; time; locale; preset
arithmetic       integer only (FNV-1a), so the digest itself cannot vary by optimisation
```

## The claims, and the test of each

| claim | test |
|---|---|
| the text is the documented format, and the digest is FNV-1a of it | `CfdData_TheFingerprintIsAGoldenFunctionOfTheText` (a steady channel: three boundary conditions and gravity) and `_TheSecondGoldenCoversEveryKindAndTheTransientLines` (a transient case with a pressure inlet, a moving wall, a slip wall whose face has a path edge, a path sketch and two copies, a non-zero reference height and every setting off its default, plus a cavity) are each checked against a hand-written expected text, in full, and against a **second FNV-1a implementation written in the test**. The oracle is not the code under test, and the expected text was written from the format, not printed and pasted back. The cavity pins the reference-pressure and chamfer-edge lines **by substring**, and its digest is checked against FNV-1a of the code's own text: weaker than the two full goldens, and the only lines held that way |
| equal definitions give equal fingerprints | the same: rebuilt, copied, held by a second object with another name, id and revision, restored by redo (`_TheFingerprintIgnoresIdentityAndEverythingButMeaning`, `_AddUndoRedoRestoresTheAnalysisExactly`) |
| -0.0 and +0.0 digest alike | the same case: negative zeros in the initial condition, `==` holds, text and fingerprint equal |
| one ULP is a different value | the same case: `nextafter(0.7, 1)` changes the fingerprint |
| every field is covered | `CfdData_TheFingerprintCoversEveryField`: **40 rows**, each one field changed alone, each changing `==`, the text and the fingerprint. The rows cover every definition member, every field of every boundary-condition payload, every field of core's `FaceSelector` and of a `FaceCopy`, every coordinate of the reference location, the gravity vector and reference height, and the transient settings. Every record the text walks is pinned by size, and by member count (a structured binding) wherever its members are public. The `CfdBoundaryCondition` class (private members) and the one-member steady, linear-solver and output settings are pinned by size only; none has padding a member could hide in. A field added to any record therefore breaks the build until the text and the table are extended. The five rows and the class and count pins were added after the independent review (findings 3, 4 and 6) |
| order is meaning | `_TheDefinitionIsValidatedPerMode`: two boundary conditions swapped are a different definition and a different fingerprint |
| the same in every preset | the golden case passes with the same text and number in debug-ext, release-ext and debug-shared-ext; the post-run script collects the printed fingerprint from each preset's own executable and requires them identical |

## What it is for, and what it is not

It records **what intent a result was computed from** (`CfdResultSource::definition`),
readable without the document, and it is the content key a later derived object
can be keyed by. It is **not** the currentness rule on its own: the source also
carries the analysis revision, and both must match. ADR-061 §5 explains why that
decision belongs to P18-QUAL-001's open owner decision D1, and how
`CfdData_AnEditTypedBackKeepsTheResultStale` pins the regime in force.

A 64-bit digest can collide: for two given different definitions the chance is
about 2^-64, and across a million distinct definitions about 2^-25 that any pair
does. A collision would make a stale result's `definition` field look equal, but
never its revision, so currentness is unaffected. Only the provenance record would
be ambiguous, and only in that case.
