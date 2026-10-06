# P16-QUAL-001 — configuration behaviour

```text
STATUS:   BLOCKED BY DESIGN -- meshing explicitly REFUSES under an active
          configuration override, and the refusal is tested
```

The brief requires this to be unambiguous, because configuration regeneration
was a defect carried into P16 from an earlier phase. Either a supported path or
an explicit block can qualify, if honest. This is the block.

## The underlying defect, which P16 did not cause and does not hide

BetterCAD can hold a configuration with parameter overrides and make it active.
What it does **not** yet do is rebuild the bodies when that happens:

```text
base configuration          20 x 30 x 50 mm box
createConfiguration("Wide") with width 20 -> 80 mm, made active
the regenerator's body      STILL 20 x 30 x 50
```

That is recorded as current behaviour by
`tests/meshing/GeometryPreparationTests.cpp`, which asserts the unchanged
volume explicitly rather than describing the defect in a comment.

## The guard

`GeometryIneligibility::ConfigurationOverrideActive` is the **first** check
`requireMeshableGeometry` makes — before the object is even looked up:

> *"A configuration with parameter overrides is active. The carried
> regeneration defect means the bodies in hand are the base configuration's, so
> nothing here can be trusted to describe the active one."*

So the forbidden outcome — *"effective configuration changes, but geometry
remains stale, and meshing proceeds anyway"* — is unreachable. Meshing stops
before it can read the body.

## What is tested, and what each test adds

```text
| TEST                                             | WHAT IT ESTABLISHES       |
| the base body is unchanged under an active       | the defect is REAL and
| override                                         | measured, not assumed     |
| part.reason() == ConfigurationOverrideActive     | the guard fires           |
| prepare() fails with FailedPrecondition          | it is a refusal, not a
|                                                  | warning                   |
| the message names the configuration ("Wide")     | the diagnostic identifies
| and says "does not yet rebuild"                  | WHICH configuration and
|                                                  | WHY -- not a generic error|
| clearing the active configuration meshes again   | NOT A LATCH: the refusal
|                                                  | is about the state, and
|                                                  | the base configuration is
|                                                  | meshable as before        |
| an override plus an upstream sketch edit still   | the configuration reason
| reports ConfigurationOverrideActive              | takes PRECEDENCE over
|                                                  | ordinary staleness, so the
|                                                  | diagnostic names the
|                                                  | deeper cause              |
| ConfigurationOverrideActive is first in the      | precedence is a property
| enumeration and in the check order               | of the type, not of the
|                                                  | order someone happened to
|                                                  | write the ifs in          |
```

The last two are the ones that make this more than a thrown error. A model can
be *both* under an override *and* stale from a sketch edit; reporting the sketch
edit would send a user to fix the wrong thing.

## Why RM-MESH-09 is deferred rather than built

P16-REFMOD-001 was permitted a configuration reference model "only if the
existing system guarantees a safe, current regenerated geometry path". It does
not. A reference model exercising it would either

* duplicate `GeometryPreparationTests`' own unit — which is where a refusal
  belongs, in the milestone that owns the boundary; or
* reproduce the historical stale-configuration bug as a *supported* workflow,
  which the brief forbids in as many words.

So it is deferred with its reason, and that reason is recorded in
`docs/verification/P16-REFMOD-001/REFERENCE_MODELS.md` as well as here.

## What would change this

Nothing in P16. Making configuration meshing *supported* means making
configuration **regeneration** rebuild bodies on activation — a change to the
configuration contract in `assembly`/`features`, not to meshing. When that
happens, the guard becomes unnecessary and this section becomes a test that the
two configurations give two different meshes. Until then:

```text
CONFIGURATION BEHAVIOUR:  BLOCKED BY DESIGN
                          meshing refuses unsafe stale configuration geometry
                          6 behavioural assertions + the precedence contract
                          not marked PASS as a capability, because it is not one
```
