#include <bettercad/features/MassProperties.hpp>

#include <bettercad/core/document/Configurations.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>

namespace bettercad::features {

namespace {

/// The mass-times-length-squared part of Huygens' theorem, so the dimension of
/// every term is checked by the type system rather than by inspection.
[[nodiscard]] MassMomentOfInertia moment(Mass mass, Length first, Length second) {
    return mass * first * second;
}

/// A x A^T for the symmetric 3x3 built from @p tensor, with A row-major.
[[nodiscard]] std::array<double, 9> conjugate(const std::array<double, 9>& a,
                                             const std::array<double, 6>& symmetric) {
    // The tensor as a full matrix: xx xy xz / xy yy yz / xz yz zz.
    const std::array<double, 9> i{symmetric[0], symmetric[3], symmetric[4],
                                  symmetric[3], symmetric[1], symmetric[5],
                                  symmetric[4], symmetric[5], symmetric[2]};
    std::array<double, 9> ai{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += a[3 * row + k] * i[3 * k + column];
            }
            ai[3 * row + column] = sum;
        }
    }
    std::array<double, 9> result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            double sum = 0.0;
            // (A I) A^T, so the second factor's row is A's row `column`.
            for (std::size_t k = 0; k < 3; ++k) {
                sum += ai[3 * row + k] * a[3 * column + k];
            }
            result[3 * row + column] = sum;
        }
    }
    return result;
}

[[nodiscard]] std::string describeState(std::optional<NodeState> state) {
    if (!state) {
        return "has never been regenerated";
    }
    switch (*state) {
        case NodeState::UpToDate:
        case NodeState::Regenerated:
            return "is up to date";
        case NodeState::Failed:
            return "failed to regenerate";
        case NodeState::Blocked:
            return "is blocked by a problem upstream";
    }
    return "is in an unknown state";
}

} // namespace

InertiaTensor shiftedFromCentroid(const InertiaTensor& centroidal, Mass mass, const Point3D& to) {
    // d = centre of mass - the new reference point.
    const Length dx = centroidal.about.x - to.x;
    const Length dy = centroidal.about.y - to.y;
    const Length dz = centroidal.about.z - to.z;
    InertiaTensor shifted;
    shifted.xx = centroidal.xx + moment(mass, dy, dy) + moment(mass, dz, dz);
    shifted.yy = centroidal.yy + moment(mass, dx, dx) + moment(mass, dz, dz);
    shifted.zz = centroidal.zz + moment(mass, dx, dx) + moment(mass, dy, dy);
    // Minus, because these are tensor components: the point-mass contribution to
    // the off-diagonal is -m dx dy, matching xy = -integral xy dm.
    shifted.xy = centroidal.xy - moment(mass, dx, dy);
    shifted.xz = centroidal.xz - moment(mass, dx, dz);
    shifted.yz = centroidal.yz - moment(mass, dy, dz);
    shifted.about = to;
    return shifted;
}

InertiaTensor transformed(const InertiaTensor& tensor, const RigidTransform3D& motion) {
    // The SI values, conjugated, and put back. Working in SI rather than in a
    // display unit keeps the arithmetic in one system; Quantity has no meaning
    // attached to its SI number beyond its dimension.
    const std::array<double, 6> si{tensor.xx.si(), tensor.yy.si(), tensor.zz.si(),
                                   tensor.xy.si(), tensor.xz.si(), tensor.yz.si()};
    const std::array<double, 9> turned = conjugate(motion.matrix(), si);
    InertiaTensor result;
    result.xx = MassMomentOfInertia::fromSi(turned[0]);
    result.yy = MassMomentOfInertia::fromSi(turned[4]);
    result.zz = MassMomentOfInertia::fromSi(turned[8]);
    result.xy = MassMomentOfInertia::fromSi(turned[1]);
    result.xz = MassMomentOfInertia::fromSi(turned[2]);
    result.yz = MassMomentOfInertia::fromSi(turned[5]);
    result.about = motion.apply(tensor.about);
    return result;
}

Result<PartMassProperties> partMassProperties(const Document& document,
                                              const Regenerator& regenerator, ObjectId feature) {
    // The carried regeneration defect, refused rather than answered wrongly. A
    // configuration override changes a parameter's EFFECTIVE value without
    // changing the parameter object, so the regenerator does not mark the features
    // that read it dirty and their bodies are still the base configuration's. Mass
    // is density x volume, so answering here would multiply a correct density by a
    // volume for the wrong part and report it as fact.
    //
    // Checked before anything else, so that no amount of valid material and valid
    // geometry can talk this into answering.
    if (const std::optional<ConfigurationId> active = document.activeConfiguration()) {
        const Configuration* configuration = document.configurations().find(*active);
        if (configuration != nullptr && !configuration->overrides().empty()) {
            return makeError(
                ErrorCode::FailedPrecondition,
                std::format("mass properties are not available while the configuration '{}' is "
                            "active: it overrides {} parameter(s), and a configuration override "
                            "does not yet rebuild the geometry it changes, so the volume in hand "
                            "is the base configuration's. Activate the base configuration to get "
                            "mass properties",
                            configuration->name(), configuration->overrides().size()));
        }
    }

    // The material, and then the density. Two steps because they fail for
    // different reasons and the user needs to be told which: "nothing is
    // assigned", "what was assigned is gone", "what is assigned states no
    // density". requireEffectiveMaterial and requireDensity each own their own
    // diagnostic (ADR-026, ADR-027).
    const Result<const Material*> material = requireEffectiveMaterial(document);
    if (!material) {
        return std::unexpected(material.error());
    }
    const Result<Density> density = requireDensity(document, (*material)->materialId());
    if (!density) {
        return std::unexpected(density.error());
    }

    const DocumentObject* object = document.findObject(feature);
    if (object == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("there is no object {} to take mass properties of", feature));
    }
    const geometry::Body* body = regenerator.body(feature);
    if (body == nullptr) {
        // Distinguish "it broke" from "it never makes one": a sketch has no body
        // and never will, and telling the user to regenerate would be wrong.
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("'{}' ({}, {}) has no body to take mass properties of; it {}",
                                     object->name(), object->typeName(), feature,
                                     describeState(regenerator.state(feature))));
    }
    if (const std::optional<NodeState> state = regenerator.state(feature);
        state == NodeState::Failed || state == NodeState::Blocked) {
        // UNREACHABLE TODAY, and kept deliberately. features::Regenerator drops the
        // body of a failed or blocked item rather than keeping the last good one
        // ("Failed and blocked items have no (stale) result", RegeneratorTests), so
        // the nullptr check above fires first and this never does -- which
        // tests/features/MassPropertiesTests.cpp asserts, so the day that changes it
        // is a failing test rather than a silent wrong number.
        //
        // It costs one map lookup, and what it guards against is a mass computed
        // from geometry that no longer follows from the document. That is worth a
        // lookup even at a probability of zero.
        return makeError(
            ErrorCode::FailedPrecondition,
            std::format("the mass properties of '{}' ({}) would be taken from stale geometry: it {}",
                        object->name(), feature, describeState(state)));
    }

    const Result<geometry::MassProperties> geometric = body->massProperties();
    if (!geometric) {
        return std::unexpected(geometric.error());
    }
    if (!(geometric->volume.si() > 0.0) || !isFinite(geometric->volume)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("'{}' ({}) encloses no usable volume, so it has no mass",
                                     object->name(), feature));
    }
    const Result<geometry::VolumeSecondMoments> moments = body->centroidalVolumeSecondMoments();
    if (!moments) {
        return std::unexpected(moments.error());
    }

    PartMassProperties result;
    result.feature = feature;
    result.material = (*material)->materialId();
    result.density = *density;
    result.volume = geometric->volume;
    result.mass = *density * geometric->volume;
    result.centreOfMass = geometric->centerOfMass;
    // Density times a second moment of VOLUME is a second moment of MASS. The
    // dimensions carry that; nothing here converts or scales.
    result.aboutCentreOfMass.xx = *density * moments->xx;
    result.aboutCentreOfMass.yy = *density * moments->yy;
    result.aboutCentreOfMass.zz = *density * moments->zz;
    result.aboutCentreOfMass.xy = *density * moments->xy;
    result.aboutCentreOfMass.xz = *density * moments->xz;
    result.aboutCentreOfMass.yz = *density * moments->yz;
    result.aboutCentreOfMass.about = geometric->centerOfMass;
    result.aboutOrigin = shiftedFromCentroid(result.aboutCentreOfMass, result.mass, Point3D{});
    return result;
}

PartMassProperties transformed(const PartMassProperties& properties,
                               const RigidTransform3D& motion) {
    PartMassProperties moved = properties;
    moved.centreOfMass = motion.apply(properties.centreOfMass);
    // The centroidal tensor turns with the body and keeps its reference point,
    // which rotated() moves for it.
    moved.aboutCentreOfMass = transformed(properties.aboutCentreOfMass, motion);
    // The ORIGIN does not travel with the body, so its tensor is re-derived rather
    // than transformed: the body is now somewhere else relative to a fixed point.
    moved.aboutOrigin = shiftedFromCentroid(moved.aboutCentreOfMass, moved.mass, Point3D{});
    return moved;
}

} // namespace bettercad::features
