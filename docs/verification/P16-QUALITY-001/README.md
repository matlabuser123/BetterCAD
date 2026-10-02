# P16-QUALITY-001 — Mesh Quality Metrics / Validation

```text
STATUS:   PASS
TASK:     P16-QUALITY-001 -- mesh quality metrics, classification and report
PHASE:    P16 -- Meshing
DATE:     2026-10-02
```

BetterCAD defines its own mesh quality metrics: a formula, a range, an ideal
value and a direction for each, written down **before any threshold exists**.
A threshold is a separate, policy-level choice that cannot change a computed
number, and BetterCAD ships none — because a quality threshold is a solver
requirement and no solver exists yet.

```text
Mesh -> evaluateMeshQuality -> MeshQualityReport
 const        observes only        per-element metrics, per-metric summaries,
                                   classified findings, the structural verdict
```

## Documents

```text
METRIC_DEFINITIONS.md     every metric, its range, its ideal and its
                          direction; the validity/quality boundary; the
                          threshold policy and why it is empty
ANALYTICAL_REFERENCES.md  the closed forms, derived by hand, and the
                          tolerances with their derivations
QUALITY_RESULTS.md        what the metrics measured about real BetterCAD
                          meshes, including a finding about the surface mesh
ADVERSARIAL_REVIEW.md     3 defects found and fixed, 6 limitations carried,
                          24-mutation table
qualification/            the logs, and the mutation harness
```

## Baseline

```text
branch        main
HEAD at start 1ec54fc  BetterCAD: add canonical mesh sizing controls
tree          345e34f14769648e287ed2ef61f147c80ab92958
origin/main   1ec54fc  (HEAD == origin/main)
working tree  clean
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
backend       Netgen 6.2.2604, reported by the library itself
build root    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive)
```

## Prerequisites

Verified from the committed evidence, not assumed:

```text
P16-ARCH-001      25/25 ticked, RESULT PASS      evidence at 9964f88
P16-DATA-001      26/26 ticked, RESULT PASS      evidence at 20b04b9
P16-GEOM-001      20/20 ticked, RESULT PASS      evidence at 9c755e8
P16-SURF-001      21/21 ticked, RESULT PASS      evidence at 4005815
INFRA-NETGEN-001  RESULT PASS                    evidence at eaf7da4
P16-VOL-001       19/19 ticked, RESULT PASS      evidence at e9fd82c
P16-SIZE-001      21/21 ticked, RESULT PASS      evidence at 1ec54fc
```

## Scope

Implemented: the metric definitions, their computation, the classification
machinery, the structured report, and the analytical and production validation
of all of it.

Not implemented, deliberately:

```text
a mesh optimiser or any repair     the milestone's hard rule is "bad Tet ->
                                   report bad Tet", and the type system
                                   enforces it (4 compile-failure cases)
numeric quality thresholds         a solver requirement. P17 owns it.
geometry/mesh correspondence       P16-MAP-001. Not started, and not
                                   authorized by this document.
commands, persistence, CLI, GUI    P16-CMD-001, P16-PERSIST-001, P16-CLI-001
```

## The boundary this milestone keeps

```text
STRUCTURAL   P16-DATA-001's validate(), reused unchanged and run FIRST
             an unresolvable handle, a repeated handle, a non-finite
             coordinate, a ZERO or NEGATIVE signed volume, a duplicate
             tetrahedron, a node shared between regions, an empty mesh.
             The mesh does not describe a body. ALWAYS INVALID.

QUALITY      here
             aspect ratio, radius ratio, dihedral angles, edge distribution,
             triangle shape. A valid discretisation that may solve badly.
             A matter of degree, and of a threshold someone chose.
```

Both directions are asserted, and both matter:

```text
a mirrored regular tetrahedron    REFUSED as inverted. Identical edges, face
                                  areas and unsigned dihedrals to the
                                  original -- perfect by every shape metric.
a 1.98 deg sliver                 ACCEPTED, structurally valid, every metric
                                  measured and reported.
```

There is **no `abs()` anywhere** in `src/meshing/MeshQuality.cpp`: taking one
would make an inverted element indistinguishable from a correct one, and the
mutation that inserts one is killed.

## Metrics

Full statement in [METRIC_DEFINITIONS.md](METRIC_DEFINITIONS.md).

```text
Tet4                              range        ideal     direction
signed volume V                   V > 0 valid  --        ContextOnly
det(J) = 6V, exactly              --           --        ContextOnly
min / max / mean edge length      (0, inf)     --        ContextOnly
aspect ratio  l_max / l_min       [1, inf)     1         LowerIsBetter
radius ratio  3r / R              (0, 1]       1         HigherIsBetter
inradius r = 3V/A                 (0, inf)     --        ContextOnly
circumradius R                    (0, inf)     --        ContextOnly
min internal dihedral             (0, pi)      acos(1/3) HigherIsBetter
max internal dihedral             (0, pi)      acos(1/3) LowerIsBetter

Triangle3
area                              (0, inf)     --        ContextOnly
min / max / mean edge length      (0, inf)     --        ContextOnly
shape quality 4 sqrt(3) A/sum l^2 (0, 1]       1         HigherIsBetter
min interior angle                (0, pi)      pi/3      HigherIsBetter
max interior angle                (0, pi)      pi/3      LowerIsBetter
```

**`ContextOnly` is a third direction, not a missing one.** A dimensioned size
is summarised for context and never classified: "is this volume good?" has no
scale-free answer, so a threshold on it would be a threshold on the model's
units. `validate(QualityThresholds)` refuses one.

### The dihedral convention, stated because confusing it is the classic error

```text
acos( 1/3) =  70.5288 deg    the INTERNAL dihedral -- what BetterCAD reports
acos(-1/3) = 109.4712 deg    the angle between the OUTWARD NORMALS
```

The regular tetrahedron's test asserts the first **and** asserts the result is
nowhere near the second, so an implementation reporting the supplement fails
there rather than somewhere downstream. The mutation that drops the minus sign
is killed.

### Why two shape metrics, measured rather than asserted

```text
a 10 mm equilateral base, flattening:
   apex 10.0 mm   aspect 1.1547    3r/R 0.977
   apex  1.0 mm   aspect 1.7066    3r/R 0.0849
   apex  0.1 mm   aspect 1.7318    3r/R 0.000899
```

**The aspect ratio saturates at sqrt(3) = 1.732 while the radius ratio
collapses by three orders of magnitude.** `l_max/l_min` catches stretching and
cannot see a sliver; `3r/R` catches slivers. Shipping one alone would report a
1.98° sliver as a 1.73. Pinned by a test.

`l_max / h_min` was considered and rejected: its ideal value is
sqrt(3/2) = 1.2247, and an "aspect ratio" whose perfect score is 1.22 is the
convention mismatch the definitions document exists to prevent.

## Independent validation

No expected value came from running BetterCAD. Every figure was derived by
hand and confirmed by a separate double-precision evaluation written from the
formulas. Full tables in
[ANALYTICAL_REFERENCES.md](ANALYTICAL_REFERENCES.md).

**Two reference tetrahedra, because one is not enough.** Too many of a regular
tetrahedron's metrics are 1, and several wrong formulas reproduce it. The
cube-corner tetrahedron disagrees on every shape metric, and its dihedral
range straddles the regular one's single value — 54.74° below, 90° above.

```text
                         regular (edge a)      corner (legs a)
V                        sqrt(2)/12 a^3        a^3/6
aspect l_max/l_min       1                     sqrt(2)
inradius 3V/A            sqrt(6)/12 a          (3 - sqrt(3))/6 a
circumradius             sqrt(6)/4 a           sqrt(3)/2 a
radius ratio 3r/R        1                     sqrt(3) - 1
dihedrals                all acos(1/3)         acos(1/sqrt(3)) x3, pi/2 x3
```

Equilateral and right-isoceles triangles do the same for the surface metrics
(`q = 1` and `q = sqrt(3)/2`).

**And on the production path.** A 40 mm cube meshes as 12 congruent
tetrahedra — OCCT's two triangles per planar face joined to one interior node
— whose closed forms are `V = a^3/12`, `aspect = sqrt(8/3)`,
`r = a/(2 + 3 sqrt(2))`, `R = 3a/4`, `3r/R = 4/(2 + 3 sqrt(2))`, dihedrals
`pi/4` and `2pi/3`. BetterCAD matched every one, over **every** element, and
the twelve volumes sum to the cube. This is validation of the real
CAD → surface → Netgen → metrics chain, which the synthetic references cannot
give.

## A finding about BetterCAD's meshes

**The first thing the metrics did was reveal that BetterCAD's curved-body
meshes contain near-degenerate elements.** Reported, not hidden, and no
threshold was chosen to make it go away.

```text
body                                worst 3r/R   worst dihedral    worst tri q
40 mm block, default sizing          0.640754     45 to 120 deg      0.866025
60x60x20 bored 20 mm, 8 mm target    0.032153      1.23 to 176.10    0.037152
cylinder r6 h20, 3 mm target         0.000451      0.26 to 176.89    0.090328
hollow tube r10/r6 h20, 3 mm         0.013208      2.90 to 167.66    0.090328
cylinder, 6 mm + 1.5 mm local        0.005122      2.21 to 175.60    0.090328
```

A tetrahedron must conform to the boundary triangles it is built on, so a thin
boundary triangle forces a thin element. For the bored block the chain shows in
one number: the volume mesh's worst dihedral is **1.230616°** and the
boundary's worst triangle angle is **1.230616°** — the same angle. The owner is
`P16-SURF-001`'s deflection controls and, for a requirement, `P17`.

Not one invalid element in 2163 tetrahedra and 952 triangles across six
bodies, so these are genuinely **valid and badly shaped** — which is the
distinction this milestone exists to make.

## Threshold policy

```text
SEPARATE FROM COMPUTATION   a policy classifies and can never change a
                            number. One mesh through a lenient and a strict
                            policy: IDENTICAL metrics, different verdicts.
COMPARISONS ARE STRICT      a value exactly ON a bound is on the GOOD side.
                            Pinned with std::nextafter -- nothing exists
                            between the two cases.
FAILURE BEFORE WARNING      an element past both bounds is reported once, at
                            its worst; the counts are per element while the
                            findings are per observation.
ABSENT BOUND                reported, not classified. Said by std::optional,
                            never by a sentinel.
REFUSED                     a bound on a dimensioned size; a non-finite
                            bound; a failure bound on the GOOD side of its
                            warning. And a contradictory policy handed to
                            evaluateMeshQuality classifies NOTHING, is
                            reported, and does not produce a pass.
```

### BetterCAD ships no thresholds, deliberately

Quality thresholds are solver requirements and `P17` owns them. Inventing
numbers now would be fabricating engineering judgement and dressing it as a
default — and the obvious way to pick them, choosing values that make the
existing reference bodies green, is exactly the failure the milestone brief
forbids. The curved-body figures above are the reason that matters: a
threshold set to pass them would have been set to pass a 0.26° dihedral.

So P16 ships the metrics, the machinery and the semantics, and ships no
opinion. `satisfiesPolicy()` therefore reduces to structural validity until
someone sets a policy, which is the honest answer rather than a gap.

## The report

```text
MeshQualityReport {
    structural            P16-DATA-001's verdict, run FIRST and reported as is
    structurallyValid
    tetCount, triangleCount
    valid/warning/failure/invalidElements      sum to the element count
    tets, triangles       per-element metrics, ascending ElementId
    summaries             per metric: count, min, max, mean, WORST ELEMENT
    findings              every classified observation, most severe first
    thresholds            the policy, recorded
    thresholdPolicyError  what was wrong with it, when something was
    satisfiesPolicy()
}
```

**The worst element is per metric, and there is no overall score.** A single
weighted ranking would need weights nobody can justify and would hide failure
modes. Demonstrated: of a cube-corner tetrahedron (aspect 1.41, max dihedral
90.00°) and a spike (aspect 5.03, max dihedral 86.70°), the worst aspect ratio
is the spike and the worst maximum dihedral is the corner. A `ContextOnly`
metric has no worst and says so with an invalid handle.

## Undefined is not "bad"

An infinity is never clamped into a plausible-looking score. Three reachable
routes, all tested:

```text
volume overflows            a body 1e200 m across          REFUSED
a face's area overflows     a body 1e90 m across           REFUSED
circumradius overflows      a 1 m base with its apex       defined == false,
                            1e-200 m above it: R = 1.7e199 counted Invalid,
                                                           contributes NO
                                                           samples, and the
                                                           finding NAMES
                                                           tet_circumradius
```

The third is structurally **valid** — finite coordinates, positive signed
volume — so it is the quality layer's own undefined case and not a restatement
of P16-DATA-001's.

## This layer observes

It never moves a node, reorders connectivity, drops an element or returns a
repaired mesh. A runtime check compares the mesh before and after evaluation
(including an inverted element an optimiser would be tempted to repair), and
four compile-failure cases show that no implementation behind this API could:

```text
compile_fail.meshquality.evaluate-mesh-through-a-mutable-reference
compile_fail.meshquality.evaluate-tet-through-a-mutable-reference
compile_fail.meshquality.reach-the-mesh-through-the-report
compile_fail.meshquality.repaired-mesh-from-a-report
```

## Determinism

The entire `MeshQualityReport` compared as a value — per-element metrics,
summaries, findings, their order, the structural report and the recorded
policy — identical over five evaluations of a synthetic mesh and three of a
production one. Nothing is built from an unordered container; elements are
visited in ascending `ElementId`, summaries are keyed in a `std::map`,
findings are sorted by a total order, and worst-element ties break to the
lower `ElementId`.

## Adversarial review

**Three defects found, all fixed and re-verified.** Full account in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
1  a structural finding carried a FABRICATED metric and value -- TetVolume and
   0.0 for an element whose volume is NEGATIVE. Both fields are now optional.

2  evaluateMeshQuality NEVER VALIDATED ITS POLICY, so a contradictory one was
   half honoured and half silently ignored: the shape of defect P16-SIZE-001's
   audit found four times over. It now validates, classifies nothing on
   failure, reports the diagnostic, and does not produce a pass.

3  found by MUTATION, invisible to reading the diff and to the tests: the
   finiteness guard in classify() COULD NOT FIRE, because the only quantity
   that goes non-finite -- the circumradius -- had no metric name to be
   reported under. The two radii are now named metrics, every measured
   quantity is routed through the check, and the report names the number that
   failed.
```

Finding 3 is the argument for doing both: the code was correct in isolation,
its comment was true, and the tests asserted the right behaviour and got it.
Only deleting the branch and finding nothing complained exposed that a real
failure mode had no name.

## Regression

Full qualification, `qualification/run-qualification.cmd`, which only supplies
the three variables; `qualify.cmd` and `verify-harness.cmd` beside it are
carried **byte for byte unchanged** from `P15-QUAL-001`, verified by `diff`.
Each preset is configured, has **every** build output removed, is rebuilt with
warnings as errors, and runs CTest only after a successful build -- unfiltered.

```text
preset             build   warnings  ctest                    build time
debug-ext          exit 0  0         3071/3071 passed (100%)  21 min
release-ext        exit 0  0         3071/3071 passed (100%)  26 min
debug-shared-ext   exit 0  0         3071/3071 passed (100%)  18 min

repeat release-ext   214 tests selected, x5, 214/214 passed, exit 0
repeat debug-ext     214 tests selected, x5, 214/214 passed, exit 0

stages failed: 0          qualify.cmd exit 0
started 20:39:25   finished 22:48:33   2 h 09 m 08 s   2026-10-02
```

```text
test count   3011 (P16-SIZE-001) -> 3071, exactly +60:
             56 in tests/meshing/MeshQualityTests.cpp
              4 in tests/compile_fail/MeshQualityMisuse.cpp
executions   3071 x 3 presets + 214 x 5 x 2 repeat presets = 11 353
```

The no-op rebuild after each build had nothing to do but re-check the git
revision, which is what proves the binaries CTest ran are the ones just built.
The single `error` string in each build log is the filename
`Error.cpp.obj`, not a diagnostic.

`run-qualification.out` holds the harness's verdict. `run-qualification.err`
holds the run's stderr and is kept rather than discarded, but it contains
nothing but git's `CRLF will be replaced by LF` notices, one per file, emitted
by the fingerprint step: this working tree is CRLF and `.gitattributes` says
`* text=auto eol=lf`, so git says so every time it reads a file. Not a failure
and nothing to act on.

`debug-shared-ext` matters particularly here: `MeshQualityReport` carries
`std::optional<Error>`, a `std::map` and two vectors of data structs across a
DLL boundary, and that preset is the only one that would catch a missing
export. It was also built and run against the quality subset **before** the
freeze, so a DLL defect could not have been discovered only after a two-hour
run.

### The blast radius

```text
unit.Quality*                   the subject
unit.Mesh* unit.Vol* unit.Size* the rest of the meshing module: the data
unit.Surf* unit.Geom* unit.Tet*  model, geometry preparation, the surface
unit.Netgen*                     mesh, the volume mesh, sizing and the backend
compile_fail.meshquality        the new containment proofs
compile_fail.meshids
compile_fail.volumemesh         the groups that compile against meshing headers
architecture.*                  what would notice a containment or layering
                                violation
                                                             214 tests
```

Chosen from what could break rather than from proximity to the edited file.
This milestone adds a translation unit to `bettercad_meshing` and changes no
existing behaviour -- the only edit to an existing source file is a stale
comment in `Mesh.hpp` that pointed duplicate-tetrahedron detection at this
milestone when `P16-VOL-001` had already implemented it -- so the module in
full is the right set.

### Qualified tree == committed tree

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           88a5c7bad6fe4de51f19d84dea7cdc90c300db3d
src               d0b81da9ccd6f733797450d449a17eb22fb45a97
tests             bad4fd8f344e9deb1e22330811874a2dcd854db7
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Identical before and after the run, and identical to the fingerprint recorded
independently at the freeze at 20:33:32 -- three trees moved from `HEAD`
(`include`, `src`, `tests`) and five did not. These are the trees that were
committed.

### Pre-freeze checks

Run **before** the freeze, cheapest first, because all three of P15's voided
qualifications came from doing a cheap check after an expensive one:

```text
git diff --check, new files staged with add -N   exit 0, no whitespace errors,
                                                 no tabs, no trailing spaces
the 60 new tests x5 repeats, debug-ext           60/60 passed, 124 s
the shared build, debug-shared-ext               built clean, 56/56 quality
                                                 tests passed
architecture.* containment and layering          10/10 passed
the full debug-ext suite                         3070/3071 -- see below
```

The one pre-freeze failure was `cli.new.unicode-path`, and it was diagnosed
rather than waved through. The CLI writes the name correctly -- `"name":
"Plåt ✓"` appears in the JSON it produced -- and only the stdout
regex fails to match when the console is not on code page 65001. Re-run under
`chcp 65001` through `cmd.exe`, it passes; which is exactly why `qualify.cmd`
begins with `chcp 65001`, and it passed in all three presets of the
qualification. A property of running CTest from a bash shell, not a
regression, and nothing this milestone touches.

## Result

```text
RESULT:   PASS
TODO:     19/19 ticked.
NEXT:     P16-MAP-001 -- Geometry / mesh correspondence and regions. Not
          started, and not authorized by this document.
```

## Revision

First issue, 2026-10-02.
