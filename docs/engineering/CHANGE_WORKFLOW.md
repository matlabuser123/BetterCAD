# Change Workflow

The working form of the lifecycle in [CLAUDE.md](../../CLAUDE.md#engineering-lifecycle).
Fill in the phases the change actually needs: a one-line fix needs a few, a
milestone needs all of them. Delete what does not apply rather than writing
"n/a" everywhere.

```text
TASK          the milestone ID, or the defect
SCOPE         what is in, and what is deliberately out
BASELINE      the commit this starts from, and what is already qualified

UNDERSTAND    what owns the behaviour now; its API, invariants, dependents,
              persisted format, and the tests that define it today
ARCHITECT     module, interface, ownership, dependency direction, persistence,
              identity, units, errors, regeneration, undo, test strategy
BLAST RADIUS  direct callers, indirect dependents, and the impact on
              serialization, regeneration, references, CLI, tests, models
              -> the regression set this change must run

IMPLEMENT     the smallest correct change

TARGETED TESTS         nominal, parameter change, failure, save/load, regeneration
INDEPENDENT VALIDATION see VALIDATION_TEMPLATE.md
FAILURE PATHS          each refusal: structured, atomic, recoverable

ADVERSARIAL REVIEW     see ADVERSARIAL_REVIEW.md -- a gate, not a formality

REGRESSION    the set the blast radius named, then the whole suite
QUALIFICATION freeze, record tree IDs, run the presets, check
              qualified tree == committed tree

EVIDENCE      docs/verification/<milestone>/
RESULT        PASS / FAIL / BLOCKED, with the numbers behind it
```
