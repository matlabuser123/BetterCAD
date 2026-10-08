# P17-ASSEMBLY-001 — mutation protection

```text
14 probes applied, 11 KILLED, 3 SURVIVED
```

Two of the three initial survivors were **test defects that the probes found**,
and both were fixed and then killed. The three that remain are inert, and each
is inert for a reason that is itself a property worth recording.

## Method

Each probe restores the two production files from hash-verified pristine
copies, applies **one** verified literal substitution (the harness dies if the
anchor is missing or ambiguous), rebuilds, runs the assembly selection, and
restores. The run ends with a restore build — a restored source is not a
restored binary.

```text
filter      StructuralSystem_       26 tests, counted with ctest -N first
harness     .../scratchpad/asmmut/run.sh
logs        .../scratchpad/asmmut/results.txt, results-again.txt
```

### One result was VOID, and it is recorded rather than quietly reused

M1's first run reported `KILLED-BY-COMPILER` with `collect2.exe: ld returned
1 exit status` — a **linker** error, which replacing `+=` with `=` cannot
cause. The cause was a collision: the test binary was open in an interactive
run while the harness relinked it. That is the known signature of a collided
build and it makes the result meaningless in either direction.

M1 was re-run on an idle build root and is **killed by nine tests**. The void
result is kept in the log so the correction is visible.

## Results

```text
#    Mutation                                   Result      Killed by
----------------------------------------------------------------------------
M1   stiffness overwritten, not summed          KILLED      9 of 26
M2   one local row dropped                      KILLED      9 of 26
M3   local row and column swapped               SURVIVED    inert: Ke is symmetric
M4   raw NodeId arithmetic for the row          KILLED      24 of 26
M5   element order recorded reversed            KILLED      4 of 26
M6   duplicate pattern not uniqued              KILLED      5 of 26
M7   only the upper triangle assembled          KILLED      12 of 26
M8   K post-symmetrised                         KILLED      1 of 26  (after a
                                                            detector was added)
M9   diagonal regularisation added              KILLED      8 of 26
M10  one element skipped                        KILLED      11 of 26
M11  load overwritten, not summed               SURVIVED    inert: P17-LOAD
                                                            accumulates per node
M12  load source not checked                    KILLED      2 of 26
M13  element refusal skipped                    SURVIVED    unreachable
M14  a read past the end wraps instead of
     answering zero                             KILLED      1 of 26  (after the
                                                            test was fixed)
```

## The kills that matter most

```text
M1   values[slot] += Ke(a,b)  ->  =
     The duplicate-accumulation defect this milestone exists to prevent, and
     the brief's first automatic failure. Nine kills, including the dense
     oracle, the contributor-group sums, the energy identity, the internal
     force, the nullity and BOTH rigid-mode tests -- because an overwritten
     matrix is no longer the sum of element stiffnesses and therefore no longer
     has the rigid-body null space.

M4   the row from kDofsPerNode * nodeId.value() + c instead of
     FreeEquationMap::equationOf
     Correct only for a dense, zero-based, gap-free handle space -- which a
     mesh's NodeIds are not. TWENTY-FOUR of 26 tests fail, the broadest kill in
     the set, which is what ADR-037's "the value that comes out must BE the
     row" buys.

M7   for b = a instead of b = 0: only the upper triangle, no mirror
     Twelve kills. The entry that has no counterpart is not merely asymmetric,
     it is absent, and the pattern check, the oracle, the energy and the rigid
     modes all see it.

M9   the diagonal scaled by 1.000001 -- a regularisation, which the brief lists
     as an automatic failure
     Eight kills. The rigid-body modes are the detector: a regularised matrix
     has no null space, which is the whole point of adding one and the whole
     reason it must not be added here.

M10  break after two elements
     Eleven kills. A softer body that would have solved successfully.

M6   the symbolic pattern not deduplicated
     Five kills, through the CSR invariants: a repeated inner index within a
     row IS a duplicate structural entry, and the strictly-ascending assertion
     catches it before any value is compared.
```

## The two defects the probes found

### M8 — the suite could not see a post-hoc symmetrisation

The probe inserts an averaging pass over `(i,j)` and `(j,i)` before
validation. It **survived**: the two values already agree to 1e-17, so
averaging them changes nothing the oracle, the energy, the rigid modes or the
symmetry bound measure.

That is the brief's automatic failure "K is symmetrised after assembly to hide
error" being invisible, which is a real gap: a future change could add the
averaging to conceal a genuine scatter bug.

**The detector added.** A symmetrised matrix comes out *exactly* symmetric on
every mesh; an untouched one cannot, when its elements are asymmetric at the
ulp level and several of them write each entry. So the test now asserts the
global symmetry error is strictly **positive**, with both premises asserted
first — and it is careful about where: RM-MESH-01, at six tetrahedra, has an
error of exactly zero through legitimate cancellation, so the detector lives on
the block fixture and the reference models only bound the error from above.

With the detector in place M8 is killed.

### M14 — the out-of-range read was checked on a vector of zeros

`ForceVector::operator[]` answers zero past the end. The probe makes it wrap
to `row % size` instead. It **survived**, because the test asked for
`force()[Ndof]` on an **unloaded** model, where row 0 is zero too — so
wrapping returned zero and passed.

**Fixed**: the check now runs on a vector with a non-zero row 0, which makes
wrapping distinguishable from answering zero, and tests three out-of-range
positions. The probe is now killed.

Both of these are the same family as P17-BC-001's D4: an assertion whose
subject was not what it appeared to be. Finding them is what the probes are
for.

## The three survivors

```text
M3   Ke(a,b) read as Ke(b,a) in the scatter.

     INERT BECAUSE Ke IS SYMMETRIC. Transposing the local read gives K^T, and
     K^T = K to within the ulp-level element asymmetry measured in
     ANALYTICAL_VALIDATION.md. No test can distinguish them and none should:
     they are the same matrix.

     The asymmetric mis-pairings ARE caught -- M2 drops one local row and is
     killed by nine tests, M7 assembles one triangle and is killed by twelve --
     so what survives here is specifically the transposition that is a no-op,
     not a scatter error.

M11  force[row] += component  ->  =

     INERT BECAUSE OF P17-LOAD'S CONTRACT. PreparedLoads::nodal() holds ONE
     entry per node, ascending and without repeats, so the assembly never sees
     two contributions to one row: the three components of one node go to three
     different rows, and two loads on one node were already summed by
     P17-LOAD. The `+=` is defensive.

     That changes the meaning of brief section 23's test, and the test now says
     so: the end-to-end claim (+10 and -3 reach F as 7) is what it proves, and
     the contract the `+=` relies on is pinned by its own assertion -- if
     `nodal()` ever stopped being unique by node, the `+=` would become
     load-bearing and this probe would start killing.

M13  the element-refusal check disabled

     UNREACHABLE. Possession of a StructuralModel is ADR-036's evidence that
     the mesh came from the mesher, so no degenerate, inverted or non-finite
     element can reach assembly, and a hand-built mesh carrying one cannot
     become a StructuralModel. The branch is kept because a mesh arriving from
     `io`, or a mesher backend change, would make it live -- and an assembly
     that skipped a bad element would publish a softer body with nothing
     reporting it.

     The survival is the measurement that confirms the unreachability the
     diagnostics matrix records. Same disposition as P17-BC-001's M11 and
     P17-ELEM-001's M7 and M8.
```

## One of the brief's mutations is not expressible

```text
"narrow sparse index type"
    the index is std::uint64_t and the overflow bound is a compile-time proof
    in the header, not a runtime branch. Narrowing it to 32 bits would compile
    and would not overflow at any size this suite can mesh -- the largest
    fixture has nnz = 102 276 against a 32-bit limit of 4.29e9 -- so the probe
    would survive for a reason that says nothing about the code. The
    static_assert is the evidence instead, and -Wconversion -Werror is what
    stops a narrowing slipping in silently.
```

And two more are covered under different names:

```text
"iterate elements through an unordered container"   M5 (the order is recorded
                                                    and asserted against the
                                                    mesh's own)
"drop shared-node load accumulation"                M11
"reuse M1 prepared load vector on M2"               M12
"use raw NodeId arithmetic"                         M4
"ignore duplicate sparse entries"                   M1, M6
"assemble only upper triangle but forget mirror"    M7
"post-symmetrise bad K"                             M8
"add diagonal regularisation"                       M9
"skip one element"                                  M10
"replace += with ="                                 M1
"drop one local row/column"                         M2
"swap local row/column DOF mapping"                 M3
```

## Restoration verified

```text
source hashes after the run match pristine, both files
restore build OK
100% tests passed out of 26, 37.18 s
```
