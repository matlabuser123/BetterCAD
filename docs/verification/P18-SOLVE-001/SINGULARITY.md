# P18-SOLVE-001 — singularity, the missing datum, and regions

```text
SUBJECT:  when the model has no unique solution, how that is detected, and what is NOT done about it
```

## Why a conduction model can be singular

`Kff` is the conduction matrix with the prescribed rows and columns removed. A region of the model
that is connected to no prescribed temperature and no convection face has a row sum of zero along
every row (a constant temperature changes no flux), so the constant vector is in the null space: the
solution exists only for compatible loads and is never unique. The cases are physical and common:
pure Neumann (flux and generation only), or a body cut off from every datum.

## What is detected, and by what

Two independent signals, so neither carries the claim alone:

| Signal | Where | Finds | Classification |
| --- | --- | --- | --- |
| **anchoring analysis** of the reduced network | `analyseAnchoring`, before any factorisation | a connected region of the nonzero entries of `Kff` with no row whose sum differs from zero | `missing_temperature_datum` if NO region is anchored, `unanchored_thermal_region` if some are |
| **the kernel's pivot gate** | `linalg::solveSymmetricSparse` | a zero pivot, a non-positive pivot, a ratio below the floor, an unusable residual | `singular_system` (the model IS anchored, so the cause is numerical) |

The first is read from the numbers, not inferred from geometry: it covers a mesh P16 never produced
and a synthetic system alike. The second is the qualified P17 gate, unchanged. They do not share
code, and each has tests that the other cannot pass: `[1 2; 2 1]` and `[1 1; 1 1]` and `diag(1, 1e-14)`
are all anchored in the row-sum sense yet refused by the gate (`FactorisationSingularityIsIndependentOfAnchoring`);
the pure-Neumann and disconnected systems are refused by the analysis, and without it (probe S10) the
kernel still refuses them but as `singular_system`, so the classification test kills it.

## What is NOT done

```text
epsilon on a diagonal:           NO     (probe S08 killed)
pinning one unknown:             NO     (probe S09 killed)
penalty Dirichlet:               NO     (constraints are applied by exact reduction)
weak thermal spring:             NO
pseudo-inverse / least squares:  NO     (the kernel is a direct factorisation of Kff, and fails)
hidden reference temperature:    NO     (no 0 K, 273.15 K or 300 K is chosen)
fallback to a second algorithm:  NO
returning a result for a singular model with an arbitrary level:  NO
```

A pure-Neumann model with a **compatible** load (infinitely many solutions) and one with an
**incompatible** load (none) are both refused with `missing_temperature_datum`, and the message tells
the user what to add: a prescribed temperature or a convection condition.

## The anchor tolerance, measured

`|sum_j Kff(i,j)| > kAnchorTolerance * sum_j |Kff(i,j)|`, `kAnchorTolerance = 1e-10`.

```text
rounding noise on a conduction row     ~2.2e-16 relative   (measured: 1+3e-16 rounds to 1+2.2e-16)
weakest sensible anchor                ~1e-6  relative     (one convection face, h A / k L)
constant                               1e-10               between them, four orders either side
```

Tested at both edges: a `1e-6` anchor is recognised and solved; rounding noise is not an anchor
(probes S19, zero tolerance, and S20, tolerance 1e-3, are both killed). It decides classification and
the early refusal only; the pivot gate and the residual are separate and not relaxed by it.

## h = 0 is not an anchor

A convection facet with `h = 0` contributes zero to `K` and zero to `Q`. Assembled through the seam
(the canonical condition refuses `h = 0`, P18-DATA's rule), the system's rows still sum to zero and
the solve reports `missing_temperature_datum`, not a number.