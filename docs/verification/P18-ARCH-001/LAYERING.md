# P18-ARCH-001 — the thermal module's layer, and how it is enforced

```text
DECISION:  ADR-050 -- thermal at 45, below structural
CHECKER:   tests/architecture/CheckLayering.cmake, rule 3 strictly-lower
RESULT:    497 files, 0 violations on the real tree; 19 architecture tests
           PASS, 4 of them new
```

## The table after this milestone

```text
core        0     sketch     10    features   20    assembly  30
drawing    40     meshing    40    thermal    45    structural 50
io         60     renderer   70    scripting  70
```

No existing entry moved. `thermal` took a free integer, which is what ADR-035
respaced the table to allow, and the checker's comment beside the entry says
why it is 45 and not 50.

## Why 45, in one paragraph

The rule is strictly-lower: a module may include only modules strictly below
it. The roadmap's one known coupling is a temperature field driving thermal
expansion and stress, which is `structural` READING a `thermal` result. So the
reader must be above the read, and 45 is the only choice that lets that
include be written later without renumbering a qualified module. The mirror
image is the cost: `thermal` can never include `structural`, so P17's sparse
solver is reused by extraction below 45 or by an independent link
(P18-SOLVE-001 decides), never by include and never by a hand copy.

## What is proved, and by what

Each fixture tree under `tests/architecture/fixtures/` holds exactly one
violation and asserts the message naming it, so a rule that stopped firing
would fail rather than pass quietly.

```text
| DIRECTION                       | VERDICT  | PROVED BY                                   |
| thermal -> meshing, features,   | ALLOWED  | fixtures/valid/src/thermal/Analysis.cpp,    |
|   core                          |          | which also holds set(layer_thermal 45) in   |
|                                 |          | place: delete the entry and the valid tree  |
|                                 |          | reports an unknown module                   |
| thermal -> io                   | REFUSED  | architecture.checker.thermal-layer-violation|
| thermal -> structural           | REFUSED  | architecture.checker.thermal-to-structural  |
|                                 |          | -- THE direction ADR-050 fixes              |
| meshing -> thermal              | REFUSED  | architecture.checker.meshing-to-thermal     |
|                                 |          | -- the cycle, for the second mesh consumer  |
| nglib.h in src/thermal/         | REFUSED  | architecture.checker.thermal-backend-leak   |
| structural -> thermal           | ALLOWED  | by the table (50 > 45); no fixture, because |
|                                 |          | nothing writes that include yet -- the      |
|                                 |          | thermo-mechanical milestone will be the     |
|                                 |          | real caller, as P17-PERSIST-001 was for     |
|                                 |          | io -> structural                            |
| Qt, Eigen in a public header,   | REFUSED  | rules 2, 7 and 6, module-agnostic; the      |
|   src/ -> apps/                 |          | existing fixtures cover thermal already     |
```

## Zero-match protection

```text
ctest -N -R 'unit\.ThermalInput'                         11
ctest -N -R 'architecture\.checker\.[a-z-]*thermal'       4
ctest -N -R '^architecture\.'                            19   (15 + 4)
whole suite                                            4024   (4009 + 11 + 4)
```

Both new filters are in `ZeroMatchGuard.cmake` with asserted minimums (8 and
4). The second pattern was widened during the adversarial review: the first
draft matched only names that BEGIN with `thermal`, which left the cycle
fixture `meshing-to-thermal` outside the guard (finding F3 in
`ADVERSARIAL_REVIEW.md`).

## The real tree

```text
cmake -DSOURCE_DIR=<repo> -P tests/architecture/CheckLayering.cmake
-- Architecture check passed: 497 files, 0 violations
```

497 is P17-QUAL-001's tree plus `include/bettercad/thermal/ThermalAnalysis.hpp`
and `src/thermal/ThermalAnalysis.cpp`; the fixture trees are not in it, because
the checker globs `include/`, `src/`, `apps/` and `examples/` only.
