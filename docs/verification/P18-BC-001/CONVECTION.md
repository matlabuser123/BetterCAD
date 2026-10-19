# P18-BC-001 — convection (Robin)

```text
SUBJECT:  GeometryReference + h + T_inf  ->  current boundary facets  ->  one
          Tri3Convection (Kconv 3x3 [W/K], Qconv 3 [W]) per facet, bound to unknowns
```

## What is produced

For every boundary facet of the target face, in P16's ascending facet order:

```text
ConvectionFacet { facet id, dofs[3] in the triangle's own corner order, Tri3Convection term }
Tri3Convection  { Kconv(r, c) [W/K], Qconv(i) [W], area, h, T_inf }
```

The terms are LOCAL. Scattering them into a global matrix is P18-ASSEMBLY-001's
job, so there is no global array here to overwrite, and a node shared by several
facets simply appears in several `ConvectionFacet`s for the assembler to add.
`dofs[i]` is the unknown of corner `i` **as P16 stores the triangle**, never a
sorted order, so row `i` and source `i` cannot be paired with the wrong unknown
(`ConvectionFacetCarriesTheAnalyticalTermsAndOwnOrder`).

## The formulas

```text
Kconv  =  (h A / 12) | 2 1 1 |        diagonal h A / 6, off-diagonal h A / 12
                     | 1 2 1 |        row sums h A / 3, eigenvalues h A / 3, h A / 12, h A / 12
                     | 1 1 2 |
Qconv  =  (h T_inf A / 3) [1 1 1]^T
```

Derivation and the sign convention: WEAK_FORM.md and SIGN_CONVENTION.md. Computed
as `h * (A / 12)` and `(h * (A / 3)) * T_inf`: the division first, so a product
overflows only when the entry itself would (the order P18-LOAD-001 settled on, and
for the same reason).

## Traps closed

| Trap | Why it is tempting | How it is caught |
| --- | --- | --- |
| source only, no matrix | the source is the easy half | the kernel returns both; `B16` (source omitted) and every residual test fail |
| one-point centroid matrix `hA/9` everywhere | one-point quadrature is the default instinct | whole-matrix comparison; the wrong matrix has the right row sums and a null vector the true one does not; `B14` killed |
| lumped matrix `hA/3 I` | same row sums | whole-matrix comparison; `B15` killed |
| `Kconv` proportional to `k` | the strong law contains `k` | there is no conductivity argument; `B19` (a factor of 45) killed |
| `Qconv` sign | intuition | the constant-ambient residual is exactly zero only for the right sign; `B17`, `B18` killed |
| area in mm² | viewer coordinates | the `s^2` scaling test; `B22` killed |
| `h` and `T_inf` swapped | two numbers in one struct | distinct types; the runtime mutation `B32` killed |
| winding flips a sign | normals | none is computed; reversing the vertex order gives bit-identical global terms |
| negative or NaN `h` | `h < 0` is false for NaN | checked with negated comparisons and `isfinite`; `B28`, `B29` killed |
| overflow | finite inputs, infinite product | two gates (matrix, source) each with its own test and probe (`B26`, `B27`) |
| zero-area facet silently contributing nothing | zero is a number | `DegenerateFacet`; `B34`, `B31` killed |

## Superposition

Two convections on one face are two exchanges and ADD:

```text
K = (h1 + h2) A/12 [2 1 1; ...]        Q = (h1 T1 + h2 T2) A/3 [1 1 1]
```

checked against an independent oracle, against the single equivalent
`(h1 + h2, T_eff = (h1 T1 + h2 T2)/(h1 + h2))`, and, for two IDENTICAL conditions,
against exactly twice the single one (`ConvectionSuperposition`,
`ConvectionSuperpositionOnAMappedFace`). Identical convections are never
deduplicated (`B35`, which deduplicates them, is killed). The equivalent ambient
is an oracle only; no canonical condition is ever rewritten into it.

## Prescribed temperature on the same face

Allowed, and the convection terms are bit-identical with and without it
(`DirichletAndRobinOverlapPolicy`). Rationale in BC_MODEL.md.

## `h = 0`

The kernel and the integration seam accept it and return all-zero terms for any
ambient; the canonical record refuses it (BC_MODEL.md, "The `h = 0` decision").

## What it does not do

No conductivity is read, no material is consulted, no solved temperature is used
and no convective heat is computed from a result: that is P18-BALANCE-001's. No
radiation (`epsilon sigma (T^4 - T_inf^4)`), no time-varying `h` or `T_inf`, no
contact conductance.
