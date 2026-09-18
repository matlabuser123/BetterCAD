# Architecture Review

For a decision with long-term architectural consequences — an assembly
solver, semantic topology, a constraint-solver redesign, CAD/mesh
association, a simulation architecture, a plugin system, a collaboration
model, a Python API, an agent architecture. Not for ordinary feature work.

Put up two or three serious candidates. One candidate is a decision already
made; a straw man is not a candidate.

```text
PROBLEM               what must be solved, and why now
CURRENT ARCHITECTURE  what exists, and what it already gets right
CONSTRAINTS           invariants, persisted formats, qualified behaviour

CANDIDATE A
CANDIDATE B
CANDIDATE C
```

Compare on: correctness, architecture fit, complexity, persistence,
determinism, performance, extensibility, failure modes, migration cost,
testability.

```text
CHOSEN DESIGN          and what makes it the choice
REJECTED ALTERNATIVES  and what would make them the choice instead
DEPENDENCY IMPACT
PERSISTENCE IMPACT
TESTING STRATEGY
```

Record the outcome as an ADR in
[../architecture/decisions/](../architecture/decisions/).
