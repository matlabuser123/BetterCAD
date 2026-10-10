# P17-REACTION-001 — determinism

```text
RESULT: PASS, and MEASURED rather than asserted
```

Reaction aggregation is a floating-point reduction over a hundred or more
terms, and moments multiply coordinates by forces before summing. Both are
order-sensitive, so the traversal order is part of the contract.

## What could have been non-deterministic, and how each is closed

```text
risk                                   how it is closed
---------------------------------------------------------------------------
unordered iteration over constrained   ConstraintSet::constrained(), ascending
  degrees of freedom                   and unique BY CONSTRUCTION
unordered iteration over nodes         the mesh's own enumeration, ascending by
                                       NodeId, which P16 guarantees
unordered iteration over restraints    the order they were given to
                                       prepareStructuralRestraints
a hash map for the multiplicities       THERE IS NONE. The multiplicity table
                                       is a SORTED VECTOR looked up by binary
                                       search -- see below
summation order of the moment           frozen with the node order; every
                                       (x - O) x F is accumulated in ascending
                                       NodeId
parallel reduction                     none. Serial, deterministic aggregation
wall clock, locale, threading          none used
```

### The multiplicity table is a sorted vector on purpose

Deciding which reactions are "owned" and which are "shared" is what routes each
one into a bucket that is then **summed**. A hash table's traversal order would
therefore reach the floating-point result. So the table is a
`std::vector<std::size_t>` parallel to `ConstraintSet::constrained()`, and the
lookup is `std::ranges::lower_bound` over the ascending unique handles.

A source search for `unordered_map` or `unordered_set` in
`src/structural/StructuralReaction.cpp` returns **nothing**.

## Within one run: bitwise, five repeats

`StructuralReaction_IsDeterministicAcrossRepeatedRecoveries`, on the
**overlapping** two-support fixture — chosen because it exercises the
multiplicity classification, the owned and shared buckets, and both balances.

```text
compared, per repeat
    every SupportReaction          node, Force3D, constrained mask
    every RestraintReaction        owned and shared counts, forces, moments
    forceBalance()                 external, reaction, imbalance, both norms,
                                   scale, normalized
    momentBalance()                origin, external, reaction, imbalance, both
                                   norms, scale, normalized
    sharedForce(), sharedMoment()
    source()

repeats     5
result      BITWISE IDENTICAL, all five
```

`operator==` on both aggregates is `= default` over `Quantity`, `double`,
`meshing::NodeId` and `RestraintComponents`, so this is an **exact**
comparison of every channel — not a tolerance. It is what proves no sum
crossed an unordered container.

The restraint summaries are additionally asserted to be in the order the
restraints were given: `RestraintId(1)` then `RestraintId(2)`.

## Ordering, asserted on real output

Not inferred from P16's guarantee but checked against it:

```text
nodal channel      is_sorted by NodeId                       PASS
                   adjacent_find finds no duplicate          PASS
                   every reported node has a non-empty mask  PASS
                   constrainedDegreesOfFreedom ==
                     nodal().size() * 3, for a fixed support PASS
```

`is_sorted` alone permits repeats, which is why `adjacent_find` is there too.
The cardinality identity is what proves one node cannot contribute a component
twice — the risk a second loop over nodes would have introduced.

## Across presets

```text
EXACT IDENTITY expected, and asserted in each preset independently:
    NodeId ordering of the reaction channel
    RestraintId ordering of the summaries
    the constrained mask at every reported node
    the EXACT zeros at unconstrained components of a partial support
    reaction cardinalities, including sharedDegreesOfFreedom
    the fully constrained case's imbalance of EXACTLY zero

NUMERICAL EQUIVALENCE required, and asserted in each preset independently
against the SAME independent oracles:
    R_total vs the applied load                       1e-9 rel
    the partition formula                             1e-14 rel
    the analytical pressure resultant                 1e-9 rel
    the analytical weight rho V g                     1e-9 rel
    the analytical gravity moment c x W               1e-9 x scale
    the test's own moment accumulation                1e-9 x scale
    the origin-shift relation                         1e-9 x scale
    assembled F vs PreparedLoads::resultantForce()    1e-12 rel
    eta_F, eta_M                                      <= 1e-12
```

**BITWISE CROSS-PRESET EQUALITY IS NOT CLAIMED.** No preset's reaction channel
was carried to another run and compared byte for byte. What is claimed is that
every preset independently reproduces the same properties against the same
independent oracles within the bounds above, and that within a run five repeats
are bitwise identical — the same position P17-SOLVE-001 and P17-POST-001 took.

## Compensated summation: considered and not used

Brief 65 asks for an explicit decision. The measured accumulation error is

```text
worst eta_F   1.48e-14   (large mesh, 850 load nodes + 100 reaction entries)
worst eta_M   2.50e-14
```

against a `1e-12` gate — a 40x margin on the largest fixture measured. Ordinary
deterministic summation satisfies BetterCAD's guidance comfortably, so **no
compensated summation is introduced**. Adding Kahan or pairwise summation would
be an optimisation without a measured need, and it would make the traversal
harder to reason about for no gain.

If a future mesh an order larger erodes that margin, the measurement in
[EQUILIBRIUM_TOLERANCE.md](EQUILIBRIUM_TOLERANCE.md) is the baseline to
compare against, and compensated summation is the first thing to try — before
loosening the gate.

## Repeat stability under `--repeat`

```text
ctest -R "StructuralReaction_|compile_fail.structreaction|StructuralBC_"
      --repeat until-fail:5 -j 8
59 tests, 100%, 295.33 s
```

Run before the freeze, every test five times back to back. `ctest --repeat` is
**per test** and fixtures do not re-run between passes, so a mutating test
would show up here; none does, because every fixture builds its own document.

P17-BC-001 is in that selection deliberately: its shared code changed, so its
32 tests were repeated alongside the new ones.
