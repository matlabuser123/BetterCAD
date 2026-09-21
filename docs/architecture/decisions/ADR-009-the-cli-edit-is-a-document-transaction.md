# ADR-009 — A CLI edit is a document transaction, and a batch is one of them

```text
Status:    Accepted
Date:      2026-09-21
Milestone: P13-CLI-001
Builds on: ADR-002 (assemblies live in the document)
           ADR-003 (internal references first; a locator is never identity)
           ADR-005 (placement is intent, transforms are derived)
           ADR-006 (assembly module and layer)
           ADR-008 (the assembly solve is a final pass)
```

## Context

Until this milestone the CLI **reads**. Its seven commands are `new`, `info`,
`validate`, `export-step`, `export-stl`, `version` and `help`, and the only
`io::saveDocument()` call in all 1343 lines of it is in `new`, which writes an
empty document. Every feature CLI test in the repository invokes `info` or
`validate` and nothing else: documents are built in process by the test, and
the CLI is asked to describe them.

`P13-CLI-001` turns it into an **editor**. Eight of its seventeen checklist
items are commands with no ancestor anywhere in the codebase.

Three questions have to be answered before any of them is written, and each
has a wrong answer that would be hard to take back once files exist that
depend on it.

### 1. How does a script name the thing it created two lines ago?

A scripted workflow creates a component and then mates it. It needs a way to
say *that one*.

The obvious answer is by name, and the obvious answer is the one this project
has explicitly rejected. `P13-REF-001` has a qualified test called
`Reference_IdentityDoesNotFollowNames`, and ADR-003 states the rule the whole
reference system rests on: a locator is never identity. A CLI that wrote names
into files as references would reintroduce, at the interface, exactly what the
model refuses internally.

But the document turns out to say something stronger than "names are not
identity", and it is worth stating exactly, because it changes the shape of
the answer:

```text
validateIdentifier()    a name is [A-Za-z_][A-Za-z0-9_]* , at most 64 chars
requireNameAvailable()  a name is unique across objects AND parameters
Document::rename()      re-checks both on every rename
```

So a name is a **unique key at any given instant**, and a name can never be a
decimal number. The risk here is not ambiguity. The risk is *staleness across
time*: a name resolved today and stored means something else after a rename.

### 2. One process per edit, or a batch?

Each invocation loading, editing and saving is simple, deterministic, and
matches every command the CLI already has. But a twenty-step script then
rewrites the file twenty times — and, far worse, **a script that fails at step
seven leaves the first six edits on disk.** The file is then neither the
document the script describes nor the one it started from, and nothing in it
says so. Rerunning is not idempotent and not recoverable.

That failure is the one this milestone exists to prevent, and one process per
edit does not prevent it. Per-invocation atomicity is not script atomicity.

A batch mode fixes it, and is usually said to cost a small language of its own
— its own parser, its own grammar, its own failure semantics — a second
interface to keep in step with the first.

### 3. What runs the assembly solve?

`features::validateDocument()` builds a `Regenerator` and regenerates. It does
**not** register the assembly handlers, and it cannot: `features` is layer 2,
`assembly` is layer 3, and ADR-006 and ADR-008 put the registration in the
caller's hands on purpose. So today `validate` regenerates features and knows
nothing about the assembly final pass.

## Decision

### A selector is an ID or a name, and the two sets cannot overlap

```text
SELECTOR := <decimal digits>      -- an object ID, the identity
          | <identifier>          -- a name, resolved to an ID at that instant
```

Disjointness is not a convention this parser invents; it is a **consequence of
`validateIdentifier()`**, which requires the first character to be a letter or
an underscore. A component can never be named `7`, so `7` can only ever be an
ID, and no sigil, prefix or escape is needed to tell the two apart.

Identity is the ID, always. A name is accepted only as a lookup at the moment
of use, resolved immediately, and never stored — the resolved `ObjectId` is
what reaches the document. Every creating command prints the assigned ID, so a
script can capture it and stop using the name.

Because the guarantee is load-bearing rather than incidental, it is pinned by
its own test: if anyone ever relaxed the identifier rule to admit a leading
digit, that test fails before any file exists that depends on it.

### Every edit is a transaction, and a batch is a transaction of many edits

One mechanism, used twice:

```text
single-shot   load -> apply one edit  -> save
batch         load -> apply N edits   -> save
```

An edit is a function over an **already-loaded document**:

```cpp
using EditResult = std::expected<std::string, EditFailure>;
using EditApply  = EditResult (*)(Document&, Args);
```

The single-shot command is literally the batch of one — the same `EditApply`
behind a different driver — so the two cannot disagree about what a command
means, and there is no second implementation to keep in step.

Atomicity then follows from the shape rather than from care: **nothing is
written until every edit has succeeded.** A batch that fails at step seven
saves nothing, exits non-zero, and names the line that failed. The file is
exactly as it was, so rerunning the corrected script is the whole recovery
procedure.

### The batch language is the CLI's own command lines

A batch file is a list of the same commands, one per line, minus the document
path — which is given once, to `batch` itself:

```text
# comments and blank lines are skipped
component-add --part Block --name Base
component-add --part Block --name Arm --z 50mm
mate-add --type fixed --component Base --name Ground
```

There is no second grammar, no second parser and no second set of option
names. `batch` tokenises a line, looks the command up in the same table the
top-level dispatcher uses, and calls the same `EditApply`. The only syntax the
batch file adds to the CLI is `#` for a comment and `"` for an argument
containing spaces.

Quoting turns out to have a narrower job than expected, and it is worth saying
so: **no name ever needs it.** Object, parameter and configuration names are
all identifiers, and `Configurations.hpp` gives that as the reason -- "so that
they can be typed on a command line without quoting". What does need it is a
quantity written with a space before its unit, `--z "50 mm"`, which the
argument parser accepts and the tokeniser would otherwise split in two.

This is what makes "do not build a second assembly implementation inside the
CLI" hold at the level of the *interface* as well as the model.

### Edits go through the P13-CMD-001 command objects

`component-add` constructs a `CreateComponentCommand` and executes it rather
than calling `assembly::createComponent()`. Those commands already validate,
and already fail atomically, so the CLI inherits both instead of restating
either.

What they do **not** give a one-shot process is undo, and none is built: a
`CommandHistory` that dies with the process would be state with no reader.
`P13-CMD-001` measured undo as deliberately transient, and it stays transient.

### The CLI is the composition root that joins features to assembly

`regenerate`, `solve` and `status` build a `Regenerator` **and** call
`assembly::registerHandlers()` on it. That is the injection ADR-006 and
ADR-008 describe, performed by the one component allowed to see both layers.

A consequence worth writing down rather than discovering later: `regenerate`,
`solve` and `status` **never write the file.** ADR-005 makes transforms
derived, so there is nothing for them to persist. They report, and their exit
code says whether the assembly is sound.

## Alternatives considered

| Option | Why not |
| --- | --- |
| **Names only, no IDs** | Reintroduces name-as-identity at the interface, against ADR-003 and the qualified `Reference_IdentityDoesNotFollowNames`. A rename silently retargets a script |
| **IDs only, no names** | Sound, and unusable: a script would have to capture and thread integers through every step, and a script's readability is part of its correctness |
| **A `#7` sigil for IDs** | Solves a problem that does not exist. The identifier rule already makes the sets disjoint, and a sigil is a shell-quoting hazard for no gain |
| **One process per edit, no batch** | Simple, and it permits the half-applied script this milestone exists to prevent. Per-invocation atomicity is not script atomicity |
| **A batch with its own language** (JSON, or a bespoke grammar) | A second interface over the same operations, with its own option names to drift out of step. Reusing the command lines costs one tokeniser and keeps one meaning per command |
| **Batch only, no single-shot** | Forces a file on the one-line case and breaks every existing CLI convention |
| **Edits calling `assembly::` free functions directly** | Bypasses the validation and the all-or-nothing execution `P13-CMD-001` already qualified, and would duplicate both in CLI code |
| **`regenerate` writing solved transforms back** | Directly against ADR-005. A persisted transform is a stale transform waiting to happen |

## Consequences

**Good.**

- A script is a plain text file of plain commands, and reading one needs no
  knowledge the CLI's own `--help` does not already give.
- Script-level atomicity is structural: no edit reaches the file until all of
  them have succeeded.
- The single-shot and batch paths cannot disagree, because they are one path.
- A name never becomes identity, and the reason is a property of the name rule
  rather than a discipline the parser has to maintain.

**Costs, stated plainly.**

- A batch holds the whole document in memory and writes nothing until the end.
  For the assembly sizes this project targets that is not a concern, and when
  it becomes one the fix is streaming, not partial saves.
- `batch` adds a tokeniser — quoting and comments — which is a genuine, if
  small, second syntax. Its failure modes (an unterminated quote, an unknown
  command, a malformed line) are each a diagnostic naming the line number.
- A face target cannot name a *copied* face from the CLI. The grammar covers a
  face's feature, role and profile entity; `FaceSelector::copies` is reachable
  through the core API and not through this interface. Recorded as a known
  limitation rather than papered over.
- `validate` still does not run the assembly final pass. Changing it would
  mean either moving the registration into `features` — which the layering
  forbids — or giving `features::validateDocument()` an injection point, which
  is a change to a qualified subsystem and is not authorized here. `status` is
  the command that does register it.
