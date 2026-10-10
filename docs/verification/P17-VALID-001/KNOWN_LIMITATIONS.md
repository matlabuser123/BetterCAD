# P17-VALID-001 — known limitations

Recorded because a limitation nobody wrote down is a limitation somebody will
later mistake for a guarantee. None of these blocks the milestone; each bounds
what its evidence supports.

## 1. The two-family argument for completeness is an argument, not a measurement

The two hard-fail bounds come from two measured laws, one per degeneration
family:

```text
FLATTENING, toward a plane    err ~ C / sqrt(3r/R)
STRETCHING, toward a line     err ~ C * aspect
```

A tetrahedron's shape can collapse toward a plane or toward a line, and the
claim is that a hybrid — a sliver that is also a needle — is bounded by
whichever bound it crosses first. **That is reasoning, not measurement.** It is
plausible because both bounds are checked independently and either refusing is
enough to refuse the mesh, but no hybrid family was swept.

What would close it: a two-parameter sweep over (apex height, base size) with
the fit constant measured across the grid. What it would cost: a test that
takes minutes rather than seconds, for a case that both existing bounds already
refuse conservatively.

**The honest statement of the guarantee is therefore:** for the two
degeneration families measured, an element inside both bounds recovers a strain
to better than 1e-9. For a hybrid, the bounds still apply and the guarantee is
expected to hold but has not been measured.

## 2. The envelope warning bounds are derived from and asserted against the same fixtures

The four warning bounds are "the worst value any qualified P16 reference model
exhibits, rounded away". The tests then assert that no qualified reference model
warns. That is circular in form.

It is not circular in substance, because the claim the warning makes is exactly
what the measurement says — "this element is outside the range of every mesh
BetterCAD has qualified" — and no stronger claim is made. But the circularity
would become vacuous if a bound were loosened, so
`TheEnvelopeBoundsReallyAreTheMeasuredWorstCase` asserts each bound is within a
**factor of two** of the measured worst. Measured at 1.01x to 1.08x.

**What this means in practice:** the envelope is an eight-mesh sample. A user
meshing a sharper body will warn, correctly flagged as "we have no qualified
experience of a mesh this bad". It is not a statement about good engineering
practice and must not be read as one.

## 3. Only the worst element per metric is reported

`validateStructuralMeshQuality` reads `MeshQualityReport::summaries`, which
carry one worst element per metric. So if three elements are below the accuracy
floor, the report names one.

For the REFUSAL this is immaterial — one is enough to refuse, and the solve does
not proceed. For REPORTING it is a real limitation: a user fixing a mesh would
want every offending element.

The mitigation is in the type system rather than in a workaround: the policy is
a public `meshing::QualityThresholds` value, so a caller wanting per-element
classification calls

```cpp
meshing::evaluateMeshQuality(mesh, structural::structuralQualityThresholds())
```

and reads P16's own `findings`, which are already deterministically ordered.
`AgreesWithP16sOwnPerElementClassification` asserts the two paths reach the same
verdict, so the cheap summary read and the full per-element pass cannot
disagree.

**Why the summary was chosen:** reading `summaries` costs nothing and recomputes
no metric. Running `evaluateMeshQuality` a second time inside validation would
traverse every element again, on top of the traversal the mesher already did.

## 4. The configuration-override refusal is inherited, not tested here

`InputProblem::GeometryIneligible` carries P16's
`ConfigurationOverrideActive`, and it reaches this milestone through
`requireStructuralModel` unchanged. P17-ARCH-001's own suite tests that path.

This milestone tests the two STALENESS paths end to end — geometry and intent —
and relies on P17-ARCH for the override. Stated rather than claimed: the brief
asks for a configuration-stale check and the check exists, but the test that
drives it from a configuration is upstream.

## 5. The laws' constants are debug-ext measurements

Every figure in [ACCURACY_LAW.md](ACCURACY_LAW.md) and
[QUALITY_DISTRIBUTIONS.md](QUALITY_DISTRIBUTIONS.md) was taken in `debug-ext`
and is reported as such. A fit constant is a floating-point quantity and could
move with a different optimisation level or FMA contraction.

Mitigated, not ignored:

```text
each law is PINNED AS A BAND          1e-16 .. 1e-14 for the flattening fit,
                                      1e-16 .. 1e-15 for the stretching fit,
                                      so a preset-to-preset shift inside the
                                      band does not fail a test and a real
                                      regression does
each threshold carries MARGIN         17x and 7.5x on their crossings, rather
                                      than sitting at them
the suite runs in ALL THREE PRESETS   unfiltered, so a verdict that moved
                                      would be caught even if a constant did
```

## 6. "Cannot be bypassed" has a precise and limited meaning

Stated in full in [SOLVE_ENTRY.md](SOLVE_ENTRY.md). Briefly:

```text
CLAIMED      `solveStructuralAnalysis` is the only production path from a
             Document to a StructuralResult, and it validates
NOT CLAIMED  that the compiler forbids every alternative route to a number
```

The stages below remain public because P17's analytical validation needs them.
A caller could construct a `StructuralResultSource` by hand — it is plain data
— and pass it to `StructuralResult::create`. Closing that would mean making
result provenance a possession-gated type, which is a P17-DATA decision.

## 7. The validator re-prepares what the solve then prepares again

`solveStructuralAnalysis` calls `validateStructuralAnalysisForSolve` and then
re-resolves the material, the numbering, the restraints and the loads.

Deliberate: it keeps ONE implementation of the stage order, so the validator and
the solve cannot disagree about what a solve requires. The cost is two O(nodes)
passes and a few lookups — the price ADR-036 already chose for re-preparing
rather than holding. On a very large mesh it is measurable but small beside the
factorisation, and it has not been profiled.

## 8. What the hard-fail bounds do NOT bound

```text
DISCRETISATION ERROR
    The laws measure the error in recovering a field the mesh represents
    EXACTLY. How well a mesh of a given density approximates a field it
    cannot represent exactly is convergence, a different question, owned by
    nothing in P17.

GLOBAL CONDITIONING
    A mesh of individually accurate elements can still assemble an
    ill-conditioned K. P17-SOLVE-001 measures that directly as
    `min|D| / max|D|` and refuses the solve on it. The gates are independent.

SUPPORT SUFFICIENCY
    Six or more constrained degrees of freedom says NOTHING. Only the
    factorisation decides, and this milestone deliberately does not claim
    otherwise.

ENGINEERING ADEQUACY
    An element inside both bounds may still be a terrible element. Passing
    means "the kernel can recover a strain on this to better than 1e-9", and
    no more. The envelope warnings exist to say the rest.
```
