# ADR-027 — An engineering property is known, unknown, or derivable — never zero

```text
STATUS:    Accepted
DATE:      2026-09-26
MILESTONE: P15-ARCH-001
TOUCHES:   ADR-014 (geometry is built on demand, failures are loud),
           ADR-020 (an undefined datum is reported)
```

## Context

A solver that receives `0` for a Young's modulus it was never given does not
fail — it produces a stiffness matrix full of zeros and a displacement field of
infinities, or worse, a plausible-looking answer. The same for a density of zero
producing a weightless part. So the representation of "not known" is a
correctness question, not an ergonomics one.

The repository already refuses to guess in three places, and they set the shape:

- `core/standards/` omits `HoleDeviation::JS` because the sources disagree, rather
  than picking one rounding.
- `drawing::Resolution` is `Resolved / Unresolved / Invalid` with a diagnostic,
  and ADR-014 makes a drawing whose geometry cannot be built fail loudly instead
  of drawing something stale.
- `Result<T>` — `std::expected<T, Error>` — is the established way a function
  says "no, and here is why", with a structured `Error` carrying a code and a
  message.

The unit system is also stronger than expected and settles the dimensional half.
`Quantity<Dimension D>` gives compile-time dimensional safety over five base
dimensions — length, mass, time, temperature, angle — with `Density` already
declared (`mass / volume`) and literals `_kg_per_m3` and `_g_per_cm3`.
`isFinite(Quantity)` exists. Quantities serialize as bare SI doubles.

## Decision

### Three states, and the third is the interesting one

```text
Known        a value is held
Unknown      no value is held, and that is a legitimate state of a material
Derivable    no value is held, but this property can be computed exactly from
             others that are Known
```

`Unknown` is not an error and not a zero. It is what an incompletely
characterised material honestly looks like — most real datasheets give density,
E and yield, and say nothing about thermal expansion.

A property is therefore **not** `std::optional<Quantity>` alone, because
`optional` cannot express *Derivable* and cannot carry provenance (ADR-028). It
is a small value type over a `Quantity`, with an explicit state and no implicit
conversion to the underlying number. The exact spelling belongs to
`P15-UNITS-001`; what this ADR fixes is that **three states exist, `Unknown` is
representable, and nothing fabricates a default.**

### Asking for a property

Two levels, matching how the repository already distinguishes "look" from
"require":

```text
property(material, kind)            -> the property, which may be Unknown
requireProperty(material, kind)     -> Result<Quantity>, an Error if Unknown
```

A solver uses the second. The diagnostic names the material, the property and
the consumer, in the style the drawing layer already uses: not "missing
property" but "Al6061T6 (object:12) has no thermal conductivity, which the
thermal solver needs".

`requireMechanicalProperties()` / `requireThermalProperties()` gather a
solver's whole set in one call, so a run fails once with a complete list rather
than at the first gap.

### Canonical, derived, and the elastic-constant question

```text
CANONICAL   density                      rho
            Young's modulus              E
            Poisson's ratio              nu
            yield strength               sigma_y
            ultimate tensile strength    sigma_u
            specific heat capacity       cp
            thermal conductivity         k
            coefficient of thermal expansion  alpha
            dynamic viscosity            mu          (P19)

DERIVABLE, never stored when derivable
            shear modulus     G = E / (2(1 + nu))
            bulk modulus      K = E / (3(1 - 2nu))

DERIVED from geometry + material, never stored
            mass, centre of mass, inertia tensor     (ADR-026)
```

**G and K are Derivable, not canonical.** They are exactly determined by E and
nu for an isotropic material, so storing them creates a second source of truth
that can disagree with the first. When E and nu are Known, G and K are computed;
when either is Unknown, G and K are Unknown too, and the diagnostic says which
input was missing rather than reporting G as the gap.

**A supplied G that contradicts E and nu is rejected, not reconciled.** The
schema does not offer a slot for a canonical G, so the conflict the brief asks
about cannot be represented — which is the cheapest way to handle it. If a future
anisotropic material needs independent constants, that is a different property
set with its own ADR, not an override of this one.

### Temperature dependence, without building it now

The extension path is fixed so that today's constant-property work is not thrown
away later:

```text
today      a property's value is one Quantity
later      a property's value is a LAW evaluated at a state (temperature, ...)
```

The separation is between a property's **identity** — which material, which
property kind, its provenance — and its **value representation**. P15 implements
a constant value; a later milestone replaces the value representation with a law
(constant, piecewise table, interpolated table, analytic function) **without
touching material identity, assignment, or the Known/Unknown/Derivable states**.

The rule that makes this safe: **nothing outside the property type reads a raw
double.** Consumers call `requireProperty(material, kind)` — or a later
`requireProperty(material, kind, state)` — so adding an evaluation state adds a
parameter, it does not restructure callers.

Evaluating a constant property at any temperature yields the constant, so a
temperature-aware consumer written later works against P15 data unchanged.

### The unit gaps this uncovers, for P15-UNITS-001 to close

Audited, not assumed. P15-UNITS-001 must add:

```text
energy            = force x length         derivable from existing bases
power             = energy / time          derivable
specific heat     = energy / (mass x temperature)
thermal conduct.  = power / (length x temperature)
thermal expansion = 1 / temperature
dynamic viscosity = pressure x time
```

None of those needs a new base dimension — `temperature` is already one, and the
header says it was included "so that thermal quantities can [be expressed]".

**One genuine gap: there is no electric current base dimension.** `Dimension`
has length, mass, time, temperature and angle only, so electrical resistivity
and conductivity are **not expressible today**. P15 does not need them; P19 might.
Recorded here so that whoever needs them knows it is a base-dimension change and
not an alias.

## Consequences

**A solver cannot invent a property.** `requireProperty` returns `Result`, so a
missing value is a failed run with a named cause, which is the same contract a
failed regeneration already has.

**"Unknown density" cannot become zero density**, because `Unknown` holds no
`Quantity` at all and the property type does not convert to `double`.

**An incompletely characterised material is usable for what it does specify.** A
material with density and E but no `k` supports mass properties and structural
work, and fails clearly for thermal. That is how real data arrives.

**One derivation rule, in one place.** G and K are computed by the materials
module, so P17 cannot hold a different formula.

**Adding temperature dependence later is additive.** Identity, assignment,
persistence of *which* material, and the three states are untouched by it.

## Alternatives rejected

**`std::optional<Quantity>`.** Rejected: it has two states where three are
needed, so *Derivable* would have to be inferred by every caller, and it has
nowhere to put provenance.

**NaN as "unknown".** Rejected: it propagates silently through arithmetic, and
`isFinite()` already exists to reject NaN as *invalid data*, which is a different
thing from *absent data*. Conflating them loses the distinction.

**Zero, or a negative sentinel.** Rejected. Zero is a legitimate value for some
quantities and a catastrophic one for a modulus; a negative sentinel is a magic
number that every consumer must remember to test.

**G and K canonical, stored alongside E and nu.** Rejected: two sources of truth
for one physical fact, with no rule for which wins.

**A full `PropertyLaw` abstraction now.** Rejected as overbuilding, which
`CLAUDE.md` forbids directly. What is needed now is the guarantee that a law can
replace a constant later, and that is obtained by keeping every consumer behind
`requireProperty` rather than by writing the law today.

## Validation

```text
Quantity<Dimension> gives compile-time dimensional safety        Quantity.hpp:30-47
five base dimensions; temperature is one of them                 Dimension.hpp:14-20
Density already exists, with literals                            Units.hpp:22, Literals.hpp:121-125
isFinite(Quantity) exists                                        Quantity.hpp:175
NO electric current base dimension                               Dimension.hpp:14-20 (audited)
quantities serialize as bare SI doubles                          src/io/json/*.cpp
Result<T> = expected<T, Error> is the established refusal         core/Error.hpp
Resolved/Unresolved/Invalid is the established tri-state          drawing/Resolution.hpp
standards data omits what it cannot settle rather than guessing   HoleTolerances.hpp
21 tests already cover the quantity system                       tests/core/units/
```

## Invariants

```text
Unknown is never zero, never NaN, and never a sentinel
no consumer reads a property as a raw double
a property that can be derived exactly is derived, not stored
a solver that needs a missing property fails with a diagnostic naming it
adding temperature dependence changes no material identity or assignment
```
