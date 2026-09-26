# ADR-026 — A material assignment is intent; mass is derived

```text
STATUS:    Accepted
DATE:      2026-09-26
MILESTONE: P15-ARCH-001
TOUCHES:   ADR-005 (placement is intent, transforms are derived),
           ADR-011 (drawing intent is canonical, projection derived),
           ADR-024 (a selection has an identity)
```

## Context

ADR-025 gives a material definition an identity and an owner. This ADR settles
what *uses* it: which model object a material is attached to, what that
attachment stores, and what is computed from it.

The repository already answers the second half. `geometry::MassProperties` holds
`volume`, `surfaceArea` and `centerOfMass` — and notably **not mass**, because
there is no density to multiply by. It is documented as "(uniform density)". It
is also **never persisted**: a search of every JSON writer in `src/io/json/` for
`volume`, `centroid` or `centreOfMass` returns nothing. Geometry-derived
quantities are recomputed, never stored. It carries `volumeRelativeError` and
`areaRelativeError` beside the values, so a derived quantity already travels with
a statement about its own quality.

The audit also settled what a material could possibly be attached to, and this
constrained the answer more than any preference did.

**There is no `Part` document object.** The 26 object kinds are `annotation`,
`chamfer`, `circular_pattern`, `combine`, `component`, `coordinate_system`,
`datum_axis`, `datum_plane`, `dimension`, `draft`, `extrude`, `fillet`, `hole`,
`linear_pattern`, `loft`, `mate`, `mirror`, `revolve`, `rib`, `sheet`, `shell`,
`split`, `sweep`, `variable_fillet`, `view`. A part **is** a document:
`ComponentDefinition::part` is an `ObjectReference`, and a component in an
assembly names a part elsewhere.

**There is no `body` document object either.** `BodyId` exists and widens to
`ObjectId`, but no type declares `kTypeName = "body"`. A body is a regeneration
*result*, and the handle callers actually use is the ObjectId of the **feature
that produced it** — `regenerator.body(featureId)`. A solid inside a body, and a
face of it, have no persistent identity at all; that is the ground ADR-012 and
ADR-024 stand on.

## Decision

**A material assignment is document-level intent: one optional `MaterialId` for
the part.**

```text
the part's material  =  document-level state, like the active configuration
                        one optional MaterialId, or none
```

It is document-level state rather than a document object for the same reason
`ConfigurationId` is: there is exactly one of it, it needs no identity of its
own, and nothing references it.

**Mass is derived, never stored.**

```text
CANONICAL   the material's density                  (ADR-027)
            the part's material assignment
DERIVED     volume, surface area, centre of mass    already, from geometry
            mass = density x volume
            centre of mass of a uniform part = the geometric centroid
            inertia tensor
```

`MassProperties` is extended by a *separate* derived result that needs a
material; the existing geometry-only struct is not given a `mass` field it cannot
always fill. A request for mass on a part with no material assigned **fails with
a diagnostic** (ADR-027), and never returns zero.

**One material per part in P15.** Per-body materials are deferred, and the
mechanism is named so the deferral is not a dead end: a body's only persistent
handle is its producing feature's `ObjectId`, so a later milestone can add an
override keyed by that feature. What will never be offered is per-solid or
per-face material, because neither has persistent identity — offering it would
rebuild the defect ADR-024 removed.

**An assembly occurrence cannot override a part's material in P15**, and this is
an explicit deferral, not an oversight. A part's material lives in the part
document; an occurrence lives in the assembly document. An override would have to
name a material across that boundary, and ADR-003 defers external references —
"Part references are internal for P13; external references are [future]". P15
will not pre-empt that work. **Consequence, stated plainly: an assembly's mass
properties use each part's own material, and two occurrences of one part always
have the same material.** When external references arrive, the override belongs
on `ComponentDefinition` as an optional material reference, which is where
`suppressed` and `placement` already live.

**Configuration does not affect material assignment in P15.** This is forced by
audited semantics, not chosen: `Configurations.hpp` states that "a configuration
overrides the values of free parameters, and everything else follows from the
equations that were already there", and "what a configuration never does is
change the document's canonical state". A `MaterialId` is not a parameter value —
parameters are dimensioned scalars — so the existing override mechanism cannot
carry it. The only non-parameter override is suppression, and it is typed:
`suppressComponent` refuses an ID that is not a component because "a suppression
override on a sketch would be a statement about nothing".

So:

```text
assignment under configuration A   the same material
assignment under configuration B   the same material
a suppressed component             still HAS its part's material; suppression
                                   means "not in this build", not "material
                                   deleted" (ADR/P13 wording, preserved)
```

A configuration changes a part's *dimensions*, so it changes volume and therefore
mass — through geometry, which is exactly the existing derivation chain and
requires nothing new.

## Consequences

**Mass cannot go stale, because it is never stored.** The brief's concern —
"can mass be persisted and become stale after geometry changes?" — is answered by
the same rule that already keeps a drawing's projected curves out of the file.

**A part with no material is a normal, representable state**, not an error at
rest. It becomes an error only when something asks for mass, and then it is a
diagnostic naming the part, not a zero.

**Assemblies get mass properties as soon as parts have materials**, with no new
reference machinery, because each part answers for itself.

**Two occurrences of one part cannot differ in material.** For the common
engineering case — the same bracket in steel and in aluminium — the answer in P15
is two parts, which is also how they would differ in any other way. Recorded as a
known limitation rather than hidden.

**The deferral is bounded and named.** Both deferred capabilities have a stated
mechanism and a stated precondition, so P17 and beyond are not left guessing:
per-body needs a feature-keyed override; per-occurrence needs external
references.

## Alternatives rejected

**A material assignment as a document object** (an "assignment object" holding
target + material). Rejected for the part-level case: there is exactly one, it
has nothing to reference it, and giving it an identity adds a node to the
dependency graph that carries no information. It becomes the right shape only
when assignments multiply — per body or per occurrence — which is precisely when
a later milestone should introduce it.

**A material field on every feature.** Rejected: it invites a material on a
sketch or a datum plane, for which the semantics are undefined, and the
repository's own answer to that shape is `suppressComponent`'s refusal — an
override that names an object it cannot mean is an error, not a default.

**Mass stored on the part and invalidated on change.** Rejected: it is a cache
with a correctness obligation, and the repository has a working rule — derived
geometry is not persisted — that costs nothing to keep.

**Per-body material keyed by body index.** Rejected outright: a positional index
into a regeneration result is the exact defect ADR-024 removed from chamfer
references.

**Configuration-selected material variants.** Rejected for P15: it would require
a new override kind, and the brief's own instruction is not to add
configuration-dependent material behaviour without a strong architectural reason.
None was found. If it is ever wanted, the honest shape is a new typed override
beside suppression, not a parameter carrying an ID.

## Validation

```text
MassProperties has volume/area/centroid and NO mass                Body.hpp:20-30
it is documented as "(uniform density)"                            Body.hpp:20
it carries relative error estimates beside the values              Body.hpp:25-30
derived geometry is never persisted (0 hits in every JSON writer)   src/io/json/
there is no Part document object; a part IS a document             Component.hpp:60
there is no body document object (no kTypeName "body")             audited: 0 hits
a body's handle is its producing feature's ObjectId                regenerator.body(id)
a configuration overrides free PARAMETER values only               Configurations.hpp:1-21
a configuration never changes canonical state                      Configurations.hpp:16-21
suppression is typed and refuses a meaningless target              Configurations.hpp:73-75
external part references are deferred                              ADR-003
ConfigurationId is document-level state, not an object             Id.hpp:154-157
```

## Invariants

```text
mass, centre of mass and inertia are derived and never persisted
a part with no material is representable; asking it for mass is a diagnostic
a material is never attached to an object whose material semantics are undefined
no material assignment is keyed by a position in a regeneration result
switching configurations never changes which material a part uses
a suppressed component keeps its material
```
