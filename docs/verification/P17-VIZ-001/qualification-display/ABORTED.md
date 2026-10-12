# This run was ABORTED, deliberately, 15 minutes in

It is kept rather than deleted so the sequence is visible.

```text
launched   10:10
aborted    10:27, during the debug-ext build stage
reason     an adversarial review question, asked while the run was in
           flight, had an answer that was a DEFECT
```

## The defect

`MeshPresentation::Compute` sets the plain facet group's interior colour to
`staleFacetColour()` when the mesh is stale. But this milestone **measured**
that per-vertex colours override a group aspect's interior colour — that is the
whole 1-versus-4807 result.

So a **stale contoured mesh draws its contour anyway**, and the stale facet
colour never reaches the screen. In `MeshStyle::Shaded` there is then no stale
indication at all; in `ShadedWithEdges` only the edges change, while the
facets — the dominant visual — show a contour as though the result were
current.

That breaks the requirement the brief calls the hardest:

```text
a stale result must never be presented indistinguishably from a current one
```

and it made the tick on "Clear stale-result visual state" **wrong for the
contoured case** — a false checkbox, which is the failure this project guards
against above all others.

## Why abort rather than finish

A qualification certifies the tree it froze. Finishing this one would have
produced a PASS for a tree containing a known defect, and the fix would then
have voided it anyway.

```text
abort now, fix, re-run        ~15 minutes lost
finish, fix, re-run           ~195 minutes lost, and a PASS on record for a
                              tree that should not have one
```

The process tree was verified dead by PID before anything was edited — a
harness that outlives its own stop has cost this project a run before.

The replacement run is in [../qualification-display/](.) once it has been
re-launched against the fixed tree; this file stays as the record of why there
were two.
