# P17-LOAD-001 — the gravity decision

```text
GRAVITY:  IMPLEMENTED
```

The brief requires this to be explicit either way and forbids leaving the
checkbox ambiguous. It is implemented, and the reasons are the three the brief
itself sets as the test of readiness.

## Why the density path was ready

```text
ConsumerKind::FeaLinearStaticWithGravity   exists in P15, and
                                           requiredProperties() lists Density
                                           for it and not for FeaLinearStatic
StructuralAnalysisMode::LinearStaticWithGravity
                                           exists in P17-DATA-001, and
                                           P17-MAT-001 maps it to that consumer
StructuralMaterial::density()              an optional that is present EXACTLY
                                           when the mode needs one, because
                                           P17-MAT-001 READS P15's requirement
                                           table rather than restating it
features::requireDensity                   P15's own boundary, which refuses a
                                           missing or unusable density and
                                           names the gap
```

P17-MAT-001 said this in as many words: *"Gravity is selectable and not
assembled. A material can be resolved for `LinearStaticWithGravity`; nothing
computes a body force. `P17-LOAD-001` owns it."* That milestone did the work of
making the density available for this consumer by name; the only thing missing
was the integral.

## Why the integration was straightforward

A constant body force over a Tet4 needs no quadrature, for the same reason
P17-ELEM-001's `Ke` needs none: the shape functions are linear, so each
integrates to `V/4` over its own element.

```text
    b = rho g                 N/m^3
    F_tet = rho V g           N
    each of the four nodes receives rho V g / 4
```

`V` is P16's **signed** volume, read through `meshing::signedVolume` with no
absolute value anywhere. A `StructuralModel` proves every element is positively
oriented, so taking a magnitude could only hide a violation of that — and
mutation probe M13 confirms an `abs` there would be inert rather than
protective.

Dimensionally:

```text
    kg/m^3  x  m^3  x  m/s^2   =   kg m/s^2   =   N
```

## What the load carries, and what it must not

```text
GravityLoad { Vector3D acceleration; }     m/s^2, and nothing else
```

**No density.** ADR-028's hard rule is that a downstream solver maintains no
second authoritative material database, and a density on the load would be
exactly that — a value that could disagree with P15's. Asserted at compile
time:

```cpp
static_assert(sizeof(GravityLoad) == sizeof(Vector3D));
static_assert(!std::is_constructible_v<GravityLoad, Density>);
```

**No implicit direction.** A model may be built in any orientation, and an
environmental constant baked into the solver is not a fact about the user's
part. `kStandardGravity` (9.80665, the CGPM value) is offered as a convenience
and is not a default: a default-constructed `GravityLoad` has zero
acceleration, and the test asserts that. A sideways gravity is tested
alongside the downward one.

## What was validated

Against the **analytic** volume of the fixture, never against the mesh's own:

```text
the unit fixture       40 x 30 x 20 mm, so V = 2.4e-5 m^3 by hand
rho                    7850 kg/m^3
g                      9.80665 m/s^2
expected weight        -rho V g       matched to 1e-10 relative
RM-MESH-01             120 x 70 x 35 mm, V = 2.94e-4 m^3 -- matched the same way
```

A planar-faced block is tiled exactly by its tetrahedra, so the mesh volume
equals the analytic one to accumulation and the weight does too. That is what
makes the analytic figure a usable oracle here rather than a bound.

```text
total weight           rho V g, to 1e-10 relative, on two independent fixtures
transverse components  zero to 1e-9 of the weight
direction              negative when the acceleration is, and a sideways
                       acceleration gives a sideways weight
moment about O         (r_cm - O) x M g, with r_cm the block's geometric
                       centre from its own dimensions -- 1e-10 relative
node coverage          EVERY node of the mesh carries some of the weight,
                       unlike a surface load
element coverage       the contribution reports the tetrahedron count
scale law              F proportional to s^3 for a geometrically similar body
                       at three scales
density edit           7850 -> 2700 changes the weight to match, with the mesh
                       untouched
```

## The refusals

```text
gravity asked, material has no density    DensityMissing, and the diagnostic
                                          says "no density ... Set the analysis
                                          mode to self-weight so the material is
                                          resolved for it"
a load set with no gravity                the density is NEVER consulted, so a
                                          no-gravity analysis is not blocked for
                                          want of one
```

There is no default density anywhere in the module — **no 7850** — and the
mutation probe that bypasses the check is killed. The diagnostic points at the
analysis **mode** rather than at the material, because that is the thing the
user changes: P17-MAT-001 established that intent decides the consumer, and a
material resolved for `LinearStatic` legitimately carries no density.

## What is still deferred, and to where

```text
a per-element or graded density            P15 assigns a material per DOCUMENT;
                                           there is no per-body assignment yet,
                                           which P17-MAT-001 recorded as a
                                           limitation. A graded body force needs
                                           that first
rotational body forces (centrifugal)       not in P17's authorized scope; ADR-034
                                           fixes the solver as linear static
the gravity VECTOR as document intent      the load is canonical and lives in
                                           StructuralAnalysisDefinition, so it
                                           persists with the analysis -- but a
                                           document-wide default gravity, if one
                                           is ever wanted, is a UI and
                                           persistence question for P17-CMD-001
                                           and P17-PERSIST-001
```
