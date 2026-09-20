# ADR-007 — One configuration system, not two

```text
Status:    Accepted
Date:      2026-09-20
Milestone: P13-CONF-001
Supersedes: nothing
```

## Context

`P13-CONF-001` asks for assembly configurations: named builds of one assembly
in which components and mates can be suppressed. Its checklist names a strong
`AssemblyConfigurationId`.

A configuration system already exists. `P12-PARAM-002` built named
configurations for parameters — `ConfigurationId`, `Configuration`,
`ConfigurationTable`, create/modify commands, persistence, undo and redo —
with these semantics, stated in its own header:

```text
base values -> the active configuration's overrides -> what is in force
```

A parameter's stored value is its **base** value and no configuration edits
it. The value in force is the base with the active configuration's override
applied on top, read through `Document::effectiveParameterValue()`. That is
why the header can claim that switching `Small -> Large -> Small` restores
`Small` exactly: the base never moved.

The question is whether assembly configurations are that same concept
extended, or a second one beside it.

## Decision

**Assembly suppression is stored as overrides on the existing
`Configuration`.** There is one configuration system, one `ConfigurationId`,
one active configuration, and one place a document records which build it is
describing.

`Configuration` gains two override maps beside its parameter overrides:

```cpp
using ComponentSuppression = std::map<ComponentId, bool>;
using MateSuppression     = std::map<MateId, bool>;
```

An absent entry means the object keeps its base state. A present entry is the
value that object's `suppressed` flag takes in this configuration.

The effective readers live in `assembly`, not in `core`:

```cpp
bool assembly::isComponentSuppressed(const Document&, ComponentId);
bool assembly::isMateSuppressed(const Document&, MateId);
```

because reading a base flag means reading `ComponentDefinition` and
`MateDefinition`, which are layer 3. `core` stores the overrides — it needs
only the ID types, which are its own — and never learns what a component is.

## Candidates considered

### A. A separate `AssemblyConfigurationId` and table — rejected

What the checklist literally asks for. Rejected on four counts.

**It is the second-system mistake `CLAUDE.md` names outright** — "reject a
design that introduces ... a second parameter system or document model". Two
configuration tables in one document means two active states, two name
spaces, two persistence paths, two undo stories, and two answers to "which
build is this?".

**It is wrong for the user.** A family of parts varies by dimension *and* by
content together: the `Large` build is wider **and** has the reinforcing
bracket. Under two systems the engineer creates `Large` twice, activates it
twice, and keeps the two in step by hand. Nothing in the model would notice
when they drift, and a document whose parameter configuration is `Large`
while its assembly configuration is `Small` is a silent wrong answer, not an
error.

**It duplicates solved problems.** Exactly-one-active, base-is-none, name
uniqueness, ascending-ID iteration, `forgetParameter` on deletion,
persistence by name rather than number, and the commands are all already
built and qualified. A second table reimplements each, and each
reimplementation is a chance to differ.

**It gains nothing.** The strong identity the checklist wants is already
there: `ConfigurationId` is a strong typed ID over its own tag. A second tag
would make assembly configurations *distinguishable* from parameter
configurations, which is precisely what is not wanted — they are the same
configuration.

### B. Suppression as a flag flip per configuration — rejected

Store a set of objects each configuration suppresses, rather than a map to a
value.

Rejected because it cannot express un-suppression. A component whose base
state is suppressed — "not in this build, normally" — could never be turned
back on by a configuration. Parameters already took the general form: an
override is a **value**, not a toggle. Mirroring that keeps one rule for both
kinds of overridable state.

### C. Overrides on the existing `Configuration` — chosen

Two maps beside the parameter overrides, and the same base-plus-override
reading everywhere.

The property that matters is inherited rather than rebuilt: because the base
flag is never edited by switching, `A -> B -> A` restores `A` exactly, with no
accumulated drift, for the same reason parameters do. The milestone's
"switching is deterministic" and "switching is atomic and recoverable"
requirements are then properties of a design already qualified, not new
machinery to be trusted.

## Consequences

**The solver does not learn that configurations exist.** It asks whether a
component or mate is suppressed and gets the effective answer, exactly as
expressions and features read `effectiveParameterValue()` today. The
P12 header states the principle: "a configuration reaches everything without
any of them knowing that configurations exist."

**One configuration controls both.** Activating `Large` sets the wider
dimension and unsuppresses the bracket in one act, because they are one
configuration.

**A document with no configurations behaves exactly as before.** The base
configuration is the absence of overrides, so every existing assembly — and
every existing file — keeps its behaviour with nothing added to it.

**Deleting an object clears its overrides.** `Document::removeObject()` calls
`ConfigurationTable::forgetObject()`, the sibling of the `forgetParameter()`
that already runs when a parameter is deleted, so a configuration can never
name an object that is gone.

**The cost:** `Configuration` now holds three kinds of override rather than
one, and a reader must know that a configuration is not only about
parameters. That is a smaller cost than two systems, and the class is named
for what it is — a configuration of the document — rather than for
parameters.

**What this does not decide:** whether a component may select a different
*part* configuration than the document's. `TODO.md` records that as an
accepted P13 constraint and it stays out of scope here.

## Deviation from the milestone brief, recorded

`P13-CONF-001`'s checklist says "implement strong `AssemblyConfigurationId`".
This ADR does not add that type, and the reasoning is above. The *capability*
is delivered in full: assembly configurations have strong typed identity,
carried by `ConfigurationId`. What is not delivered is a second identity type
for it, because the thing it would identify is not a second thing.

`CLAUDE.md` requires an architecturally significant decision to put up
candidates, compare them and record what was rejected. That is what this is,
and it is the mechanism the constitution provides for exactly this situation
— rather than either following the checklist into a second system, or
quietly departing from it.
