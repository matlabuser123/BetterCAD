# P17-VALID-001 — determinism

A report whose order moves cannot be diffed, and a verdict that depends on
iteration order is a verdict that can differ between presets. Both are checked
rather than argued.

## What could have been non-deterministic, and what each is instead

```text
hazard                          what is used instead
-----------------------------------------------------------------------------
the threshold map's iteration   std::map<QualityMetric, QualityThreshold>,
                                which P16 chose for exactly this reason:
                                "keyed by metric, so iteration follows the
                                enumeration order and the report cannot
                                depend on a hash"

the quality summaries           std::map, likewise P16's

the finding order               std::stable_sort on (severity desc, code,
                                element). Stable, so findings that tie keep
                                the order the stages produced them in --
                                which makes the order a function of the
                                staged chain and not of the sort's internals

the worst element per metric     READ from MetricSummary::worst, which P16
                                computes by its own frozen traversal. Not
                                recomputed here, so there is no second
                                tie-break to disagree

the stage order                  a straight-line sequence of ifs, not a
                                container of checks

the restraint and load order     the user's, preserved by
                                StructuralAnalysisDefinition. P17-BC-001
                                already established that two orderings of a
                                restraint list give the same ConstraintSet,
                                because it is a union accumulated into an
                                ascending set

the published arrays             walked in the order the recovered fields
                                were produced, which IS the mesh's own
                                ascending enumeration. Never indexed by a
                                raw NodeId -- P16 allows a caller to choose
                                node ids, so `id.value()` as an index is
                                correct for every mesh the current backend
                                happens to produce and wrong for the first
                                one that is not
```

No wall clock, no pointer identity, no unordered container, no unfixed random
seed, no locale-dependent formatting of a compared value, no temporary path.
There is no thread in this module.

## Asserted, not asserted-about

```text
OrdersFindingsDeterministically
    the same report twice, compared with `==` -- which is field-complete and
    defaulted, so a field added without adding it to the comparison is
    impossible. Then the ORDER itself is walked: every adjacent pair must be
    (failure before warning) or (same severity, non-decreasing code).

AcceptsAWellPosedAnalysis / "and it is deterministic"
    the full document-level report twice, compared with `==`. This covers the
    measured counts and the per-metric worst values, not only the findings.

SolvesAWellPosedAnalysisEndToEnd / "and the solve is deterministic"
    two solves of the same document. Every displacement compared with `==`
    -- EXACT equality, not a tolerance, because the same inputs through the
    same code must give the same bits. The pivot ratio and the strain energy
    likewise.
```

The displacement comparison being exact is deliberate. A tolerance there would
pass even if the solve were order-dependent at the last few bits, which is
precisely the defect determinism testing exists to catch.

## Across presets

The whole suite runs unfiltered in `debug-ext`, `release-ext` and
`debug-shared-ext`, and this milestone's filters additionally run under
`QUALIFY_REPEAT` in two of them. A verdict that depended on optimisation level
or on a DLL boundary would show up there.

`debug-shared-ext` carries a specific hazard this milestone was written
against: an out-of-line member of a struct that carries no export macro is not
exported, and that defect has recurred in three BetterCAD modules.
`StructuralValidationReport::status()` is therefore defined INLINE in the
header, with the reason stated beside it, and the four `inline constexpr`
constants carry no export macro — a `BETTERCAD_*_EXPORT` on a header-defined
`constexpr` expands to `dllimport` and fails only in that preset.

## What is NOT claimed

The MEASURED NUMBERS in this milestone's evidence — the quality distributions
and the accuracy law — were taken in `debug-ext` and are reported as such. The
law's fit constant is a floating-point quantity and could move between presets;
that is why the test pins it to a BAND (`1e-16 < err*sqrt(3r/R) < 1e-14`, 2.6x
below and 4.1x above the measured range) rather than to a value, and why the
threshold derived from it carries a 17x margin on the crossing point rather
than sitting at it.
