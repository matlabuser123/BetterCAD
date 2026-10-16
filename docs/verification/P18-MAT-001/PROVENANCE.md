# P18-MAT-001 — material provenance

```text
A resolved thermal material binds to    MaterialId  +  Material::revision()
A thermal result binds to               the same pair, in ThermalResultSource
                                        (P18-DATA-001), plus everything else
                                        it was computed from
```

## MaterialId alone is not enough, and this is the proof

`ThermalMaterial_TheSameIdWithAChangedConductivityIsADifferentInput`:

```text
before:  MaterialId M, designation "Steel", k = 42.75 W/(m K), revision 1
after:   MaterialId M, designation "Steel", k = 50.00 W/(m K), revision 2

resolved.id()          UNCHANGED
resolved.revision()    1 -> 2
resolved ==            false        <- a different solver input
```

Currentness that compared the id would call the old result current, and the
user would read a temperature field computed from a conductivity their model
no longer has. `ThermalResultSource` carries **both** fields for exactly this
case, and `ThermalMaterial::operator==` compares the effective inputs and the
source identity rather than the id.

## Three provenances, all distinct

`ThermalMaterial_ProvenanceDistinguishesIdAndRevision`:

```text
A, revision 1    k 42.75, rho 7853.25, cp 486.5
A, revision 2    cp edited to 490
B, revision 1    k 42.75, rho 7853.25, cp 486.5   -- the SAME numbers A had

A1 vs A2    same id, different revision     not equal
A1 vs B1    same k, same rho, same cp       NOT EQUAL -- identity differs
```

Same values, new canonical identity: not equal, by source authority. Nothing
in P18 deep-compares `k`, `rho` and `cp` to decide currentness.

## Where the revision comes from

`features::Material::revision()`, which is `DocumentObject::revision()`: it
starts at 1 and moves by one on every effective `setDefinition` -- every
`setMaterialMechanical`, `setMaterialThermal`, `removeMaterialProperty`,
`setMaterialProvenance` that changes something. A no-op edit does not move it
(measured in `AStructuralEditStalesTheThermalResultUnderWholeMaterialRevision`),
and a REFUSED edit does not move it (measured in `AFailedMaterialEditChangesNothing`).

Nothing time-dependent, nothing pointer-based: an id and a counter.

## The derived view carries no copy of the record

```text
sizeof(ThermalMaterial) == sizeof({MaterialId, uint64_t, ThermalAnalysisMode,
                                   ThermalConductivity, optional<ThermalCapacity>})
```

asserted at compile time. No designation, no provenance text, no library key,
no `Material*`. The view points back; it does not duplicate.
