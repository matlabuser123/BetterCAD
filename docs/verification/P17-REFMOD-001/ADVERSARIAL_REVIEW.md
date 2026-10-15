# P17-REFMOD-001 — adversarial review

```text
QUESTIONS:           the brief's, plus the reviewer's own
FINDINGS:            9 -- 3 measured limitations of qualified modules (F1-F3,
                     recorded for the owner), 6 suite defects found during
                     development, all fixed before the freeze
PRODUCTION DEFECTS:  0 corrected; 3 measured and reported (see below)
VERDICT:             PASS, with F1-F3 carried as owner decisions
```

The review ran against the suite as it stands, by the reviewer who wrote it,
with the brief's questions as the attack list and the measurements as the
only admissible answer.

## The attack list, and what each found

**"Could an expected value be coming from the code under test?"**
`tests/reference/Analytic.hpp` includes no BetterCAD header; the structural
section's own opening says so and lists what it does not contain. Every
oracle in `StructuralModelsTests.cpp` is built from `mm(document, name)`,
the resolved material's three numbers (compared EXACTLY with the header's
declared constants first), and the analysis's own load value. The production
`ElasticityMatrix` is handed to `recoverElementFields` because that is the
thing under test; the stress it is compared against is Hooke's law in the
test. **No finding.**

**"Is any test vacuous?"** Eight vacuity guards, each of which would fail if
the instrument measured nothing:

```text
RM-FEA-01   the mesh has an interior node; the boundary winding sum is positive
RM-FEA-03   strictly increasing nodes, tets and free equations BEFORE any error
RM-FEA-06   the first node moved > 1 mm; R e_x is not e_x; every base tet has
            an image in the placed mesh; the interior population is non-empty
RM-FEA-07   both bands are non-empty on every mesh
RM-FEA-09   the mesh is still HELD when the refusal is asked for; the re-solve
            gives a different energy
determinism a changed pressure changes the fingerprint
```

**Finding S1 (suite, fixed).** The first local-refinement draft read
`edgeStatsOf(mesh, band)` on an EMPTY band, which that helper documents as
"every tetrahedron" -- so the band's median was the whole mesh's and the test
would have compared two copies of the same number. `REQUIRE_FALSE(band.empty())`
now precedes every read.

**Finding S2 (suite, fixed).** RM-FEA-07's first geometry, 20 x 30 x 100 mm,
refined NOTHING at 4, 5 or 10 mm: the slab control's size-tree boxes did not
fit inside the section. Measured through the CLI on saved documents and
through the runner; the model is 40 x 60 x 100 and the builder records the
numbers. A first reading of the CLI experiment attributed 43 nodes to 30 x 40
because the variant script substituted `0.02 -> 0.03` and then `0.03 -> 0.04`;
the runner's own figure for 30 x 40 is 14, and the record was corrected rather
than left.

**"Does the rigid transform actually test invariance?"** It did not, at
first: **Finding S3 (suite, fixed).** An 80 x 50 x 30 block on a frame of this
suite's own gave the same eight corners two DIFFERENT tetrahedralisations in
its two placements, and a 0.4% energy difference. A band loose enough to pass
would have passed a convention error too. RM-FEA-06 now uses RM-MESH-06's
block, sizing and placement -- the pair P17-POST-001 measured to give
bit-identical invariants -- and the test asserts element-by-element
correspondence through the node map before it compares anything. The band is
then set by the one interior node, measured at 2.66e-07 m from its image, at
1e-5 against measured differences of 1e-6.

**"Are the two node populations judged separately?"** Yes, with the
interior population required non-empty (one node), its gap recorded
separately (2.66e-07 m against 1.56e-17 m for the boundary), and the
displacement-vector error reported per population (1.09e-06 boundary,
1.59e-06 interior). **No finding.**

**"Does the convergence study prove the levels differ before it compares?"**
Yes, as P16-REFMOD-001 required after its one-ULP table: strictly increasing
nodes, tets and free equations are `REQUIRE`d first.

**"Is the convergence claim honest?"** It claims a monotone trend and a 1.2x
margin, measured 1.27x. It does NOT claim agreement with beam theory, because
the measured ratios are 3.8%, 4.7%, 4.8% of Timoshenko -- **Finding F2**
(linear Tet4 locks in bending) -- and because the reason is structural:
**Finding F3**, OCCT never subdivides a developable wall along its ruling, so
every wall triangle spans the full length at every deflection and the wall
tetrahedra are needles. F3 is asserted as a measurement (longest boundary edge
> 0.99 L at every level) so a future surface mesher that fixes it will be
noticed by this suite. The mid-span moment instrument is reported as noisy on
such meshes (0.44, 0.54, 0.44 N m against 10) and only its sign and shortfall
are gated.

**"Was any tolerance loosened to pass?"** One tolerance is stated rather than
defaulted, and it is the review's central finding. **Finding F1.** The
production moment-equilibrium threshold, 1e-12 normalized, is crossed by a
correct bending solve: `eta_M` 1.15e-12 at the 2465-element level and
1.27e-12 for the committed cantilever lengthened to 120 mm, against 5.98e-13
for the committed geometry. The imbalance is the factorisation's forward error
on needle elements (pivot ratios 5e-4 to 7e-4); the reactions still balance
the applied load to 1e-9, asserted. The suite solves the convergence LEVELS
under `EquilibriumTolerance{1e-9, 1e-9}`, says so in the test's opening
comment, records `eta_M` at every level and asserts the committed level under
the production default; the runner's model-change edit for the cantilever
SHORTENS it so the production path stays under the default. The threshold
itself was not touched: P17-REACTION-001 set it from measurement and changing
it is an ADR for the owner, with the new measurement as the input. A
reviewer should read this as "the suite found the edge of a qualified
threshold and reported it", which is what a reference suite is for.

**"Could the CLI fixtures pass against the wrong numbers?"** The axial bar's
largest displacement and von Mises are pinned to the closed form's digits
(`2.15354e-05 m`, `50000000 Pa`); the pressure block's and the column's
`totalForce.z` to `5400` and `10.77` N; the refusals to the named codes with
nothing on stdout. **Finding S4 (suite, fixed).** The first regexes assumed a
one-line quantity layout and a `stale_geometry` currency spelling; the CLI
prints quantities over four lines and the currency as `stale: the geometry
changed`. Both regexes now match the real bytes, and the runner's line count
assumption was corrected when the sizing line was added. A regex that does
not match is a test that was never run against the real output; both were.

**"Does the runner really refuse a stale mesh for every model?"** It requires
`stale: the geometry changed`, a `rejected` validation with `mesh_stale` and a
refused solve after every main-dimension edit, then remeshes and re-solves.
That is RM-FEA-09's protection exercised ten times with ten different
geometries, not once.

**"Is RM-FEA-08 really refused by the SOLVER and not by the count?"** The
test requires validation ACCEPTED with exactly 8 constrained degrees of
freedom, then `structuralSolveProblem == SingularSystem` through the public
stages, then the production entry refusing with "singular" in its message.
One restraint fewer is shown to be refused by the count instead, and a fixed
base is shown to solve. **No finding.**

**"Is RM-FEA-10 refused without rebinding?"** `boundaryFacetsOf` on the
missing face returns `Unresolved` with zero facets while the real faces
return two each; `structuralLoadProblem == TargetUnresolved`; the restraint
stage PASSED (12 constrained DOFs) before the load stage refused. **No
finding.**

**"Could a save silently carry a result?"** The bytes are checked for six
words; the reloaded mesher holds nothing; the re-solve is bit-identical. **No
finding.**

**"Is anything in a production module changed?"** `git status` shows no file
under `src/`, `include/` or `apps/`; the fingerprints in FREEZE.md prove it.
**No finding.**

**Finding S5 (suite, fixed).** The cantilever's first model-change value,
120 mm, made the runner's re-solve fail on F1. Rather than hide F1 by raising
anything, the edit was changed to shorten the beam (80 mm) and F1 was written
up with the 120 mm measurement as part of its evidence.

**Finding S6 (suite, fixed).** The local-refinement gates were first written
as "the untouched band has no more than twice the baseline's elements";
measured, Netgen's grading re-tetrahedralises the far end (6 and 14 against
2) while leaving its SCALE coarse (55.9 and 40.0 against 61.6 mm). The gates
are on the scale and on the contrast between bands, at a half, with the
measurements in the comment.

## What the review did not do

It did not attempt to construct a `StructuralValidationReport` by hand to
test a formatter, and it did not add a stale-mesh CLI fixture, for the reason
P17-CLI-001's failure matrix gives: that state is unreachable through a
process that regenerates and meshes before it solves.
