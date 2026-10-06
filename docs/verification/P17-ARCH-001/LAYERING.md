# P17-ARCH-001 — the layer decision and its enforcement

```text
SUBJECT:  where a structural module sits, why the table had to change, and how
          the decision is enforced rather than described
SOURCE:   tests/architecture/CheckLayering.cmake, read in the committed tree
DECISION: ADR-035
```

## The audit, before the decision

`CheckLayering.cmake` rule 3, quoted exactly:

```cmake
elseif(DEFINED layer_${file_module} AND NOT target_module STREQUAL file_module
       AND NOT layer_${target_module} LESS layer_${file_module})
```

Three facts follow, and all three were read out of the code rather than assumed:

```text
1. A cross-module include is a violation unless the target's layer is
   STRICTLY lower.
2. Therefore two modules on the SAME layer cannot include each other at all.
   That is why `drawing` and `meshing` may share 4: neither uses the other.
3. A module with no `set(layer_<name> N)` entry is itself a violation
   ("unknown module '<x>' (add it to the layer table)"), so the table is a
   mandatory registration point and not documentation.
```

The table in force at the start of this milestone:

```text
core 0   sketch 1   features 2   assembly 3
drawing 4   meshing 4   io 5   renderer 6   scripting 6
```

## What a structural module needs

```text
structural -> meshing     reads the Tet4 mesh, the geometry/mesh map and the
                          quality report                       so > meshing
structural -> features    the Regenerator, and P15's material accessors
structural -> core        ids, units, Result, the document
io -> structural          will serialize the analysis intent    so < io
renderer -> structural    will display the results              so < io too
```

So `meshing (4) < structural < io (5)`. **There is no integer there.** Fact 2
above rules out sharing 4, because a structural module uses meshing — which is
exactly the relationship `drawing` does not have.

## The third renumber

```text
ADR-006   introduced `assembly` at 3     and moved io 3 -> 4
ADR-015   introduced `drawing` at 4      and moved io 4 -> 5
P17       needs a module between 4 and 5 and would move io 5 -> 6
```

Both earlier ADRs record the renumber as unavoidable rather than cosmetic. The
roadmap then has `P18` thermal, `P19` CFD, `P20` optimisation and `P21`
semantic topology, several of which sit in the same band between a derived
representation and persistence.

Two renumbers in two additions, with four more additions expected in the same
place, is what decided this. The options and the reasoning are in
[ADR-035](../../architecture/decisions/ADR-035-a-structural-module-at-layer-50-and-a-layer-table-respaced-in-tens.md).

## The table now

```text
core        0     sketch     10    features   20    assembly  30
drawing    40     meshing    40    structural 50    io        60
renderer   70     scripting  70
```

Relative order is unchanged, which is the whole safety argument for a respace:
the architecture check passed before and after with the same verdict on the
same tree.

```text
before the respace   445 files, 0 violations   (the tree as P16 left it)
after                447 files, 0 violations   (+2 structural files)
```

A layer value is now an **ordinal with gaps**, stated in the checker's own
comment so a reader is not left to infer it. Nine free integers sit between any
two neighbours.

## Cost of the respace, measured

The numbers live in exactly one authoritative place and a handful of comments:

```text
tests/architecture/CheckLayering.cmake     the source of truth, 9 entries
ARCHITECTURE.md                            the mirrored table + 2 prose mentions
src/<module>/CMakeLists.txt                6 header comments quoting their own
                                           layer: sketch, features, assembly,
                                           drawing, meshing, renderer
docs/architecture/decisions/ADR-006, 015   quote the tables of THEIR day and
                                           were deliberately left alone --
                                           they are historical records
```

Every one of the first three was updated in this change. Leaving a module
comment claiming "layer 4" after the respace would be the documentation drift
this project treats as a defect.

## Rule 6, and the gap it closes

Rules 1 to 5 could not express "the library must not depend on an application",
which `P17`'s brief requires for a structural core:

```text
rule 2 keys on a header's SHAPE      stops #include <QWidget> in src/structural/
                                     but not
                                     #include "../../apps/bettercad_cli/Commands.hpp"
rule 3 keys on bettercad/<module>/   apps/ is not a module, so it has no layer
                                     and cannot be compared
```

A library file reaching a CLI parser, a file dialog or a GUI selection would
have passed the architecture check. Rule 6:

```cmake
if(header MATCHES "(^|/)apps/" AND file MATCHES "^(include|src)/")
```

Written for every module, not only this one. Nothing under `include/` or `src/`
had such an include when the rule was added — verified by search before adding
it — so it closed the gap at zero cost.

## Enforcement, not description

Five trees, each holding exactly one direction, each asserting the message that
names it. A rule that stopped firing would fail rather than pass quietly.

```text
| FIXTURE                      | DIRECTION                  | EXPECTED |
| valid/src/structural/        | structural -> meshing,     | ALLOWED  |
|   Analysis.cpp               |   features, core           |          |
| structural-layer-violation   | structural -> io           | REFUSED  |
| meshing-to-structural        | meshing -> structural      | REFUSED  |
| structural-backend-leak      | structural -> nglib.h      | REFUSED  |
| library-reaches-apps         | src/ -> apps/              | REFUSED  |
```

Measured, each run standalone against its own tree:

```text
structural-layer-violation   1 violation   'structural' must not depend on 'io'
meshing-to-structural        1 violation   'meshing' must not depend on 'structural'
structural-backend-leak      1 violation   mesh backend header <nglib.h>
library-reaches-apps         1 violation   reaches into apps/
valid                        0 violations
```

`meshing-to-structural` is the one that matters most: it is the **cycle**. P16
must know nothing about the solver that consumes it, or the mesh would depend on
the solve that depends on the mesh.

The `valid` tree does double duty. It proves the permitted direction, and it
holds `set(layer_structural 50)` in place: delete the table entry and that tree
reports an unknown module instead of passing, so the registration cannot be
silently lost.

There is deliberately **no** structural-specific Qt fixture. Rule 2 keys on a
header's shape and not on a module, so `architecture.checker.qt-leak` already
covers every module including this one, and a second fixture would assert the
same code path twice while implying the first did not cover it.

## What is not enforced by the include checker

Honest limits, stated rather than left to be discovered:

```text
CMake target dependencies are not checked by this script. It reads #include
directives. A link-time dependency added in a CMakeLists.txt without a matching
include would not be caught here -- it would simply be unused.

`apps/` -> `apps/` is not constrained. The CLI and the GUI are siblings and
rule 6 scopes to include/ and src/ deliberately.

Private headers inside a module are not partitioned. `src/structural/` may
include its own private headers freely, which is rule 4's boundary only for
public headers.
```
