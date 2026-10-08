# P17-SOLVE-001 — how the constraints are applied

## Exact free-system reduction, and only that

```text
    [ Kff  Kfc ] [ uf ]   [ Ff ]
    [ Kcf  Kcc ] [ uc ] = [ Fc ]      with  uc = 0

    so        Kff uf = Ff
    and       u = uf on the free DOFs, EXACTLY 0 on the constrained ones
```

```text
method                    exact free-system reduction
penalty method used       NO
automatic pinning used    NO
regularisation used       NO
row zeroing used          NO -- and the two methods are not combined, which is
                          brief section 48's requirement: nothing zeroes a
                          constrained row or sets a diagonal to one on the
                          full K
```

For the current zero-displacement scope there is no `Ff -= Kfc uc` correction,
because `uc = 0`. The partition is written out in the header anyway, and the
extraction drops the constrained columns at the one place that term would be
subtracted — so a later prescribed-displacement milestone adds **one term**
rather than discovering the structure.

Verified by search over the implementation with comments stripped (735 lines):

```text
1e12, 1e20, penalty, epsilon          0 occurrences each
pin, spring, stabilize, regularise    0 each
```

## The numbering is P17-DOF's, not this milestone's

```text
global DofIndex  ---- FreeEquationMap::equationOf ---->  FreeEquationIndex
                                                          (zero-based position)
FreeEquationIndex ---- FreeEquationMap::dofOf ---->      global DofIndex
```

`buildFreeEquationMap` is the only way to obtain a `FreeEquationMap`, and it
**refuses a numbering and a constraint set built against different meshes**. So
the M1/M2 check cannot be forgotten: there is no other route.

The solver renumbers nothing. The single place a 1-based `DofIndex` becomes a
zero-based row is confined to `extractFreeSystem`, with the conversion written
once:

```cpp
const auto globalRow = static_cast<std::size_t>(dof.value() - 1);
```

which is correct because the assembly's row space *is* the free numbering of
the empty constraint set (ADR-038), so a global row and a `DofIndex` differ by
exactly one.

## Extraction: a copy, in FreeEquationIndex order

```text
copies / views / reassembles      COPIES
```

Stated rather than implied: a view would have to carry the global pattern and
skip constrained entries on every access, which puts the constraint logic
inside every inner loop. The copy is `O(nnz(Kff))`, is built once, and makes
`Kff` an ordinary CSR that the residual loop and the factorisation both read
directly.

**The order is `FreeEquationMap::freeDofs()`, which is ascending**, so the
reduced rows are visited in ascending global order and the inner indices of
each row come out ascending *without a sort* — which is what a CSR needs and
what makes the extraction deterministic by construction rather than by a
sorting step a future caller might skip. Asserted: every reduced row's inner
indices are strictly ascending.

Nothing traverses an unordered container: `unordered` has zero occurrences.

### Checked entrywise against the definition

```text
for every free pair (i, j):   Kff(i, j) == K(dofOf(i), dofOf(j))     EXACT
for every free row i:         Ff(i)     == F(dofOf(i))              EXACT
```

Both compared with a tolerance of **zero**, because the reduction copies
values and does not recompute them. A constrained row or column leaking in
would break the first; the mutation that keeps the `Kfc` columns is probed.

Symmetry is checked too, because `SimplicialLDLT` reads only one triangle: if
the extraction had broken symmetry the solver would silently use half of a
matrix that is not the one BetterCAD assembled. Measured:
`largestSymmetryError(Kff) <= largestSymmetryError(K)`.

## Representative dimensions

The block fixture, with a fixed support on one face:

```text
Ndof                 57
Nconstrained         12
Nfree                45
K dimensions         57 x 57       nnz(K)   1647
Kff dimensions       45 x 45       nnz(Kff) 1071
F size               57
Ff size              45
```

RM-MESH-01, with a fixed support on its bottom face:

```text
Ndof                 24
Nconstrained         12
Nfree                12
Kff dimensions       12 x 12       nnz(Kff) 126
```

An explicit global-to-free example, from the same fixture: the constrained set
is the twelve degrees of freedom of the four nodes of the start cap, so their
`DofIndex` values have no `FreeEquationIndex` at all and every other
`DofIndex` maps to one, in ascending order. `Nfree + Nconstrained == Ndof` is
asserted on every fixture.

## Reconstruction

```text
free global DOF         u[row] = uf[equationOf(dof)]
constrained global DOF  u[row] = 0.0, EXACTLY
```

The full vector is allocated zero-filled and only the free rows are written, so
a constrained entry is exactly `0.0` rather than merely small — it was never an
unknown, so no factorisation noise can reach it. Asserted over **every**
constrained degree of freedom of the fixture, through
`SolvedSystem::displacementOf`, with an exact comparison.

And the vector is in **global `DofIndex` order, never the solver's internal
permutation**. `SimplicialLDLT` applies an AMD fill-reducing permutation inside
the factorisation; `solve()` returns the vector in the order it was given,
which is `FreeEquationIndex` order. The test checks the typed accessor and the
raw span agree for every free degree of freedom, so a reordering would break
one of them, and the mutation that shifts the mapping by one is probed.

```text
u entries        metres, SI. No conversion to mm inside the solver
```

## Zero free equations

```text
Nfree == 0   ->   SUCCESS, u = 0
```

Every degree of freedom constrained to zero means `u = 0` is the solution and
there is nothing to factor. Returning a failure would report a fully supported
model as unsolvable, so brief section 27's policy A is taken. **Nothing is
handed to the library**, so no `0 x 0` factorisation is attempted; the pivot
ratio is reported as `1.0`, which is the honest value for a factorisation with
no pivots, and the residual is vacuously zero.

The full residual is still computed, and its constrained entries are the
reactions that balance the applied load — which is exactly what
`P17-REACTION-001` will want from this case. The mutation that removes the
short-circuit and factorises the empty system is probed.

## Source compatibility

```text
checked                                       where
--------------------------------------------------------------------
the settings, on their own terms               validate(SolverSettings)
the constraints against the system's mesh      buildFreeEquationMap, which
                                               has no alternative route
the numbering against the system's dimensions  extractFreeSystem: the
                                               MeshStamp AND the DOF count
```

**Before any factorisation.** The adversarial case is measured: the same model
remeshed gives the *same* degree-of-freedom count and a *different* mesh
identity, so dimensional compatibility proves nothing — and the solve is
refused with `SourceMismatch`.

Staleness beyond that is inherited rather than re-asked. A
`GlobalStructuralSystem` can only exist for a `StructuralModel`, whose
possession is ADR-036's evidence that the geometry and the mesh are current;
and `AssemblySource` carries the body, the control, the geometry revision, the
mesh stamp and the material identity and revision, which `SolvedSystem` carries
forward unchanged. A material edit therefore moves the solution's source, which
is asserted — so an old solution is distinguishable from a current one without
this milestone adding a second currentness mechanism.

The settings participate too: a solution carries the `SolverSettings` it was
produced under, and two solutions with different tolerances compare unequal
even when the numbers agree. That is brief section 123's requirement met
without a new field in `StructuralResultSource`.

## What is deliberately not done

```text
factorisation caching      NOT implemented. A load-only change could reuse the
                           factorisation and deliberately does not: correctness
                           first. The API does not prevent adding it -- the
                           reduced system is a value type that a cache could
                           key on -- and a restraint change would invalidate
                           any such cache, because it changes FreeEquationMap
                           and therefore Kff even when K is unchanged
prescribed displacement    deferred by P17-BC-001, so there is no value field
                           to read and nothing to treat as zero by accident
reaction aggregation       P17-REACTION-001's. The full residual is retained,
                           not summarised
```
