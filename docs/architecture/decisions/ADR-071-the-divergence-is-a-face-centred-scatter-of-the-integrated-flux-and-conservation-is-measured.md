# ADR-071 — The divergence is a face-centred scatter of the integrated flux, and conservation is measured rather than asserted

```text
Status      Accepted
Date        2026-11-07
Milestone   P19-DIV-001 — Divergence / Flux Operator
Authorized  2026-11-07, the owner's "Authorize DIV-001", in answer to a
            question naming this milestone and no other. The twelve item
            checklist was already in TODO.md and the owner neither quoted nor
            altered it, so the checklist is the scope
Supersedes  nothing
Amends      nothing
Depends on  ADR-058 (one FV cell is one P16 tetrahedron; no fourth variable),
            ADR-063 (the topology, and finding F1 reserving thresholds),
            ADR-064 (the face winding: out of the owner), ADR-065 (the
            geometric metrics and the skewness measurement), ADR-066 (the
            frozen variable table: phi is FACE-located and is kg/s),
            ADR-067 (the frozen boundary table, including the phi column),
            ADR-070 (the gradient, whose face interpolation this reuses)
```

## Context

The checklist states the operator as one identity:

```text
∫_V ∇·F dV = Σ_f F_f · Sf
```

Everything below follows from reading that identity carefully together with
what the repository already holds, and from four measurements taken **before any
C++ existed** (`experiments/01-cancellation-and-closure.py`, seed 20261107).

Three facts about the existing code decided more of this design than any
preference of mine.

**`phi` already exists, and it is already integrated.** ADR-066's frozen
variable table has three variables, and the third is
`{MassFlux, Face, 1, mass/time, "phi"}`. `CfdFields.hpp` is explicit that
"`phi` IS A MASS FLOW RATE, kg/s, NOT A MASS FLUX, kg/(m^2 s), despite the name
every text uses. It is the face-INTEGRATED quantity `rho (U . Sf)`". So the
right-hand side of the identity, for the mass variable, is **already what the
field model stores**: a divergence of `phi` is a signed sum with no area factor
and no interpolation anywhere.

**The face area vector is wound out of the owner, and there is no sign array.**
ADR-064 stores `Sf` so that it points owner → neighbour across an internal face
and out of the fluid at a boundary face. `CfdGeometry.hpp` warns in as many
words that "a caller summing a cell's four stored area vectors without
consulting `owner` gets a residual of order one, not of order eps". The sign is
therefore the whole of the internal-face cancellation question.

**There is no prescribed `phi` to read.** ADR-067's table gives `phi` a policy
at every boundary kind — `FixedValue` at the three walls and the velocity
inlet, `Computed` at both pressure boundaries — but `CfdBoundaryFace` carries
only `pressure` and `velocity`. No accessor anywhere returns a prescribed mass
flow rate, and `Computed` exists precisely because at a static-pressure inlet
"the flow rate is an OUTCOME of the solve, not an input" (ADR-058). This is
decisive for Decision 5.

### The measurements, made before any C++ existed

```text
Q1  the sign flip        (-S) . U  ==  -(S . U)   200,000 random triples
                         0 mismatches, worst relative gap 0.000e+00: EXACT

Q2a per-face cancel      owner contribution + neighbour contribution == 0.0
                         20,000 internal faces, 20,000 bit-exact (100.00%)

Q2b global conservation  sum over cells  vs  the boundary sum, 20,000 meshes
                         bit-exact on only 8,067 (40.34%)
                         worst |imbalance| / sum_f |flux_f| = 1.125e-16

Q3  closure              sum_f S_f bit-zero on 972 of 20,000 tets (4.86%)
                         worst |sum S| / sum |S| = 3.993e-16
    constant field       divergence bit-zero on 4,827 (24.14%)
                         worst residual / (sum|S| |c|) = 2.144e-16

Q4  linear exactness     (1/V) sum_f U(C_f) . S_f  vs  trace(A), 20,000 tets
                         worst gap / (sum|S||U| / V) = 2.966e-16   (eps 2.220e-16)
```

**Q2b CONTRADICTED THE PREDICTION THIS ADR WAS GOING TO MAKE.** The first draft
of the experiment asserted that a face-centred scatter would make global
conservation bit-exact, and measured 40.34% instead. The reason is not subtle
once seen: each cell accumulates its own faces **before** the two sides of an
internal face ever meet, and `(a + x) + (b - x)` is not `a + b` in floating
point. The pairwise claim (Q2a) is exact; the global claim is not, and cannot
be. Decision 3 is what that costs.

The first draft of the experiment also reported a worst relative gap of
`9.992e+284`, which is not a number about the arithmetic at all: it divided by
`max(|boundary_total|, 1e-300)`, and a mesh whose boundary sum nearly cancels
sent the denominator to the floor. That is P19-GRAD-001's finding r8 — a
tolerance stated relative to the answer rather than to the scale the sum
accumulates — recurring in the first thing this milestone measured. Both the
prediction and the mis-measurement are recorded here rather than quietly fixed.

## Decision 1 — The stored quantity is the FACE-INTEGRATED flux, and the divergence is a derived read

The field stores, per cell, the signed sum

```text
    N_P  =  Σ_f  ±(F_f · S_f)
```

which is the **left-hand side of the checklist's own identity**: the volume
integral of the divergence, equivalently the net outflow from the cell. The
per-volume divergence is a derived read, `N_P / V_P`.

The direction of that derivation is the decision. The conserved quantity is the
integral: a control-volume balance sums `N_P`, and global conservation is a
statement about those sums. If the field stored `N_P / V_P` instead, recovering
the conserved quantity would mean multiplying by the volume again at every read,
putting a rounding step **inside the identity being claimed**. So the integral
is stored and the division is offered, never the reverse.

```text
entry point        stored Q                 derived Q / Volume
velocity           m^3/s  volumetric flow   1/s            = ∇·U
mass flux          kg/s   mass flow         kg/(m^3 s)     = ∇·(rho U)
```

Both quantity aliases are formed by division, as ADR-070 Decision 3 formed the
gradient pair, and they live in `CfdData.hpp` for the reason that decision
gives: `Quantity` is templated on a `Dimension` with compile-time division, so
naming the type costs nothing, and adding an alias to layer 0 to serve one
module is the change ADR-063 Decision 7 refused.

### Rejected: store the per-volume divergence only

Simpler to read and wrong where it matters. It makes the conservation check
report the residual of an arithmetic operation the operator performed for the
check's benefit, rather than of the sums it actually accumulated.

### Rejected: return both as separate fields

Two arrays that must agree, where one array and an accessor cannot disagree.

## Decision 2 — The traversal is a face-centred SCATTER, not a cell-centred gather

The operator walks **faces once**, computes one flux per face, adds it to the
owner's accumulator and subtracts it from the neighbour's. It does not walk
cells and gather their faces.

This is the opposite of ADR-070's Green-Gauss loop, which gathers, and the
reason is Q1 and Q2a. Because `(-S) · U` is bit-for-bit `-(S · U)` — 200,000
trials, zero mismatches — one face product negated gives the neighbour's
contribution **exactly**, so an internal face's two contributions sum to
exactly `0.0` on every one of 20,000 measured faces. Internal-face cancellation
becomes a property of the traversal rather than a numerical hope.

A gather would compute the face value twice, once from each side. ADR-070
already had to defend against the two visits disagreeing, and solved it by
making the interpolation weight **the owner's** so that "the face has ONE value
whichever of its two cells is asking". A scatter removes the question instead of
answering it: there is only one visit, so there is nothing to keep consistent.

The gradient could not do this for least squares, whose per-cell normal system
is irreducibly a gather. The divergence has no such constraint.

### Rejected: gather per cell, for symmetry with ADR-070

Consistency with the neighbouring operator is worth less than making the
milestone's central conservation claim structural. It also doubles the face-value
work.

## Decision 3 — Global conservation is MEASURED, and never asserted to be zero

The summary carries the global imbalance as a number:

```text
netBoundaryFlux    Σ over boundary faces of the outward flux
globalNetFlux      Σ over cells of N_P
globalImbalance    the difference, in the integrated unit
```

In exact arithmetic the first two are equal and the third is zero. Q2b measured
that they are **not** bit-equal — 40.34% of meshes, worst `1.125e-16` of the
accumulated flux magnitude — so the operator reports the difference and makes no
claim that it vanishes.

This follows ADR-069's treatment of its own uncorrected faces and ADR-070's of
its skewness: the honest statement of an operator's error is a measurement it
publishes, not a tolerance it hides. ADR-063 finding F1 reserves any
accept-or-refuse threshold on mesh or solution quality to P19-VALID-001 and the
owner, so there is no limit here and nothing is refused for a non-zero
imbalance.

**What IS promised exactly** is the pairwise claim: for any internal face, the
owner's and the neighbour's contributions from that face sum to exactly zero.
That is Decision 2's structural guarantee, it is testable per face, and it is
the strongest true statement available.

**And it is NOT carried as a summary flag**, which is a correction to this
decision's first draft. A flag reporting whether those two contributions sum to
zero would be asking whether `x + (-x) == 0.0` for a finite `x`, which is true
for every finite double: it could not read false, so it would be a summary
member that cannot vary, reading as evidence while measuring nothing. That is
the tautological-check defect P19-DIFF-001's review found in a cardinality test,
and here it was caught before the code existed rather than by a reviewer after.
The claim is pinned by the suite reading an internal face's two contributions off
a two-cell field, and by Q1's 200,000 trials — neither of which is something the
operator need say about itself.

## Decision 4 — Two entry points, one template, and the velocity path is the only one that needs a boundary

```text
divergeCfdMassFlux(topology, geometry, input)                -> kg/s field
divergeCfdVelocity(topology, geometry, boundary, input)      -> m^3/s field
```

`CfdDivergenceField<Q>` is one template with two explicit instantiations, so a
mass-flow field cannot be passed where a volumetric one is wanted — the device
`CartesianVector` and `CfdGradientField` both use, and for the same reason: the
check is at compile time rather than a runtime tag a caller may ignore.

The asymmetry in the signatures is not an oversight. The mass-flux path reads a
caller-owned **face** array and needs no boundary at all (Decision 5). The
velocity path reads cell values and must therefore ask ADR-067's table what the
velocity is at a boundary face, exactly as the gradient does.

## Decision 5 — The mass-flux path consults no boundary, so `Computed` is NOT refused

`phi` is a `Face`-located field in ADR-066's table. The caller owns one value
per face, boundary faces included, and the operator sums what it is given.

It follows that this path **cannot** refuse the `Computed` policy, and must not.
ADR-070 refuses `Computed` for a cell-centred variable because a boundary value
it needs would have to be invented. Here the value is not inferred from a policy
at all: it is supplied. At a pressure inlet `phi` is an outcome of the solve, and
by the time anyone takes its divergence the solve has produced it — so refusing
that face would refuse a correct input. There is no `VariablePolicyIsAnOutcome`
enumerator on this path.

This is a real divergence in treatment between two neighbouring operators, and
it is recorded here so that it reads as a decision rather than an inconsistency.
The rule behind both: **a policy says where a value comes from, not whether a
sum may be taken over it.**

### Rejected: take `phi` per boundary face from the prepared boundary

There is nothing to take. `CfdBoundaryFace` holds `pressure` and `velocity` and
no mass flow rate, and inventing `rho (U_b · S_f)` here would duplicate a
derivation that belongs to whatever computes `phi` — P19-CONV-001's first
checklist item is "Define mass flux `phi`", and it is not authorized.

## Decision 6 — The velocity face value is a VECTOR, interpolated with ADR-070's own weight

On an internal face each component is interpolated with the **same owner
weight** ADR-070 derived,

```text
    w  =  (d·n − (C_f − C_P)·n) / (d·n)        a normal projection
    U_f,i  =  U_N,i + w (U_P,i − U_N,i)
```

in that stable form and not `w U_P + (1-w) U_N`, because the first reproduces a
constant exactly and the second does not — the defect P19-DIFF-001's own test
caught in its face viscosity and P19-GRAD-001's mutation sweep then caught
again in a test that could not see it.

Reusing the gradient's weight is a correctness requirement, not tidiness: a
Green-Gauss gradient and this divergence evaluated on the same mesh must agree
about what the field is at a face, or `trace(grad U)` and `div U` would be two
different numbers for one quantity.

### Amendment, 2026-11-07 — "exactly one definition" was false when it was written

This decision read **"The weight therefore has exactly one definition, and both
operators read it."** An independent review established that it was false, and
the sentence is corrected here rather than quietly replaced, because an ADR that
states a correctness requirement and then rests it on textual duplication is the
same class of defect ADR-069 and ADR-070 were each corrected for.

There were **three** copies when that sentence was written:

```text
cfdFaceInterpolationWeight    PUBLIC and EXPORTED (ADR-069 Decision 7,
  CfdDiffusion.hpp:188        P19-DIFF-001). Returns std::optional<double>:
                              nullopt where there is no positive orthogonal
                              distance or no unit normal, and 1.0 at a boundary
                              face. THE NORMATIVE DEFINITION
CfdGradient.cpp:282           a private static copy, from P19-GRAD-001, which
                              returns 0.5 in the degenerate case
CfdDivergence.cpp:103         a private static copy added by THIS milestone,
                              likewise returning 0.5 -- under a comment
                              claiming it was "read from one definition and not
                              re-derived"
```

The non-degenerate arithmetic was identical in all three, so no number this
milestone measured or reported changes. The degenerate branches were not: the
exported form refuses, and the two copies silently interpolated at a midpoint,
which would have returned a **successful** field computed with a weight nobody
asked for — against this repository's rule that errors are never silently
swallowed, and against the operator's own "REFUSED, NOT SKIPPED" doctrine three
lines away.

What is true now:

```text
this operator READS the exported definition and refuses when it is empty
the gradient still holds its private copy, from P19-GRAD-001
```

Unifying the gradient's copy is **not** in this milestone's checklist and is not
done here: it is another milestone's translation unit, and CLAUDE.md's rule
against unrelated refactoring applies. It is recorded as a limitation of this
milestone instead, and the cross-operator test
(`TheVelocityDivergenceAgreesWithTheTraceOfTheGreenGaussGradient`) is what would
catch the two drifting apart in the non-degenerate branch — which is the branch
that affects an answer.

On a boundary face the value comes from the `U` column of ADR-067's table:
`FixedValue` gives the prescribed `Velocity3`, `ZeroGradient` gives the owner's
own velocity, and `Symmetry` is Decision 7.

Because the whole velocity is taken rather than one component's slice, the
refusal ADR-070 needed for a scalar field meeting `Symmetry`
(`SymmetryNeedsAVectorField`) **cannot arise here** and no enumerator for it
exists. A divergence is intrinsically a vector operation; there is no scalar
entry point to protect.

## Decision 7 — A `Symmetry` face contributes exactly zero flux, by the policy's own definition

`Symmetry` means the normal component is zero and the tangential components are
zero-gradient. The normal volumetric flow through such a face is therefore zero
**by definition**, and the operator writes exactly `0.0`.

The alternative is to form `U_b = U_P − (U_P · n) n` and then dot it with `S_f`.
That computes a quantity already known to be zero, and gets it only to rounding:
`U_b · n = U_P·n − (U_P·n)(n·n)` vanishes exactly only when `n·n` is exactly 1,
which a normalised direction need not be. Taking the definition is both more
accurate and simpler.

This is not a shortcut around a test. The flux through a slip wall is zero
because the boundary condition says the fluid does not cross it, and an operator
that reported `3e-17` there would be reporting its own rounding as physics. The
suite measures the discarded alternative and records the difference, so the claim
is evidenced rather than asserted.

A `NoSlipWall` reaches zero on its own — `U_b` is prescribed `0`, and `0 · S_f`
is exactly `0.0` — so no special case is needed for it. A `MovingWall`'s
prescribed `w` is dotted honestly; if `w` has a normal component the boundary
condition is itself inconsistent, which is ADR-067's business and not this
operator's.

## Decision 8 — The constant mode is reported as the measured CLOSURE, not as a boolean

For a constant field `U = c`, `Σ_f c · S_f = c · (Σ_f S_f)`, which vanishes
because a closed cell's surface vectors sum to zero. Q3 measured what that is
worth in floating point: `Σ_f S_f` is bit-zero on only **4.86%** of random
tetrahedra, and the constant-field divergence is bit-zero on **24.14%** —
residuals at `3.993e-16` and `2.144e-16` of their own scales, so the identity
holds to rounding and not to the bit.

**This decision first copied ADR-070 Decision 7 and carried a
`constantModeIsExact` boolean derived from the closure. Q3 says that would have
been false.** 4.86% against 24.14% means the two properties are not the same set
in *either* direction: a dot-product accumulation can cancel to exactly zero on
a cell whose closure is not bit-zero, and need not cancel on one where it is.
ADR-070 could tie its flag to the closure because Green-Gauss multiplies one
scalar into the surface vectors and sums the vectors; a divergence contracts each
face vector against a different interpolated velocity, so the cancellation no
longer travels with `Σ_f S_f`.

The summary therefore reports `worstClosure`, the worst `|Σ_f S_f| / Σ_f |S_f|`
over the cells — a dimensionless measurement that genuinely bounds the
constant-mode residual — and makes no bit-exactness claim at all. Deriving one
honestly would mean running a probe constant inside the operator, and
P19-GRAD-001 demonstrated how treacherous that choice is: `17.25` is `69/4`, so
both products are exact and the probe cannot distinguish an unstable
interpolation from a stable one.

The exactness claim belongs to the suite, which tests the constant field
directly with a constant chosen to discriminate, and which carries the qualifier
this decision would have had to: a `FixedValue` inlet prescribing a velocity
different from the interior constant describes a field that is not constant,
whose true divergence is not zero.

## Decision 9 — Determinism is the stored face order, and nothing else

The accumulation order is `FaceIndex` order for the scatter and `CellIndex`
order for the global sum, both of which ADR-064 fixes in storage. No parallel
reduction, no unordered container, no sort. A field divided twice from the same
inputs is bit-identical, and the two repeat qualification stages are what
establishes it rather than a claim here.

## Decision 10 — What this milestone refuses to do

```text
no convection scheme     P19-CONV-001 defines phi and its interpolation, and is
                         NOT authorized. This operator consumes a phi somebody
                         else produced
no solve                 nothing here is handed to a solver, and no momentum or
                         pressure-correction equation exists. A divergence is one
                         term of an equation that has not been assembled
no least-squares form    trace(grad U) from ADR-070's least-squares gradient is
                         linear-exact on any mesh, where the surface sum is exact
                         only where the interpolation is. It is NOT built: the
                         checklist states the surface-sum identity, and a second
                         method would duplicate ADR-070 to answer a question the
                         checklist does not ask. Recorded as a limitation
no threshold             ADR-063 F1 reserves accept-or-refuse on quality to
                         P19-VALID-001 and the owner. Imbalance and skewness are
                         REPORTED; only a non-finite result is refused
no matrix                a divergence is a reduction, not an operator with rows.
                         CfdSparse is not linked by this translation unit
```

## Consequences

The pairwise cancellation guarantee is exact and structural, which makes
"Internal-face cancellation" a property a test can pin per face rather than a
tolerance. Global conservation is a published measurement, which is weaker than
a reader might assume and is the true state of the arithmetic.

The velocity path inherits Green-Gauss's skewness sensitivity exactly, because
it is the same surface sum with the same interpolation: Q4 shows the sum is
linear-exact with the TRUE face value, so every departure belongs to the
interpolation, and `worstSkewness` is the measured bound on it. A caller wanting
a mesh-independent divergence must take the trace of a least-squares gradient
itself, and this operator says so rather than pretending otherwise.

The two entry points have different signatures and different refusals, and the
asymmetry is load-bearing: one reads a face field it is given, the other reads
cell values and a frozen policy table. Writing them as one function taking an
optional boundary would hide that difference behind a null check.
