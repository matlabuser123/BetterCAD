#pragma once

// P17-BC-001 -- canonical zero-displacement restraint intent.
//
// THE AUTHORITY RULE, WHICH P17-DATA-001 WROTE FOR THIS MILESTONE. `RestraintId`
// carries the comment "A restraint's identity is the intent; the constrained
// DOF indices are a derived consequence of the current mesh and the current
// numbering, and are never its identity." So:
//
//     canonical      RestraintId + FaceName + the components held at zero
//     derived        current NodeIds, current DofIndices, the ConstraintSet
//
// and the roles are never reversed. A canonical restraint holds no facet
// handle, no node handle and no DOF index -- asserted at compile time, not
// reviewed.
//
// COMPONENTS ARE GLOBAL AXES. `Ux` is global X, whatever the face's normal is
// doing. A restrained face on a rotated body still holds global X at zero, and
// the convention is frozen here: normal-only, tangential, frictionless and
// roller supports would each need a face-local basis, and all four are out of
// scope.
//
// ZERO ONLY, AND THERE IS NO VALUE FIELD. Prescribed non-zero displacement is
// deliberately deferred, so the schema carries a component mask and nothing
// else -- a `double value = 0` that only ever supported zero would be a false
// capability. A later milestone can add one without touching `RestraintId` or
// the CAD target.
//
// THIS HEADER IS CANONICAL INTENT ONLY, for the reason `StructuralLoad.hpp`
// records: the restraints live in `StructuralAnalysisDefinition`, so the
// document object's header includes this one, and the derived machinery would
// otherwise drag the whole input boundary in behind it. The resolution to a
// constrained DOF set is in `StructuralConstraints.hpp`.

#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralDof.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bettercad::structural {

/// Which displacement components a restraint holds at zero.
///
/// A STRONG TYPE OVER `DofComponent`, NOT A BARE INTEGER. The three components
/// could be a `1 | 2 | 4` bitfield and a reader would then have to know that
/// `7` means fixed; here `fixed()` says so, `holds()` asks in component terms,
/// and a mask cannot be confused with a count or an index.
///
/// Built from `kDofComponents` and `offsetOf`, both P17-DOF's, so the bit
/// order cannot drift from the degree-of-freedom ordering it indexes.
///
/// AN EMPTY MASK IS REPRESENTABLE AND INVALID. It has to be representable --
/// that is what a default-constructed value is -- and `validate()` refuses it,
/// because a restraint that holds nothing at zero is not a weaker restraint,
/// it is a record whose intent cannot be acted on.
class RestraintComponents {
public:
    using ValueType = std::uint8_t;

    /// No components. Refused by `validate(StructuralRestraint)`.
    constexpr RestraintComponents() noexcept = default;

    /// One component.
    [[nodiscard]] static constexpr RestraintComponents along(DofComponent component) noexcept {
        RestraintComponents mask;
        mask.add(component);
        return mask;
    }

    /// All three: a fixed support. There is exactly one spelling of "fixed" in
    /// the tree, and this is it.
    [[nodiscard]] static constexpr RestraintComponents fixed() noexcept {
        RestraintComponents mask;
        for (const DofComponent component : kDofComponents) {
            mask.add(component);
        }
        return mask;
    }

    /// Adds @p component. Idempotent, so adding one twice is one component.
    constexpr RestraintComponents& add(DofComponent component) noexcept {
        const std::size_t offset = offsetOf(component);
        if (offset < kDofsPerNode) {
            bits_ = static_cast<ValueType>(bits_ | (ValueType{1} << offset));
        }
        return *this;
    }

    [[nodiscard]] constexpr bool holds(DofComponent component) const noexcept {
        const std::size_t offset = offsetOf(component);
        return offset < kDofsPerNode && (bits_ & (ValueType{1} << offset)) != 0;
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept {
        std::size_t total = 0;
        for (const DofComponent component : kDofComponents) {
            if (holds(component)) {
                ++total;
            }
        }
        return total;
    }

    [[nodiscard]] constexpr bool isEmpty() const noexcept { return count() == 0; }
    [[nodiscard]] constexpr bool isFixed() const noexcept { return count() == kDofsPerNode; }

    /// The union of two masks: what two restraints on one node hold together.
    [[nodiscard]] constexpr RestraintComponents unionWith(
        const RestraintComponents& other) const noexcept {
        RestraintComponents both;
        both.bits_ = static_cast<ValueType>(bits_ | other.bits_);
        return both;
    }

    friend constexpr bool operator==(const RestraintComponents&,
                                     const RestraintComponents&) noexcept = default;

private:
    ValueType bits_ = 0;
};

static_assert(RestraintComponents{}.isEmpty());
static_assert(RestraintComponents::fixed().isFixed());
static_assert(RestraintComponents::fixed().count() == 3);
static_assert(RestraintComponents::along(DofComponent::Ux).count() == 1);
static_assert(RestraintComponents::along(DofComponent::Ux).holds(DofComponent::Ux));
static_assert(!RestraintComponents::along(DofComponent::Ux).holds(DofComponent::Uy));
/// Three single-component masks unioned ARE a fixed support, which is the
/// equivalence `StructuralBC_FixedEqualsTheThreeComponentsCombined` asserts
/// through the whole pipeline.
static_assert(RestraintComponents::along(DofComponent::Ux)
                  .unionWith(RestraintComponents::along(DofComponent::Uy))
                  .unionWith(RestraintComponents::along(DofComponent::Uz)) ==
              RestraintComponents::fixed());

/// A human-readable mask: "ux", "ux+uz", "fixed".
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string
toString(const RestraintComponents& components);

/// One canonical restraint: an identity, a CAD target, and the components it
/// holds at zero.
///
/// NOT A VARIANT, because there is one target kind. P17-LOAD's
/// `StructuralLoad` is a variant because its four payloads carry different
/// units and different targets; a restraint has one payload shape, and a
/// variant of one would be noise. An edge or vertex restraint would need a
/// canonical edge or vertex reference P16 does not provide, and adding one
/// later is a change to this type rather than a reason to pre-build a variant.
class BETTERCAD_STRUCTURAL_EXPORT StructuralRestraint {
public:
    StructuralRestraint(RestraintId id, FaceName face, RestraintComponents components)
        : id_(id), face_(std::move(face)), components_(components) {}

    /// A fixed support on @p face: all three components at zero.
    [[nodiscard]] static StructuralRestraint fixedSupport(RestraintId id, FaceName face) {
        return StructuralRestraint{id, std::move(face), RestraintComponents::fixed()};
    }

    /// The persisted document identity, from P17-DATA-001.
    [[nodiscard]] RestraintId id() const noexcept { return id_; }
    /// The canonical CAD target, and the only one.
    [[nodiscard]] const FaceName& face() const noexcept { return face_; }
    [[nodiscard]] const RestraintComponents& components() const noexcept { return components_; }

    friend bool operator==(const StructuralRestraint&, const StructuralRestraint&) = default;

private:
    RestraintId id_{};
    FaceName face_{};
    RestraintComponents components_{};
};

/// Checks a restraint on its own terms: a valid id, a valid selector, and at
/// least one component.
///
/// Deliberately does NOT consult a mesh. Whether the face resolves is a
/// question about a particular mesh and belongs to preparation; whether the
/// record is well formed is a question about the record, and is this.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<void>
validate(const StructuralRestraint& restraint);

} // namespace bettercad::structural
