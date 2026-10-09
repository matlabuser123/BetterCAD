# P17-POST-001 — determinism

```text
RESULT: PASS, and MEASURED rather than asserted
```

The brief's section 102 asks for a measurement and not a claim, so this file
records what was compared, how, and what came out.

## What could have been non-deterministic

```text
risk                                   how it is closed
---------------------------------------------------------------------------
unordered iteration over nodes         the mesh's own enumeration. P16
                                       guarantees ascending NodeId with no
                                       unordered container anywhere
unordered iteration over elements      likewise, ascending ElementId
a hash map keyed by handle             there is none in this module
the eigensolver's output order         not relied on: the three values are
                                       SORTED descending afterwards, so the
                                       library's order cannot reach a result
eigenvector sign ambiguity             no eigenvectors are computed
summation order                        no reduction crosses elements. Every
                                       value is a function of ONE element's
                                       twelve displacements; the only
                                       cross-element quantities are two
                                       maxima, and a maximum is
                                       order-independent for finite values
wall clock, locale, threading          none used. The eigensolve is
                                       single-threaded Eigen on a 3x3
```

## Within one run: bitwise, five repeats

`StructuralPost_IsDeterministicAcrossRepeatedRecoveries`, on the solved block
fixture:

```text
compared, per repeat          every NodalDisplacement, with operator==
                              every ElementFields, with operator==
                              largestDisplacementMagnitude()
                              largestVonMises()
                              source()

repeats                       5
result                        BITWISE IDENTICAL, all five
```

`operator==` on both aggregates is `= default` over `Quantity` and `double`, so
this is an **exact** comparison of every channel — including the six strains,
the six stresses, the three principal stresses, the three principal strains,
von Mises and the hydrostatic stress. It is not a tolerance.

## On a reference model: a fingerprint over every ordered channel

`StructuralPost_IsDeterministicOnAReferenceModel`, on RM-MESH-01.

The encoding is the canonical order, hashing each double's own bit pattern
rather than a rounded form:

```text
for each node, ascending by NodeId
    NodeId, ux, uy, uz, |u|
for each element, ascending by ElementId
    ElementId
    six strains      in TensorComponent order
    six stresses     in TensorComponent order
    sigma1 e1  sigma2 e2  sigma3 e3
    von Mises
    hydrostatic stress
```

```text
fingerprint        0xc30f7c078f350b3e
over               8 nodes and 6 elements
repeats            5
result             IDENTICAL, all five
```

### The fingerprint is not a constant

A load of 9000 N in place of 5000 N must give a **different** fingerprint, and
the test requires it. Without that guard the equality above would hold for any
encoding at all, including one that hashed nothing — which is the shape of
vacuous instrument this project has recorded four times.

```text
F = 5000 N     0xc30f7c078f350b3e
F = 9000 N     different, asserted
```

It is a **test-side** encoding. It is never persisted and is not engineering
authority: a recovered field is derived state (ADR-030's rule for a mesh,
applied to a result by P17-DATA-001).

## Ordering, asserted on real output

Not inferred from P16's guarantee but checked against it:

```text
nodal channel      is_sorted by NodeId                        PASS
                   adjacent_find finds no duplicate           PASS
                   entry i == mesh.nodes()[i].id, all i       PASS
                   count == mesh.nodes().size()               PASS

element channel    is_sorted by ElementId                     PASS
                   adjacent_find finds no duplicate           PASS
                   entry i == mesh.tetrahedra()[i].id, all i  PASS
                   count == mesh.tetrahedra().size()          PASS
```

`is_sorted` alone permits repeats, which is why `adjacent_find` is there as
well.

## Across presets

```text
EXACT IDENTITY expected, and asserted in each preset independently:
    NodeId order
    ElementId order
    result cardinalities
    the exact zeros at restrained degrees of freedom
    the descending principal order

NUMERICAL EQUIVALENCE required, and asserted in each preset independently
against the SAME oracles:
    strain          vs the analytic derivative          1e-10
    strain          vs an independently fitted gradient 1e-9
    stress          vs an independent Dref              1e-10
    von Mises       vs the deviatoric invariant         1e-12
    von Mises       vs the principal-stress form        1e-10
    principal       vs Cardano's closed form            1e-10
    axial mean      vs F/A                              1e-9
    magnitude       vs std::hypot                       1e-18 absolute
```

**BITWISE CROSS-PRESET EQUALITY IS NOT CLAIMED.** No preset's recovered field
was carried to another run and compared byte for byte. What is claimed is that
every preset independently reproduces the same properties against the same
independent oracles within the bounds above, and that within a run five
repeats are bitwise identical. Saying more than that would be a claim no
measurement here supports — the same position P17-SOLVE-001 recorded.

## Repeat stability under `--repeat`

```text
ctest -R "StructuralPost_" --repeat until-fail:5
```

Run before the freeze, with every test executing five times back to back.
Because `ctest --repeat` is **per test** and fixtures do not re-run between
passes, a mutating test would show up here; none does, because every fixture in
this milestone builds its own document.
