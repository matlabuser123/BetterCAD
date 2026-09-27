# ADR-029 — Electrical resistivity is a scoped strong type, not a sixth base dimension

```text
STATUS:    Accepted
DATE:      2026-09-27
MILESTONE: P15-THERM-001
TOUCHES:   ADR-027 (which recorded this gap and left it open), P15-UNITS-001's
           Dimension and Quantity contracts
```

## Context

P15-THERM-001 is asked for an electrical-resistivity foundation. ADR-027 already
found, and recorded, why that is not a small thing:

> "**One genuine gap: there is no electric current base dimension.** `Dimension`
> has length, mass, time, temperature and angle only, so electrical resistivity
> and conductivity are **not expressible today**. P15 does not need them; P19
> might. Recorded here so that whoever needs them knows it is a base-dimension
> change and not an alias."

`Dimension.hpp` repeats it at the point of use. That was audited again here and is
still exactly true: the struct has five `int` exponents — length, mass, time,
temperature, angle — and nothing else.

Resistivity in SI is `M L^3 T^-3 I^-2`. The `I^-2` has no home.

Two further facts constrain the answer.

**The repository already has a precedent for a quantity that does not fit.**
`PoissonRatio` is a distinct class over a `double`, not a `Quantity`, because "this
system returns a plain `double` for any dimension that cancels completely — a
dimensionless Quantity is not how BetterCAD spells a pure number". Its header says
why a type was nevertheless wanted: "`double` would let Poisson's ratio be passed
wherever a strain, a factor or a count is wanted, and in particular wherever a
KINEMATIC VISCOSITY is wanted ... A dimensionless property crossing a public
engineering boundary gets a name."

**Nothing in P15 consumes resistivity.** It is asked for as a foundation. The first
consumer ADR-027 anticipates is P19.

## Constraints

- `Dimension` and `Quantity` are qualified, `constexpr` throughout, and reached by
  50 catalogued units, 5 dimensions composed from each other, 18 compile-fail
  cases and the whole of P15-UNITS-001 and P15-MECH-001.
- Storing resistivity as a bare `double` is refused outright: an untyped number
  crossing a public engineering boundary is what every strong type in this
  repository exists to prevent.
- Whatever is chosen must not make the eventual base-dimension change harder than
  it is today.

## Options

**A — Add an `electricCurrent` exponent to `Dimension`.** Resistivity becomes a
real `Quantity`, with arithmetic: `sigma = 1 / rho` would type-check, and
resistance times area over length would come out as `Ohm m`.

Against: it changes the qualified core of the unit system for a property no P15
milestone consumes. `Dimension`'s five members are reached by `operator*`,
`operator/`, `inverse()`, `hasEvenExponents()`, the square root, `siUnitSymbol()`,
`describeDimension()` and the unit catalog. Designated initialisers mean existing
aggregate initialisation would still compile, which is precisely the risk: the
breakage would not be at compile time but in whatever prints or compares a
`Dimension`. It is a change that wants its own milestone and its own regression,
driven by a consumer that can say what it needs.

**B — A scoped strong type, `ElectricalResistivity`, over a `double` in Ohm m.**
Follows `PoissonRatio` exactly: a named type that cannot be confused with mass
density or anything else, no change to `Dimension`, `Quantity`, the catalog or any
existing test.

Against: no dimensional arithmetic. Multiplying it by a length does not yield an
`Ohm m^2`, and `sigma = 1 / rho` is not typed. Its safety is nominal, not
dimensional.

**C — Do not implement resistivity at all**, citing ADR-027's "P15 does not need
them".

Against: the foundation is asked for, and a material that records a resistivity is
ordinary engineering data. Leaving it out would mean a later milestone inventing
the representation under time pressure, which is how an untyped `double` gets in.

## Decision

**B.** `materials::ElectricalResistivity` is a distinct class over a `double`
carrying `Ohm m`, on the `PoissonRatio` pattern, with `of()`, `value()` and
`isFinite()` and no implicit conversion in either direction.

The limitation is stated at the point of definition rather than discovered: this
type is **not** a `Quantity` and therefore has **no dimensional checking**. It
cannot be multiplied by a length, and electrical conductivity is not derivable
from it in a typed way. What it does guarantee is that a resistivity cannot be
passed where a mass density, a modulus or a bare number is wanted — which is the
confusion that actually costs something, given that both resistivity and mass
density are written `rho`.

**When a consumer needs the arithmetic, option A is the answer, and this type is
what it replaces.** The migration is contained by design: `ElectricalResistivity`
is one class, used by one property slot, with one accessor. Promoting it to
`Quantity<dimensions::electricalResistivity>` changes that class and leaves every
caller's spelling — `of()`, `value()` — working, because those are the names
`Quantity` uses too (`fromSi()`, `si()` differ, so the promotion is a rename of
two members, not a redesign).

## Consequences

**The qualified unit system is untouched**, so P15-THERM-001 adds no risk to the
50 units, 5 composed dimensions or 18 compile-fail cases that P15-UNITS-001
qualified.

**Resistivity is nominally safe and dimensionally unchecked, and the evidence says
so.** A test can prove a `Density` cannot be used as an `ElectricalResistivity`;
no test can prove `Ohm m` times `m` is `Ohm m^2`, because the type does not know
what an ohm is.

**Electrical conductivity is not offered.** `sigma = 1 / rho` is a relationship a
consumer may compute, but it is not stored material data and there is no derived
property for it here: ADR-027's Derived state is for values determined exactly by
other *stored* properties of the same material, and a conductivity would be a
second spelling of one property rather than a consequence of two.

**A future base-dimension change gets easier, not harder**, because the one place
resistivity is represented is named and isolated.

## Validation

Checked against the tree at `4940932`, not assumed.

```text
Dimension has exactly 5 int exponents, no current      core/units/Dimension.hpp
ADR-027 recorded the gap and left it to a consumer     ADR-027, "One genuine gap"
Dimension.hpp repeats it at the point of use           dimensions namespace comment
PoissonRatio is the precedent: strong type, no Quantity core/units/Units.hpp
no existing resistivity or current anywhere            audited: 1 hit, a comment
nothing in P15 consumes resistivity                    P15 sequence in TODO.md
```

## Invariants

```text
electrical resistivity is never an untyped double across a public boundary
electrical resistivity is never confused with mass density
electrical conductivity is not stored, and not derived here
adding an electric-current base dimension remains a separate, deliberate change
```
