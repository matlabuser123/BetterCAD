#include <bettercad/drawing/Annotation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

[[nodiscard]] std::unexpected<Error> wrong(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

} // namespace

std::string_view toString(AnnotationType type) noexcept {
    switch (type) {
    case AnnotationType::Note:
        return "note";
    case AnnotationType::Leader:
        return "leader";
    case AnnotationType::Centreline:
        return "centreline";
    case AnnotationType::Centremark:
        return "centremark";
    case AnnotationType::HoleCallout:
        return "hole_callout";
    case AnnotationType::SurfaceFinish:
        return "surface_finish";
    case AnnotationType::Datum:
        return "datum";
    }
    return "unknown";
}

std::optional<AnnotationType> annotationTypeFromString(std::string_view text) noexcept {
    for (const AnnotationType type :
         {AnnotationType::Note, AnnotationType::Leader, AnnotationType::Centreline,
          AnnotationType::Centremark, AnnotationType::HoleCallout, AnnotationType::SurfaceFinish,
          AnnotationType::Datum}) {
        if (toString(type) == text) {
            return type;
        }
    }
    return std::nullopt;
}

std::string_view toString(MaterialRemoval removal) noexcept {
    switch (removal) {
    case MaterialRemoval::Any:
        return "any";
    case MaterialRemoval::Required:
        return "required";
    case MaterialRemoval::Prohibited:
        return "prohibited";
    }
    return "unknown";
}

std::optional<MaterialRemoval> materialRemovalFromString(std::string_view text) noexcept {
    for (const MaterialRemoval removal :
         {MaterialRemoval::Any, MaterialRemoval::Required, MaterialRemoval::Prohibited}) {
        if (toString(removal) == text) {
            return removal;
        }
    }
    return std::nullopt;
}

bool isModelDriven(AnnotationType type) noexcept {
    return type == AnnotationType::HoleCallout;
}

bool needsTarget(AnnotationType type) noexcept { return type != AnnotationType::Note; }

bool isEmpty(const AnnotationTarget& target) noexcept {
    return !target.plane && !target.axis && !target.cylinder && !target.object;
}

Result<void> validate(const AnnotationTarget& target) {
    const int named = static_cast<int>(target.plane.has_value()) +
                      static_cast<int>(target.axis.has_value()) +
                      static_cast<int>(target.cylinder.has_value()) +
                      static_cast<int>(target.object.has_value());
    if (named > 1) {
        return wrong("an annotation points at one thing: a plane, an axis, a cylindrical face or "
                     "an object, not several");
    }
    if (target.plane) {
        return validate(*target.plane);
    }
    if (target.cylinder) {
        return validate(target.cylinder->face);
    }
    if (target.object && !target.object->isValid()) {
        return wrong("an annotation cannot point at an object with no identity");
    }
    return {};
}

std::vector<ObjectId> referencedObjects(const AnnotationTarget& target) {
    if (target.plane) {
        return referencedObjects(*target.plane);
    }
    if (target.axis) {
        return target.axis->object ? std::vector<ObjectId>{*target.axis->object}
                                   : std::vector<ObjectId>{};
    }
    if (target.cylinder) {
        std::vector<ObjectId> objects{target.cylinder->feature};
        for (const FaceCopy& copy : target.cylinder->face.copies) {
            if (std::ranges::find(objects, copy.feature) == objects.end()) {
                objects.push_back(copy.feature);
            }
        }
        return objects;
    }
    if (target.object) {
        return {*target.object};
    }
    return {};
}

Result<void> validate(const TextStyle& style) {
    if (!std::isfinite(style.height.si()) || style.height.si() <= 0.0) {
        return wrong("a text height must be finite and greater than zero");
    }
    return {};
}

Result<void> validate(const SurfaceFinish& finish) {
    if (!std::isfinite(finish.roughness.si()) || finish.roughness.si() <= 0.0) {
        return wrong("a roughness must be finite and greater than zero");
    }
    if (toString(finish.removal) == "unknown") {
        return wrong("a surface finish must say whether material removal is required");
    }
    return {};
}

Result<void> validate(const AnnotationDefinition& definition) {
    if (!definition.view.isValid()) {
        return wrong("an annotation must name the view it belongs to");
    }
    if (toString(definition.type) == "unknown") {
        return wrong("an annotation must have a known type");
    }

    // A note points at nothing; everything else points at something. A note
    // that carried a target would be a leader whose leader was forgotten.
    const bool wanted = needsTarget(definition.type);
    if (wanted && isEmpty(definition.target)) {
        return wrong(std::format("a {} annotation must say what it is about",
                                 toString(definition.type)));
    }
    if (!wanted && !isEmpty(definition.target)) {
        return wrong("a note is placed on the sheet and points at nothing; use a leader to point");
    }
    if (auto valid = validate(definition.target); !valid) {
        return std::unexpected(valid.error());
    }

    // The words are intent for the kinds an engineer chooses them for, and
    // DERIVED for the ones the model decides. Storing a hole callout's text
    // would be storing a number that the hole could then disagree with.
    if (isModelDriven(definition.type)) {
        if (!definition.text.empty()) {
            return wrong(std::format("a {} annotation takes its words from the model and stores "
                                     "none of its own",
                                     toString(definition.type)));
        }
    } else if (definition.type == AnnotationType::Note ||
               definition.type == AnnotationType::Leader ||
               definition.type == AnnotationType::Datum) {
        if (definition.text.empty()) {
            return wrong(std::format("a {} annotation must say something", toString(definition.type)));
        }
    } else if (!definition.text.empty()) {
        return wrong(std::format("a {} annotation draws a symbol and takes no words",
                                 toString(definition.type)));
    }

    // A datum is one letter, because that is what a later feature-control
    // frame will refer to. ISO 5459 uses capitals and skips I, O and Q,
    // which are too easily read as 1 and 0.
    if (definition.type == AnnotationType::Datum) {
        if (definition.text.size() != 1) {
            return wrong("a datum is identified by a single letter");
        }
        const char letter = definition.text.front();
        if (letter < 'A' || letter > 'Z') {
            return wrong("a datum's letter must be a capital A to Z");
        }
        if (letter == 'I' || letter == 'O' || letter == 'Q') {
            return wrong("ISO 5459 does not use I, O or Q as datum letters: they read as 1 and 0");
        }
    }

    if (auto valid = validate(definition.style); !valid) {
        return std::unexpected(valid.error());
    }

    if ((definition.type == AnnotationType::SurfaceFinish) != definition.finish.has_value()) {
        return wrong(definition.type == AnnotationType::SurfaceFinish
                         ? "a surface-finish annotation must give a roughness"
                         : std::format("a {} annotation takes no roughness",
                                       toString(definition.type)));
    }
    if (definition.finish) {
        if (auto valid = validate(*definition.finish); !valid) {
            return std::unexpected(valid.error());
        }
    }

    // Paper lengths. Each is checked where its kind uses it, so an
    // unreasonable arm length on a note is not an error nobody could act on.
    if (definition.type == AnnotationType::Centreline &&
        (!std::isfinite(definition.extension.si()) || definition.extension.si() < 0.0)) {
        return wrong("a centreline's extension must be finite and not negative");
    }
    if (definition.type == AnnotationType::Centremark &&
        (!std::isfinite(definition.armLength.si()) || definition.armLength.si() <= 0.0)) {
        return wrong("a centre mark's arms must be finite and longer than nothing");
    }
    for (const auto& [name, value] : std::array<std::pair<std::string_view, Length>, 2>{
             {{"x", definition.placement.x}, {"y", definition.placement.y}}}) {
        if (!std::isfinite(value.si())) {
            return wrong(std::format("an annotation's {} placement must be finite", name));
        }
    }
    return {};
}

Result<std::unique_ptr<Annotation>> Annotation::create(std::string name,
                                                       const AnnotationDefinition& definition) {
    if (auto valid = validateObjectName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<Annotation>(new Annotation(std::move(name), definition));
}

Annotation::Annotation(std::string name, const AnnotationDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

std::unique_ptr<DocumentObject> Annotation::clone() const {
    return std::unique_ptr<Annotation>(new Annotation(*this));
}

bool Annotation::contentEquals(const DocumentObject& other) const {
    const auto* annotation = dynamic_cast<const Annotation*>(&other);
    return annotation != nullptr && annotation->definition_ == definition_;
}

std::vector<ObjectId> Annotation::dependencies() const {
    std::vector<ObjectId> result;
    result.push_back(ObjectId{definition_.view});
    for (const ObjectId object : referencedObjects(definition_.target)) {
        if (object.isValid() && std::ranges::find(result, object) == result.end()) {
            result.push_back(object);
        }
    }
    return result;
}

Result<bool> Annotation::setDefinition(const AnnotationDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

} // namespace bettercad::drawing
