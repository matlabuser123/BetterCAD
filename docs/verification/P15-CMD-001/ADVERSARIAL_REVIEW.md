# P15-CMD-001 — Adversarial review

**Aim: disprove the milestone.** The 24 attacks the brief names, plus 4 of my own.

**Result: 3 findings.** None is a production defect. Two are architectural facts that
answer whole groups of attacks by construction, and one is a scope boundary recorded
rather than crossed.

**No production defect was found, and I do not treat that as reassuring on its own —
so two of the five automatic-FAIL gates were mutation-tested.** Making redo allocate a
new identity fails 5 test cases; making undo restore only the metadata fails the
Known-restoration assertion outright.

---

## Findings

### F1 — Create and delete were already correct, and I verified that by probe before writing anything (SCOPE, recorded)

A `Material` **is** a `DocumentObject`, and `AddObjectCommand` / `DeleteObjectCommand`
already existed with exactly the needed contract: "redo re-inserts it with the same
ID", "undo restores it with the same ID and name".

Rather than assume that extended to materials, I wrote a throwaway probe first. It
confirmed all of it:

```text
AddObjectCommand on a material     execute -> id 1; undo -> absent;
                                   redo -> present, id 1, definition and
                                   provenance identical
DeleteObjectCommand on a material  execute -> assignment Unresolved (state 2);
                                   undo -> assignment Resolved (state 1)
```

So `CreateMaterialCommand` and `DeleteMaterialCommand` **wrap** those rather than
reimplementing them, exactly as `CreateFeatureCommand<F>` already wraps
`AddObjectCommand`. What the wrappers add is a typed ID at the call site, a
material-shaped description, and the one precondition the generic command cannot
express — that the ID really names a material.

**That precondition earns its place.** Without it, `DeleteMaterialCommand` on a
sketch's ID would delete the sketch and report success. Tested.

The probe was deleted afterwards; it was a measurement, not a deliverable.

### F2 — Three whole groups of attacks are answered by the assignment model, not by code I wrote (ARCHITECTURE, recorded)

The brief devotes items 22, 23 and several attacks to distinguishing *direct* from
*effective* intent, to occurrence overrides, and to configuration-local assignment.

In this tree the canonical assignment is **one optional `MaterialId` on the Document**
(ADR-026, P15-ASSIGN-001). There is no per-body, per-occurrence or per-configuration
assignment. So:

```text
"can remove-assignment delete a parent's intent because the child's effective
 material resolves to it?"        -- there is no parent/child assignment
"can AssignMaterialCommand(Occurrence, ...) be written?"
                                 -- ComponentDefinition has no material field, and
                                    two compile-fail cases in the `assign` group
                                    already prove that absence
"can a command create configuration-local material intent?"
                                 -- a configuration overrides parameter VALUES; a
                                    MaterialId is not one, so the mechanism cannot
                                    carry it (ADR-026)
```

These are recorded as answered-by-construction rather than tested into existence. What
*is* tested is that the commands move the document's canonical field and that the
derived effective material follows it.

### F3 — The object-lifecycle interaction at item 42 is outside this milestone's boundary (SCOPE, recorded)

Item 42 asks what happens to `Assign P → A; Delete P; Undo Delete P; Undo Assign`
when the *target* of an assignment is deleted between command operations.

In this architecture the assignment's target is the **document**, not an object, so
"delete the target" means deleting the document — which is not a command. The sequence
as written cannot be built. The nearest reachable case is deleting the assigned
*material*, which is exactly the delete/undo chain tested in depth.

Recorded as a boundary rather than stretched into a test that would not mean what the
brief intends. If per-body assignment ever arrives, this becomes a real question and
the test belongs with it.

---

## The 24 attacks

### Identity

**1. Can redo create allocate a new MaterialId?** No — **mutation-tested**. Replacing
`add_->redo()` with a fresh creation fails **5 test cases**. `AddObjectCommand::redo`
re-inserts under the same `ObjectId`, and a test checks the ID after five
undo/redo cycles.

**2. Can undo delete restore a different MaterialId?** No. `DeleteObjectCommand` keeps
the whole `unique_ptr<DocumentObject>` and restores it under its original ID; nothing
is reconstructed. Tested by comparing the whole definition, the name and the ID after
undo.

**3. Can delete/undo make a stale assignment bind to a same-named B?** No, and this is
the fixture the brief calls critical. Two materials share a *designation* — names are
unique, designations are not (P15-CUSTOM-001) — with an assignment to A. Across
delete, undo and redo the assignment names A at every step and `effectiveMaterial`
resolves A or nothing, never B. A further test adds the harder ordering: delete A,
*create* B with the same designation, then undo B's creation and A's deletion; the
assignment still resolves A.

The reason it cannot happen is upstream of these commands: an assignment holds a
`MaterialId`, and the allocator never reuses one, so the only material that ID can
ever name is the one that went away.

**4. Can command history identify materials by name?** No. Every command is keyed on
`MaterialId`; three compile-fail cases prove a name cannot be passed to assign, delete
or edit. `CreateMaterialCommand` does take a name, because that is the object name
being created — and it is the only one.

**5. Can duplicate-name materials confuse edit or delete?** They cannot both exist:
object names are unique across a document. Two materials with one *designation* are
legitimate and are used as the fixture throughout.

### Exact state restoration

**6. Can property removal undo restore zero instead of Unknown?** No. The command
stores complete `MaterialDefinition` snapshots, so a slot that was `Unknown` comes back
`Unknown` — the same object, not a reconstruction. Tested both directions, asserting
`isUnknown()` **and** `value().has_value() == false`, because a zero would satisfy only
the second.

**7. Can provenance undo restore the value but not the source?** No — this is what
whole-definition snapshots buy. A test edits a density from 7850 to 7900 *and* its
citation from `Measured`/"Rev A" to `UserEntered`, then checks the undo restores the
value, the kind, the revision and the fixed date together. **Mutation-tested:** an undo
that restored only the metadata fails the Known-restoration assertion and aborts.

**8. Can editing E store a derived G or K in history?** It cannot: `MechanicalProperties`
has **no slot** for either (ADR-027), so there is nothing for a definition snapshot to
carry. A test confirms G recomputes across undo and redo and stays `Derived`, never
`Known`.

**9. Can a density edit store the old mass in history?** No. A member-by-member audit
of all five commands:

```text
CreateMaterialCommand           name_, definition_, optional<AddObjectCommand>
DeleteMaterialCommand           id_, optional<DeleteObjectCommand>
EditMaterialCommand             id_, after_, optional<before_>   (definitions)
AssignMaterialCommand           id_, optional<optional<MaterialId>>
RemoveMaterialAssignmentCommand optional<optional<MaterialId>>
```

Every member is an ID, a name, a `MaterialDefinition` or a wrapped generic command.
Grepping the header and implementation for `mass`, `volume`, `centroid`,
`centreOfMass`, `centerOfMass`, `inertia`, `completeness`, `derivedShear`,
`derivedBulk` and `effectiveMaterial` finds **six hits, all in the comment that
asserts the absence**, and zero elsewhere. Five compile-fail cases prove there is no
setter or accessor for any of it.

**10. Can undo return a stale cached mass?** No, because there is no cache anywhere:
mass is derived on request (ADR-026). The integration test edits a density from 1000 to
2000, checks the mass doubles, and checks it halves back on undo — with the volume
asserted unchanged, so the mass can only have followed the density.

**11. Can an effective material be stored instead of the direct assignment?** No. The
commands store `std::optional<MaterialId>`, which is the document's canonical field; a
test checks the canonical field and the derived `effectiveMaterial` separately so the
two cannot be conflated.

**12. Can remove-assignment undo create a new material reference?** No. It restores the
previous `std::optional<MaterialId>` through `Document::setMaterialAssignment`, which
creates nothing. Undoing a remove on an absent assignment restores absence.

### History semantics

**13. Can a failed command enter history?** No. `CommandHistory::execute` records only
a command whose `execute()` returned success. Tested with four distinct failures — a
duplicate name, a zero density, an invalid identifier, and a missing material — each
checked to leave the undo depth, the material count and the document revision
unmoved, and then `undoDescription()` checked to still name the last *successful*
command.

**14. Can a failed command clear redo incorrectly?** No, and this needed its own test
because it is the opposite error from 13. A failed create after an undo leaves
`canRedo()` true, and the redo then works and restores the original ID.

**15. Can a new successful command fail to clear redo?** No. Create, edit to "X", undo,
then edit to "Y": `canRedo()` is false, `redoCount()` is 0, `redo()` fails, and the
state is "Y". Undoing twice then reaches the "Y" edit and the create — never the
discarded "X".

**16. Can undo after an invalid command target the invalid command?** No — see 13. The
failed attempt was never recorded, so undo reaches the previous successful command.

### Snapshots and ownership

**17. Can public clone semantics accidentally be used for an undo snapshot?** No, and
this distinction was the brief's sharpest point. `cloneMaterial` allocates a **new**
MaterialId (P15-CUSTOM-001); an undo snapshot must keep the **same** one. The commands
never call `cloneMaterial`. `CreateMaterialCommand` and `DeleteMaterialCommand` delegate
to the generic commands, which use `DocumentObject::clone()` — the identity-preserving
virtual that undo/redo exists for — and `EditMaterialCommand` snapshots a
`MaterialDefinition`, which carries no identity at all.

**18. Can a snapshot copy generate a new MaterialId?** Structurally no:
`MaterialDefinition` has no identity field, and two compile-fail cases in the `custom`
group prove `definition.id` and `definition.materialId` do not exist.

**19. Can pointer-based history dangle after a deletion?** No. No command stores a
pointer, a reference or an iterator into the document. `EditMaterialCommand` stores
definitions **by value**; the create and delete wrappers store owning
`unique_ptr<DocumentObject>`, which is what lets a deleted object survive in the
history at all.

**20. Can a built-in library material be changed through EditMaterialCommand?** No —
it has no `ObjectId`, so no command can name it. Two compile-fail cases prove a
`LibraryMaterial` cannot be passed to edit or delete. A test then deletes a material
**imported** from the library — which is an ordinary document object — and confirms the
library entry and the whole four-entry table are untouched.

**21. Can cross-document IDs mutate the wrong document?** No. `CommandHistory` binds to
the first document it is used with and refuses another; `assignMaterial` additionally
refuses an ID that does not name a material in *this* document. Tested with two
documents whose material IDs are numerically identical: the second document's
assignment stays absent and the first's is untouched.

**22. Can command ordering alter deterministic material enumeration?** No.
`materialIds()` walks the document's ordered object map by ascending ID, so an undone
and redone create returns to its place. Tested over five undo/redo cycles, and the
whole command sequence is checked to give `equivalent()` documents when run twice from
the same fixed DocumentId.

### Derived recomputation

**23. Can completeness reports be restored from stale history instead of recomputed?**
No. A report is built on demand from the definition; nothing stores one. Tested: `k`
Unknown gives `Incomplete` with `{ThermalConductivity}` missing, setting it gives
`Ready`, undo gives `Incomplete` again with the same missing list.

**24. Can a derived G or K be treated as canonical after a redo?** No. After a full
undo/redo round trip the derived shear modulus is still `isDerived()` and not
`isKnown()`. It cannot be otherwise — `validate()` refuses a stored property in the
`Derived` state, which a test exercises by attempting exactly that edit.

### My own four

**25. Does `undo()` or `redo()` before `execute()` guess at something?** No. All five
commands refuse with `FailedPrecondition`, matching the core commands' wording. Tested
for all ten calls, with the document checked untouched afterwards.

**26. Does a failed `execute()` leave a command half-executed and reusable?** No. Both
wrappers `reset()` their inner command when it fails, so a second attempt starts clean
rather than inheriting a partly-executed `AddObjectCommand`. `EditMaterialCommand`
records `before_` only after the edit succeeds, so a failed edit leaves the command
unexecuted and `undo()` correctly refuses.

**27. Does an undone create release its ID for reuse, so a redo could collide?** No,
and there are two independent guards. The allocator only ever counts up
(P15-MAT-001) — probed: create A gets 1, undo, create B gets **2**, not 1 — and
executing a new command clears redo, so the undone creation cannot be replayed into a
document that has moved on. Both are tested.

**28. Does removing an already-absent assignment behave sensibly?** Yes, and the
convention is borrowed rather than invented: `SetActiveConfigurationCommand` records
the previous value and succeeds on a no-op, so `RemoveMaterialAssignmentCommand` does
too. The document's revision does not move on a no-op, which is tested.
