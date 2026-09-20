#include <bettercad/assembly/Mate.hpp>

#include <cmath>
#include <format>
#include <numbers>
#include <utility>

namespace bettercad::assembly {
namespace {

/// Whether @p type relates two pieces of geometry rather than holding one
/// component.
[[nodiscard]] bool relatesGeometry(MateType type) noexcept {
    return type != MateType::Fixed;
}

/// Whether a pair of target kinds is one this constraint can relate.
///
/// Only like with like: two planar targets, or two axes. A plane paired with
/// an axis is refused rather than guessed at, because "a plane parallel to an
/// axis" and "a plane whose normal is parallel to an axis" are opposite
/// statements and nothing in the model says which was meant. A later
/// milestone may add mixed pairs with an explicit convention.
[[nodiscard]] bool relatable(const MateTarget& a, const MateTarget& b) noexcept {
    return (isPlanar(a) && isPlanar(b)) || (a.kind == MateTargetKind::Axis && b.kind == MateTargetKind::Axis);
}

/// Whether @p type relates two axes. The three joints built on a collinear
/// pair of axes do; the planar joint relates two planes.
[[nodiscard]] bool relatesAxes(MateType type) noexcept {
    return type == MateType::Revolute || type == MateType::Slider || type == MateType::Cylindrical ||
           type == MateType::Concentric;
}

[[nodiscard]] std::unexpected<Error> wrong(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

} // namespace

std::string_view toString(MateType type) noexcept {
    switch (type) {
    case MateType::Fixed:
        return "fixed";
    case MateType::Coincident:
        return "coincident";
    case MateType::Concentric:
        return "concentric";
    case MateType::Parallel:
        return "parallel";
    case MateType::Perpendicular:
        return "perpendicular";
    case MateType::Distance:
        return "distance";
    case MateType::Angle:
        return "angle";
    case MateType::Revolute:
        return "revolute";
    case MateType::Slider:
        return "slider";
    case MateType::Cylindrical:
        return "cylindrical";
    case MateType::Planar:
        return "planar";
    }
    return "unknown";
}

Result<void> validate(const MateDefinition& definition) {
    const std::string_view name = toString(definition.type);

    // --- the fields the kind calls for, and no others ------------------
    if (!relatesGeometry(definition.type)) {
        if (!definition.component.isValid()) {
            return wrong("a fixed mate must name the component it holds");
        }
        if (definition.a || definition.b) {
            return wrong("a fixed mate holds a component, so it names no geometry");
        }
    } else {
        if (definition.component.isValid()) {
            return wrong(std::format("a {} mate relates geometry, so it names no component of its own", name));
        }
        if (!definition.a || !definition.b) {
            return wrong(std::format("a {} mate must name two pieces of geometry", name));
        }
    }

    // --- the roll reference --------------------------------------------
    // A slide must have one; nothing else may. A kind that carried a roll
    // reference it ignored would be a field that means nothing, which is
    // what this validation exists to prevent.
    const bool wantsRoll = definition.type == MateType::Slider;
    if (definition.a2.has_value() != wantsRoll || definition.b2.has_value() != wantsRoll) {
        return wrong(wantsRoll ? "a slider mate must name a roll reference on each component"
                               : std::format("a {} mate takes no roll reference", name));
    }
    if (wantsRoll) {
        for (const MateTarget* target : {&*definition.a2, &*definition.b2}) {
            if (auto valid = validate(*target); !valid) {
                return std::unexpected(valid.error());
            }
            if (!hasDirection(*target)) {
                return wrong("a slider mate's roll reference needs geometry with a direction");
            }
        }
        // The roll reference fixes one component's turn against the other's,
        // so each side's reference must be on that side's component.
        if (definition.a2->component != definition.a->component ||
            definition.b2->component != definition.b->component) {
            return wrong("a slider mate's roll references must be on the same components as its axes");
        }
        if (*definition.a2 == *definition.a || *definition.b2 == *definition.b) {
            // Rolling a direction against itself says nothing.
            return wrong("a slider mate's roll reference must differ from its axis");
        }
    }

    const bool wantsDistance = definition.type == MateType::Distance;
    const bool wantsAngle = definition.type == MateType::Angle;
    if (definition.distance.has_value() != wantsDistance) {
        return wrong(wantsDistance ? "a distance mate must have a distance"
                                   : std::format("a {} mate takes no distance", name));
    }
    if (definition.angle.has_value() != wantsAngle) {
        return wrong(wantsAngle ? "an angle mate must have an angle" : std::format("a {} mate takes no angle", name));
    }

    if (!relatesGeometry(definition.type)) {
        return {};
    }

    // --- the targets ---------------------------------------------------
    if (auto valid = validate(*definition.a); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate(*definition.b); !valid) {
        return std::unexpected(valid.error());
    }
    if (*definition.a == *definition.b) {
        return wrong(std::format("a {} mate cannot relate geometry to itself", name));
    }
    if (definition.a->component == definition.b->component) {
        // A component is rigid, so constraining two of its own faces to each
        // other says nothing and can only over-constrain the solve.
        return wrong(std::format("a {} mate must relate two different components", name));
    }

    // --- the kinds the constraint can relate ---------------------------
    if (relatesAxes(definition.type)) {
        if (definition.a->kind != MateTargetKind::Axis || definition.b->kind != MateTargetKind::Axis) {
            return wrong(std::format("a {} mate relates two axes", name));
        }
    } else if (definition.type == MateType::Planar) {
        if (!isPlanar(*definition.a) || !isPlanar(*definition.b)) {
            return wrong("a planar mate relates two planes");
        }
    } else if (!relatable(*definition.a, *definition.b)) {
        return wrong(std::format("a {} mate relates two planes or two axes, not a {} and a {}", name,
                                 toString(definition.a->kind), toString(definition.b->kind)));
    }
    if ((definition.type == MateType::Parallel || definition.type == MateType::Perpendicular ||
         definition.type == MateType::Angle) &&
        (!hasDirection(*definition.a) || !hasDirection(*definition.b))) {
        return wrong(std::format("a {} mate needs geometry with a direction", name));
    }

    // --- the value -----------------------------------------------------
    if (wantsDistance) {
        if (!isFinite(*definition.distance)) {
            return wrong("a distance mate's distance must be finite");
        }
        // Between planes the distance is signed, along the first normal.
        // Between axes it is a perpendicular separation, which has no side.
        if (definition.a->kind == MateTargetKind::Axis && definition.distance->si() < 0.0) {
            return wrong("the distance between two axes is a separation and cannot be negative");
        }
    }
    if (wantsAngle) {
        if (!isFinite(*definition.angle)) {
            return wrong("an angle mate's angle must be finite");
        }
        const double radians = definition.angle->si();
        if (radians < 0.0 || radians > std::numbers::pi) {
            // Refused, never normalised: wrapping 190 degrees to 170 would be
            // the model deciding what was meant.
            return wrong("an angle mate's angle is the unsigned angle between two directions, from 0 to 180 degrees");
        }
    }
    return {};
}

Mate::Mate(std::string name, const MateDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<Mate>> Mate::create(std::string name, const MateDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<Mate>(new Mate(std::move(name), definition));
}

std::unique_ptr<DocumentObject> Mate::clone() const {
    return std::unique_ptr<Mate>(new Mate(*this));
}

bool Mate::contentEquals(const DocumentObject& other) const {
    const auto* mate = dynamic_cast<const Mate*>(&other);
    return mate != nullptr && mate->definition_ == definition_;
}

std::vector<ObjectId> Mate::dependencies() const {
    std::vector<ObjectId> result;
    const auto add = [&result](const std::vector<ObjectId>& ids) {
        for (const ObjectId id : ids) {
            if (std::ranges::find(result, id) == result.end()) {
                result.push_back(id);
            }
        }
    };
    if (definition_.component.isValid()) {
        add({ObjectId{definition_.component}});
    }
    if (definition_.a) {
        add(referencedObjects(*definition_.a));
    }
    if (definition_.b) {
        add(referencedObjects(*definition_.b));
    }
    if (definition_.a2) {
        add(referencedObjects(*definition_.a2));
    }
    if (definition_.b2) {
        add(referencedObjects(*definition_.b2));
    }
    return result;
}

Result<bool> Mate::setDefinition(const MateDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

} // namespace bettercad::assembly
