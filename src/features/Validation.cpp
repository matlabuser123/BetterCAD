#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchRegeneration.hpp>
#include <bettercad/sketch/Solver.hpp>

#include <algorithm>
#include <format>
#include <set>
#include <variant>

namespace bettercad::features {

namespace {

/// "Name (object:7)", or just "object:7" for an ID that names nothing.
std::string label(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::format("{} ({})", *name, id) : std::format("{}", id);
}

/// "a sketch", "an extrude", ...
std::string withArticle(std::string_view noun) {
    const bool vowel = !noun.empty() && std::string_view{"aeiou"}.find(noun.front()) != std::string_view::npos;
    return std::format("{} {}", vowel ? "an" : "a", noun);
}

/// What kind of item @p id is, for messages: "a parameter", "a sketch", ...
std::string kindOf(const Document& document, ObjectId id) {
    if (document.asParameter(id)) {
        return "a parameter";
    }
    const DocumentObject* object = document.findObject(id);
    return object == nullptr ? "a missing item" : withArticle(object->typeName());
}

/// "an angle", "dimensionless", ...
std::string describeQuantity(const Dimension& dimension) {
    return dimension == dimensions::dimensionless ? std::string{"dimensionless"}
                                                  : withArticle(describeDimension(dimension));
}

class Checker {
public:
    explicit Checker(const Document& document) : document_(document) {}

    ValidationReport run() {
        checkConsistency();
        const DocumentGraph graph = buildDependencyGraph(document_);
        checkMissingReferences(graph);
        checkCycles(graph);
        checkSketches();
        checkRegenerationAndGeometry();
        return std::move(report_);
    }

private:
    void add(ValidationCheck check, Severity severity, std::optional<ObjectId> item, std::string message) {
        if (severity == Severity::Error && item) {
            failedItems_.insert(*item);
        }
        report_.issues.push_back({check, severity, item, std::move(message)});
    }

    /// "<owner>: <reference> <item>, which is <actual>, not <expected>"
    void wrongKind(ObjectId owner, std::string_view reference, ObjectId item, std::string_view actual,
                   std::string_view expected) {
        add(ValidationCheck::DocumentConsistency, Severity::Error, owner,
            std::format("{}: {} {}, which is {}, not {}", label(document_, owner), reference, label(document_, item),
                        actual, expected));
    }

    /// A reference that exists must name a parameter of dimension @p expected.
    void checkParameter(ObjectId owner, ParameterId parameter, const Dimension& expected,
                        std::string_view reference) {
        const ObjectId id{parameter};
        if (!document_.contains(id)) {
            return; // a missing reference
        }
        const Parameter* found = document_.parameters().find(parameter);
        if (found == nullptr) {
            wrongKind(owner, reference, id, kindOf(document_, id), "a parameter");
        } else if (found->dimension() != expected) {
            wrongKind(owner, reference, id, describeQuantity(found->dimension()), describeQuantity(expected));
        }
    }

    /// A profile reference that exists must name a sketch.
    void checkProfile(ObjectId owner, SketchId profile) {
        const ObjectId id{profile};
        if (document_.contains(id) && document_.findObjectAs<sketch::Sketch>(id) == nullptr) {
            wrongKind(owner, "the profile is", id, kindOf(document_, id), "a sketch");
        }
    }

    void checkConsistency() {
        for (const DocumentObject& object : document_.objects()) {
            if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
                for (const sketch::Constraint& constraint : sketch->constraints()) {
                    if (constraint.parameter) {
                        checkParameter(object.id(), *constraint.parameter, dimensions::length,
                                       std::format("{} is driven by", constraint.id));
                    }
                }
            } else if (const auto* extrude = dynamic_cast<const ExtrudeFeature*>(&object)) {
                const ExtrudeDefinition& definition = extrude->definition();
                checkProfile(object.id(), definition.profile);
                if (definition.depthParameter) {
                    checkParameter(object.id(), *definition.depthParameter, dimensions::length,
                                   "the depth is driven by");
                }
            } else if (const auto* revolve = dynamic_cast<const RevolveFeature*>(&object)) {
                const RevolveDefinition& definition = revolve->definition();
                checkProfile(object.id(), definition.profile);
                if (definition.angleParameter) {
                    checkParameter(object.id(), *definition.angleParameter, dimensions::angle,
                                   "the angle is driven by");
                }
                const sketch::Entity* axis = revolveAxisEntity(*revolve);
                if (axis != nullptr && !std::holds_alternative<sketch::LineEntity>(axis->geometry)) {
                    add(ValidationCheck::DocumentConsistency, Severity::Error, object.id(),
                        std::format("{}: the axis is {}, which is {}, not a line", label(document_, object.id()),
                                    definition.axis.line, withArticle(sketch::toString(axis->type()))));
                }
            } else if (const auto* chamfer = dynamic_cast<const ChamferFeature*>(&object)) {
                const ChamferDefinition& definition = chamfer->definition();
                if (definition.distanceParameter) {
                    checkParameter(object.id(), *definition.distanceParameter, dimensions::length,
                                   "the distance is driven by");
                }
            }
            if (const auto* feature = dynamic_cast<const SolidFeature*>(&object)) {
                checkTarget(*feature);
            }
        }
    }

    /// The profile sketch of a revolve about a line, if the sketch exists.
    [[nodiscard]] const sketch::Sketch* revolveAxisSketch(const RevolveFeature& revolve) const {
        if (revolve.definition().axis.kind != RevolveAxisKind::Line) {
            return nullptr;
        }
        return document_.findObjectAs<sketch::Sketch>(ObjectId{revolve.definition().profile});
    }

    /// The axis entity of a revolve about a line, if it exists.
    [[nodiscard]] const sketch::Entity* revolveAxisEntity(const RevolveFeature& revolve) const {
        const sketch::Sketch* sketch = revolveAxisSketch(revolve);
        return sketch == nullptr ? nullptr : sketch->findEntity(revolve.definition().axis.line);
    }

    /// A solid feature's target must be another feature that has a body.
    void checkTarget(const SolidFeature& feature) {
        const auto target = feature.target();
        if (!target) {
            return;
        }
        const ObjectId id{*target};
        if (id != feature.id() && document_.contains(id) && document_.findObjectAs<SolidFeature>(id) == nullptr) {
            wrongKind(feature.id(), "the target is", id, kindOf(document_, id), "a feature with a body");
        }
    }

    void checkMissingReferences(const DocumentGraph& graph) {
        for (const MissingReference& reference : graph.missing) {
            add(ValidationCheck::MissingReferences, Severity::Error, reference.dependent,
                std::format("{} references {}, which does not exist", label(document_, reference.dependent),
                            reference.missing));
        }
        // References into sketches are not graph edges: revolve axis lines.
        for (const DocumentObject& object : document_.objects()) {
            const auto* revolve = dynamic_cast<const RevolveFeature*>(&object);
            if (revolve == nullptr || failedItems_.contains(object.id())) {
                continue;
            }
            const sketch::Sketch* sketch = revolveAxisSketch(*revolve);
            if (sketch != nullptr && sketch->findEntity(revolve->definition().axis.line) == nullptr) {
                add(ValidationCheck::MissingReferences, Severity::Error, object.id(),
                    std::format("{}: the axis line {} does not exist in {}", label(document_, object.id()),
                                revolve->definition().axis.line, label(document_, sketch->id())));
            }
        }
    }

    void checkCycles(const DocumentGraph& graph) {
        for (const auto& cycle : graph.graph.cycles()) {
            std::string members;
            for (const ObjectId id : cycle) {
                members += members.empty() ? "" : ", ";
                members += label(document_, id);
            }
            add(ValidationCheck::DependencyCycles, Severity::Error, cycle.front(),
                std::format("dependency cycle: {}", members));
            failedItems_.insert(cycle.begin(), cycle.end());
        }
    }

    void checkSketches() {
        for (const DocumentObject& object : document_.objects()) {
            const auto* original = dynamic_cast<const sketch::Sketch*>(&object);
            if (original == nullptr || failedItems_.contains(object.id())) {
                continue;
            }
            sketch::Sketch copy = *original;
            if (auto applied = sketch::applyDrivingParameters(copy, document_.parameters()); !applied) {
                add(ValidationCheck::SketchConstraints, Severity::Error, object.id(),
                    std::format("{}: {}", label(document_, object.id()), applied.error().message));
                continue;
            }
            const sketch::SolveResult result = sketch::analyze(copy);
            switch (result.status) {
            case sketch::SolveStatus::FullyConstrained:
                break;
            case sketch::SolveStatus::UnderConstrained:
                add(ValidationCheck::SketchConstraints, Severity::Warning, object.id(),
                    std::format("{} is under-constrained: {} degree(s) of freedom", label(document_, object.id()),
                                result.degreesOfFreedom));
                break;
            case sketch::SolveStatus::OverConstrained:
            case sketch::SolveStatus::Inconsistent:
            case sketch::SolveStatus::SolverFailure:
                add(ValidationCheck::SketchConstraints, Severity::Error, object.id(),
                    std::format("{} is {}: {}", label(document_, object.id()), sketch::toString(result.status),
                                result.message));
                break;
            }
        }
    }

    void checkBody(ObjectId feature, const geometry::Body& body, BodySummary* summary) {
        const std::string name = label(document_, feature);
        if (body.isEmpty()) {
            add(ValidationCheck::Geometry, Severity::Error, feature, std::format("{} produced an empty body", name));
            return;
        }
        const geometry::TopologySummary topology = body.topology();
        const bool valid = body.isValid();
        auto properties = body.massProperties();
        auto box = body.boundingBox();
        if (topology.solids == 0) {
            add(ValidationCheck::Geometry, Severity::Error, feature, std::format("{}: the body has no solid", name));
        } else if (!valid) {
            add(ValidationCheck::Geometry, Severity::Error, feature,
                std::format("{}: the body fails the kernel's validity check", name));
        } else if (!properties) {
            add(ValidationCheck::Geometry, Severity::Error, feature,
                std::format("{}: {}", name, properties.error().message));
        } else if (!(properties->volume > Volume{})) {
            add(ValidationCheck::Geometry, Severity::Error, feature,
                std::format("{}: the body's volume is not positive ({})", name,
                            toString(properties->volume, units::mm3)));
        }
        if (summary != nullptr) {
            summary->valid = valid && topology.solids > 0 && properties && properties->volume > Volume{};
            summary->topology = topology;
            if (properties) {
                summary->properties = *properties;
            }
            if (box) {
                summary->boundingBox = *box;
            }
        }
    }

    void checkRegenerationAndGeometry() {
        Document copy = document_.clone();
        Regenerator regenerator;
        auto regeneration = regenerator.regenerate(copy);
        if (!regeneration) {
            add(ValidationCheck::FeatureRegeneration, Severity::Error, std::nullopt, regeneration.error().message);
            return;
        }
        report_.regenerated = regeneration->regenerated.size();
        // Failures already explained by an earlier check are not repeated.
        for (const auto& [id, error] : regeneration->errors) {
            if (!failedItems_.contains(id)) {
                add(ValidationCheck::FeatureRegeneration, Severity::Error, id,
                    std::format("{} failed to regenerate: {}", label(document_, id), error.message));
            }
        }
        for (const ObjectId id : regeneration->blocked) {
            if (!failedItems_.contains(id)) {
                add(ValidationCheck::FeatureRegeneration, Severity::Error, id,
                    std::format("{} was not regenerated because an item it depends on failed",
                                label(document_, id)));
            }
        }

        // Every body the model produced, including intermediate ones.
        const std::vector<ObjectId> results = resultFeatures(copy);
        for (const DocumentObject& object : copy.objects()) {
            const geometry::Body* body = regenerator.body(object.id());
            if (body == nullptr) {
                continue;
            }
            BodySummary* summary = nullptr;
            if (std::ranges::contains(results, object.id())) {
                report_.bodies.push_back({.feature = object.id(), .name = object.name()});
                summary = &report_.bodies.back();
            }
            checkBody(object.id(), *body, summary);
        }
    }

    const Document& document_;
    ValidationReport report_;
    std::set<ObjectId> failedItems_;
};

} // namespace

std::string_view toString(ValidationCheck check) noexcept {
    switch (check) {
    case ValidationCheck::DocumentConsistency:
        return "document consistency";
    case ValidationCheck::MissingReferences:
        return "missing references";
    case ValidationCheck::DependencyCycles:
        return "dependency cycles";
    case ValidationCheck::SketchConstraints:
        return "sketch constraints";
    case ValidationCheck::FeatureRegeneration:
        return "feature regeneration";
    case ValidationCheck::Geometry:
        return "geometry";
    }
    return "unknown";
}

std::string_view toString(Severity severity) noexcept {
    switch (severity) {
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    return "unknown";
}

std::size_t ValidationReport::count(Severity severity) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(issues, [&](const ValidationIssue& issue) { return issue.severity == severity; }));
}

std::size_t ValidationReport::count(ValidationCheck check, Severity severity) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(issues, [&](const ValidationIssue& issue) {
        return issue.check == check && issue.severity == severity;
    }));
}

ValidationReport validateDocument(const Document& document) {
    return Checker(document).run();
}

} // namespace bettercad::features
