# ADR-028 — Provenance is per property; a solver holds no material data of its own

```text
STATUS:    Accepted
DATE:      2026-09-26
MILESTONE: P15-ARCH-001
TOUCHES:   ADR-016 (the drawing scene is the export boundary), ADR-025, ADR-027
```

## Context

Two questions are settled together here because they are the same question from
opposite ends: **where does a property value claim to have come from**, and **who
is allowed to hold property values at all**.

The repository has a precedent for each.

**Provenance.** `core/standards/` records its sources not in code but in
`docs/verification/P12-HOLE-001/`, where each table is "checked there against
independent copies". That is provenance at the granularity of a whole table,
which works because a table is transcribed from one document in one sitting. A
material is not: a real material's density may come from a supplier datasheet
while its thermal conductivity comes from a handbook and its yield strength from
a test report, each with a different condition and date. `MassProperties` already
shows the shape of a value travelling with a statement about itself — it carries
`volumeRelativeError` and `areaRelativeError` beside the numbers.

**The consumer boundary.** ADR-016 settled the same shape for export: "the
drawing scene is the export boundary, and writers compute nothing". The audit
confirms it holds — `SvgWriter.cpp`, `DxfWriter.cpp` and `PdfWriter.cpp` contain
zero references to `core/document`, `features`, `assembly`, `BRep` or `TopoDS`.
A writer cannot recompute engineering semantics it cannot see. Material data
faces the same risk one layer up: three downstream phases (P17 structural, P18
thermal, P19 CFD) all need property values, and each could trivially grow its
own table of "standard steel".

## Decision

### Provenance is per property, with a material-level default

```text
per property   source, standard, revision, date, condition/temper, notes,
               the state it was measured at (e.g. 20 °C)
per material   the same fields, as a default for properties that state none
```

Both, and neither alone. Per-property is the accurate granularity because that is
how data is actually gathered. A material-level default exists so that an entry
transcribed wholly from one datasheet does not repeat the citation nine times.

A property's own provenance **overrides** the material's for that property, and
the effective provenance of a value is always answerable. The brief's concern —
"can provenance say the material came from one source while individual values came
from another?" — is not a bug to prevent but the normal case, and the model states
it explicitly rather than allowing the two to silently disagree.

**Provenance is metadata and never participates in identity, equality of
engineering meaning, or derivation.** Two materials with identical values and
different sources are still two materials (ADR-025: identity is the `MaterialId`).
A derived property (ADR-027's G, K) has no provenance of its own; it reports the
provenance of the inputs it was derived from, so a number can always be traced to
a source rather than appearing from an equation.

**Provenance is canonical and persisted.** A document that computes a stress and
cannot say where its modulus came from is not an engineering record. Persisting it
also makes ADR-025's import honest: the library key and revision an imported
material came from are recorded *as provenance*, which is what makes a later
"the library has moved on" offer possible without the document ever depending on
the library.

### A solver holds no material data

```text
the materials module         owns definitions, property values, derivation
                             rules and provenance
P17 / P18 / P19              consume through one contract, and store nothing
```

The contract is the one ADR-027 names:

```text
effectiveMaterial(document)                -> the part's material, or none
requireProperty(material, kind)            -> Result<Quantity>
requireMechanicalProperties(material)      -> Result<...>   density, E, nu,
                                              sigma_y, sigma_u
requireThermalProperties(material)         -> Result<...>   density, cp, k, alpha
```

**Hard rule: a downstream solver must not maintain a second authoritative
material database.** No `steel()` helper in the FEA module, no default modulus
constant, no "typical values" fallback. A solver that cannot obtain a property
fails with the diagnostic ADR-027 specifies.

This is enforceable the same way ADR-016 is, by layering: the materials module
sits below the analysis modules, so an analysis module may include materials
headers and materials may not include analysis headers. The layering checker
already fails the build on a violation and on an unknown module.

### Module ownership

Audited layer table: `core 0`, `sketch 1`, `features 2`, `assembly 3`,
`drawing 4`, `io 5`, `renderer 6`, `scripting 6`. `standards` is not in the table
because it lives *inside* core as `core/standards/`.

```text
core/units/          quantities and dimensions          exists, extended by
                                                        P15-UNITS-001
core/materials/      the built-in library: immutable reference data, no
                     geometry — the core/standards/ pattern
features (2)         the material document object and the part's assignment,
                     because that is where document objects that describe a
                     part already live
analysis (future)    P17/P18/P19, above features, consuming the contract
```

**Placing the library in `core/materials/` needs no layer-table change**, because
its module is `core` — exactly as `core/standards/` is. That was checked rather
than hoped: the checker derives a file's module from its path and errors with
"unknown module '<x>' (add it to the layer table)" for anything it does not
recognise.

**Core must not depend on FEA, thermal, CFD, GUI or IO**, and this arrangement
keeps that true: the library is data, the consumers are above it, and the
dependency only ever points down.

## Consequences

**A computed result is traceable to a source.** For a mass, a stress or a
temperature field, the chain is value → property → provenance → source, with no
step that invents a number.

**Three solvers agree by construction**, because there is one place values come
from and one place G and K are derived.

**Provenance costs schema and discipline, not correctness.** It is metadata; a
material with no provenance at all still computes. What it loses is the ability to
justify itself, which is a reviewable engineering defect rather than a crash.

**P15 populates almost no real engineering values.** This milestone defines the
contract. The library's actual data arrives with the milestone that transcribes it
and records its sources in that milestone's evidence, exactly as
`P12-HOLE-001` did for ISO 273 and ISO 286.

## Alternatives rejected

**Provenance per material only.** Rejected: it forces one citation to cover
values gathered from different places, which is how a document comes to claim a
source it does not have.

**Provenance per property only.** Rejected as needless repetition for the common
case of a single datasheet, which invites omission.

**Provenance as free-text notes.** Rejected: unsearchable, and it cannot express
"this value was measured at 20 °C", which ADR-027's temperature extension will
need to interpret a value at all.

**Provenance excluded from persistence.** Rejected: a saved document would be
unable to say where its numbers came from, which is most of what makes it an
engineering record rather than a picture.

**Let each solver keep a small table of common materials for convenience.**
Rejected, and this is the rule most likely to be broken by accident. It creates a
second authority that drifts, and it is exactly the shape ADR-016 refused for
exporters. The convenience it offers — a quick default so a solver can run
without a material — is precisely the fabrication ADR-027 forbids.

**A `materials` module of its own at a new layer.** Rejected for the library:
`core/standards/` already establishes that curated reference data with no
geometry belongs inside core, and adding a layer for it would be a table change
with nothing to show for it. The *document object* goes in `features` because
that is where objects describing a part live.

## Validation

```text
core/standards/ records sources in milestone evidence, not in code   P12-HOLE-001
a derived quantity already travels with a quality statement          Body.hpp:25-30
ADR-016's boundary holds: 0 CAD headers in all three writers         audited
the layer table is core 0 ... renderer 6                             CheckLayering.cmake:23-38
standards lives inside core, absent from the table                   core/standards/
the checker fails the build on an unknown module                     CheckLayering.cmake:69-70
the checker enforces downward-only dependencies                      CheckLayering.cmake:91-97
Result<T> carries a structured Error with a code and a message        core/Error.hpp
```

## Invariants

```text
every persisted property value can name its source
a derived property reports the provenance of its inputs
provenance never affects identity or derivation
no module above materials stores a property value of its own
no solver has a default, typical or fallback material
materials never depends on analysis, GUI or IO
```
