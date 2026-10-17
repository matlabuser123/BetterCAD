# P18-DOF-001 — Temperature DOF Model

```text
STATUS:    PASS -- 12 of 12, qualified 2026-10-17 against d40001c plus the
           working tree: 4163/4163 x 3 presets, 1277 x 5 in two, 0 warnings
           over 670 objects, 17 stages exit 0, 3h44m12s, the eight-path
           fingerprint at the freeze == before the first build == after the
           last repeat
MILESTONE: P18-DOF-001, the fourth milestone of P18 — Thermal Analysis
SCOPE:     the index space every later P18 milestone assembles against: one
           scalar temperature unknown per node, the prescribed-temperature
           constraint map, the free-equation map. No element, no matrix, no
           load, no boundary condition from CAD geometry, no solve.
```

## Baseline

```text
HEAD at start      d40001ca68382ebc71295fcee413cdc9e6f125f7  (P18-MAT-001's
                   documentation record, on top of 448eb5e)
HEAD^{tree}        71a66cd4754d1a78908f81a5ff312ffde1028302
origin/main        a3b721b150338cc1cbd24ee592a11a70a6196c0c  -- SIX COMMITS
                   BEHIND: no P18 commit had reached the remote when this
                   milestone started; verified with this milestone's push
working tree       clean but for the untracked .claude/ settings directory;
                   git diff, git diff --cached and git diff --check empty
P18-MAT-001        PASS, 19 of 19, 4115/4115 x 3; its eight-path fingerprint
                   equals commit 448eb5e's subtrees (include 5ba4f65a,
                   src 5bd8d50b, tests 1d8b2808)
P18-DATA-001       PASS, 19 of 19       P18-ARCH-001   PASS, 19 of 19
P16 / P17          QUALIFIED
authorized         2026-10-17, "start", with the owner's P18-DOF-001 brief
```

## What the audit decided before any code was written

**P16 guarantees the order and forbids the shortcut.** `Mesh` stores and
enumerates nodes in ascending `NodeId` with no unordered container,
`MeshBuilder` refuses a handle that is not greater than the last, and
`Mesh::findNode` is documented as a binary search "never `nodes_[id.value()]`"
because "handles may be sparse (1, 4, 10)". P18-DATA-001 had already keyed
the nodal temperature array by that same ordinal. So the temperature unknown
of a node IS its ordinal in the mesh's enumeration -- adopted, not invented --
and `ThermalResult::temperatures()[dof]` is the node's temperature with no
second convention to keep in step.

**P17's types are the right shape and the wrong module.** `DofIndex` and
`FreeEquationIndex` live in `structural/`, which `thermal/` may not include
(ADR-050), and `DofIndex` carries three-per-node, one-based arithmetic. Two
small distinct types in `thermal/` -- `ThermalDofIndex`, a zero-based node
ordinal, and `ThermalFreeEquationIndex`, a zero-based row -- are the same two
ideas written for one unknown per node, and their incompatibility with P17's
is the point: a structural row and a thermal row index different matrices.
[DOF_MODEL.md](DOF_MODEL.md).

## What this milestone adds

```text
ThermalDofIndex            a temperature unknown: the node's ordinal, zero-based,
                           no invalid value, no default; indexes the result array
ThermalFreeEquationIndex   a row of the reduced system, likewise
ThermalDofMap              which unknowns EXIST: N, from the mesh's own
                           enumeration, bound to its MeshStamp AND node count;
                           elementDofs() in the element's own node order
ThermalTemperatureConstraint  {dof, Temperature}: which, AND at what kelvin
ThermalConstraintMap       which unknowns are PRESCRIBED: ascending, unique,
                           exact duplicates collapsed, conflicts refused
ThermalFreeEquationMap     which are UNKNOWN and which row each is: compact
                           0 .. Nfree-1; agreesWith() re-derives the complement
reconstructTemperatures    Tf + Tc -> the full vector, parallel to the mesh
```

## The P18-specific fact

A structural restraint holds a displacement at ZERO, so P17's constraint set
carries no value. A thermal Dirichlet condition holds a node at an arbitrary
absolute temperature, so the constraint map here carries the VALUE, in
kelvin, and the reduced system a later solve forms is `Kff Tf = Qf - Kfc Tc`
with `Tc != 0`. Measured: 300 K and 350 K survive construction exactly, a
free unknown has no prescribed temperature rather than 0 K, and the probe
that forces every prescribed temperature to zero is killed.
[CONSTRAINTS.md](CONSTRAINTS.md).

## Sparse handles

```text
mesh ordinal   NodeId   ThermalDofIndex
0              7        0
1              104      1
2              9001     2
3              500000   3

2 nodes, maximum NodeId 4 294 967 294: the map holds 2 handles, 8 bytes
```

A tetrahedron with local order [9001, 7, 500000, 104] gets local unknowns
[2, 0, 3, 1], unsorted; two tetrahedra sharing nodes share unknowns; a
foreign node is `NotFound` and creates nothing.
[SPARSE_IDS.md](SPARSE_IDS.md), [MAPPING.md](MAPPING.md).

## Source binding

```text
same counts, same NodeIds, new mesh generation     old map REFUSED, by stamp
two snapshots of one builder (same stamp)          REFUSED, by count
material edit (k 45 -> 50)                         mesh current, map CURRENT,
                                                   result stale
load / solver / BC edit on the definition          map CURRENT
which unknowns are prescribed changes              old free map disagrees
only the prescribed values change                  same free topology -- and
                                                   the RHS still moves
```

[SOURCE_BINDING.md](SOURCE_BINDING.md), [FREE_EQUATIONS.md](FREE_EQUATIONS.md).

## Tests

```text
tests/thermal/ThermalDofTests.cpp               33 cases, 21 480 assertions
  one unknown per node; exact inverses over every unknown of three fixtures
  the sparse fixture by hand; NodeId is not the row; large handles, small map
  ordinals are the mesh's enumeration; P16 refuses out-of-order insertion
  Tet local order preserved; shared nodes share unknowns; foreign node refused
  no nodes / no identity refused; one node numberable; lookups past the end
  bound to the exact source; same counts and IDs on a new mesh stale; remesh
  material, load, solver and BC edits keep the map current; a changed
    constraint set stales the free map, a changed value does not
  non-zero temperatures stored; deterministic over 6 orders and 16 repeats;
    duplicates collapsed; conflicts refused both ways; NaN/inf/negative
    refused; 0 K accepted; nothing and everything prescribed
  the free map oracle; free xor constrained over 10 unknowns; rows contiguous;
    ordering deterministic; foreign and larger-snapshot sources refused;
    reconstruction [310 300 320 330 350]
  the result array is indexed by the unknown, element-local gather included,
    and refused after a remesh of the same counts
  the solver-local types are distinct; 20 000 nodes; two documents independent

tests/compile_fail/ThermalDofMisuse.cpp         15 cases + 1 control
  NodeId / ElementId <-> ThermalDofIndex, ThermalDofIndex <-> row, integers,
  defaults, a double as a temperature, three maps unconstructible, two
  definition members that do not exist
```

## Zero-match protection

```text
ctest -R 'unit\.ThermalDof' -N                  Total Tests: 33   (minimum 20)
ctest -R 'compile_fail\.thermaldof\.' -N        Total Tests: 15   (minimum 8)
the five earlier thermal filters                 unchanged
an impossible filter                             0, so the counter is real
```

## Mutation probes

```text
17 probes on ThermalDof.cpp / ThermalDof.hpp      17 KILLED   0 survived   0 void
  raw NodeId as the unknown; max(NodeId)+1 as the count; reversed order
  stamp ignored; count ignored (map and free map, four probes)
  agreesWith always true
  prescribed temperature lost; forced to zero; duplicates kept; last conflict
    wins; NaN accepted
  row == unknown; constrained unknown gets row 0
  element-local unknowns sorted; element-local unknowns 0..3
```

Four first-batch kills were process aborts rather than counted failures;
the suite was hardened (unchecked dereferences replaced by `REQUIRE`
helpers) and the four rerun, two of them additionally one process per case
because the abort is the mutant's own out-of-range index. Every probe is
killed by at least one named case; the table and the two findings are in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Adversarial review

```text
31 attacks from the brief + 6 of the reviewer's     all answered by measurement
findings                                            2, both test-method, closed
production defects                                  0
gate-blocking                                       0
```

F1: four kills were aborts in the TEST's own unchecked `*optional` under
`_GLIBCXX_ASSERTIONS`; hardened, rerun, counted. F2: two mutants abort
inside their own removed guard; counted per process through ctest.
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
The constraint map is built from SYNTHETIC constraints in this milestone:
  P18-BC-001 owns the resolution of a CAD face to nodes to unknowns, and will
  bind the constraint map to the canonical boundary-condition revision. The
  seam -- a changed constraint set stales the free map -- is exercised with
  synthetic sets, and the dependency on the canonical BC source is recorded
  as P18-BC-001's to close.

A matching free map does not make a reduced system current: Qf - Kfc Tc
  changes with the prescribed values while the free topology does not.
  Recorded in the header and in SOURCE_BINDING.md for P18-ASSEMBLY-001 and
  P18-SOLVE-001.

The mesh-node order is ascending NodeId because MeshBuilder enforces it, so
  a "permuted insertion order" mesh cannot be constructed and the order test
  asserts the map equals the mesh's enumeration rather than a permutation
  of it. A sort by NodeId would be indistinguishable today; the module does
  not sort, and mutation D03 shows the copied order is load-bearing.

A mesh with non-ascending nodes is unreachable (MeshBuilder refuses it);
  buildThermalDofMap verifies the ascent anyway and returns an Internal error
  -- a seam, deliberately not an enumerator and not claimed as tested.

Time-varying prescribed temperatures are deferred by ADR-049 to
  P18-TRANSIENT-001; the constraint map stores a constant per unknown.

The 20 000-node scale test numbers a hand-built node list, not a meshed
  solid; the index bound itself is a compile-time proof.

This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each from a fresh configure and a clean build, each running
the full unfiltered ctest; then the repeat set five times in two presets.
Logs are under [qualification/](qualification/), the stage timeline in
`qualification-times.txt`, the measurements in `postrun-summary.txt`.

```text
preset             configure  clean  build (warnings/objects)  no-op  ctest            time
debug-ext          0          0      0   (0 / 670)             0      4163/4163  0     2021.49 s
release-ext        0          0      0   (0 / 670)             0      4163/4163  0     1821.04 s
debug-shared-ext   0          0      0   (0 / 670)             0      4163/4163  0     1968.49 s
repeat release-ext                                                    1277 x 5   0     1097.71 s
repeat debug-ext                                                      1277 x 5   0     1210.24 s

4163 = 4115 (P18-MAT-001's qualified count, every one still present and
       passing) + 33 unit.ThermalDof + 15 compile_fail.thermaldof
1277 = the repeat filter's selection, recorded by the run, not predicted;
       1244 at P18-MAT-001 + 33 ThermalDof cases
no-op rebuild: one edge ("no work to do") in every preset
harness stderr: 268 lines, every one git's CRLF notice, 0 others
debug-shared-ext: 11 libbettercad_*.dll, libbettercad_thermal.dll among them

[thermal][dof] on each preset's own test binary, after the run:
  debug-ext          33 / 33   21 480 assertions
  release-ext        33 / 33   21 480 assertions
  debug-shared-ext   33 / 33   21 480 assertions
  the suite's 3 message lines (the large-handle and remesh INFO notes) are
  identical across the three presets once sorted (Catch2 shuffles order)
[thermal][material] 31 / 31, 1529 and [thermal][data] 36 / 36, 2349 in all
  three, unchanged from their own qualifications
zero-match counters on every preset: ThermalDof 33, thermaldof 15,
  ThermalMaterial 31, ThermalData 36, Thermal 135, StructuralDof 7,
  thermalids 24, architecture 19
layering after the run: 507 files, 0 violations
```

The P16 and P17 suites, and P18-ARCH/DATA/MAT's, ran unchanged: no file
under `src/core`, `src/features`, `src/meshing`, `src/structural` or `apps/`
is in this change, and the fingerprint proves the qualified tree is the
frozen one.

## Result

```text
P18-DOF-001   PASS   2026-10-17
  12 of 12 TODO items; 33 unit cases, 15 compile-fail cases
  17 mutation probes killed; 37 adversarial attacks answered; 0 production
    defects
  4163/4163 x 3 presets; 1277 x 5 in two; 0 warnings; 17 stages exit 0
  fingerprint  include f5477fa0  src 1cc3182e  tests 023734eb  apps a95876bc
               examples a955cf65  cmake 5a382115  CMakeLists.txt 3ec3c10f
               CMakePresets.json 3229f0f8   at the freeze == before == after
  the commit's subtree hashes, read back after the commit, are recorded in
    FREEZE.md by the follow-up documentation commit
Next: P18-ELEM-001 -- Linear Tet4 Thermal Element. NOT AUTHORIZED.
```

## Revision

First issue, 2026-10-17.
