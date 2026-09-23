#include <bettercad/drawing/Regeneration.hpp>

#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/drawing/Annotation.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimension.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Sheet.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/View.hpp>
#include <bettercad/drawing/Views.hpp>

#include <format>
#include <optional>
#include <string>

namespace bettercad::drawing {
namespace {

/// "Front (object:7)", matching what the regenerator itself prints.
std::string label(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::format("{} ({})", *name, id) : std::format("{}", id);
}

/// The bodies built so far in this pass.
///
/// Safe to read here, and only here, because a drawing object's dependencies
/// include everything it names (View::dependencies(), Dimension's and
/// Annotation's), so the graph has already built them. The TRANSFORMS are not
/// safe and are never read: the solve is a final pass and has not run yet
/// (ADR-023).
BodyLookup bodiesOf(const features::Regenerator& regenerator) {
    return [&regenerator](ObjectId object) { return regenerator.body(object); };
}

/// Resolves everything a view names, and stops where viewGeometry() stops
/// naming and starts projecting (ADR-023).
Result<void> resolveViewReferences(const Document& document, ViewId id) {
    auto subject = effectiveSubject(document, id);
    if (!subject) {
        return std::unexpected(subject.error());
    }
    if (*subject == ViewSubject::Object) {
        auto source = effectiveSource(document, id);
        if (!source) {
            return std::unexpected(source.error());
        }
        // An external source contributes no dependency edge, because an
        // ObjectId means nothing outside its document (ADR-003), so the graph
        // reports nothing missing for it. View::dependencies() says in as many
        // words that such a view "is failed by its handler instead" -- this is
        // that handler, and nothing else in the codebase resolves a view's
        // source across documents yet.
        const auto local = localTarget(*source);
        if (!local) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} draws {}, which is in another document; a view of an "
                                         "external part cannot be resolved yet",
                                         id, source->object));
        }
        if (!local->isValid()) {
            // createView() refuses this, so it means a file written by
            // something else. Said accurately rather than reported as the
            // external case, which it is not.
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} draws one object but names none", id));
        }
    } else {
        // An assembly view names no source: what it draws is asked of the
        // active configuration at draw time (ADR-021), which is why it has no
        // edge to any component and why the handler has to ask directly.
        // drawnOccurrences() is the same call viewGeometry() makes, and it
        // fails when this configuration leaves the assembly with nothing in
        // it.
        auto drawn = drawnOccurrences(document, id);
        if (!drawn) {
            return std::unexpected(drawn.error());
        }
    }
    // The chain a projected view derives through: each of these walks up to
    // its parent, so a broken or over-long chain is caught here.
    if (auto basis = effectiveBasis(document, id); !basis) {
        return std::unexpected(basis.error());
    }
    if (auto scale = effectiveScale(document, id); !scale) {
        return std::unexpected(scale.error());
    }
    if (auto placement = effectivePlacement(document, id); !placement) {
        return std::unexpected(placement.error());
    }
    return {};
}

/// Wraps a resolver's failure as the object's failure, keeping its code.
///
/// The code is what carries P14-STREF-001's Resolved / Unresolved / Invalid
/// split into Regenerator::error(): NotFound and FailedPrecondition mean the
/// target is not here now and the intent can recover, InvalidArgument means
/// the reference could not be right whatever the model does.
Result<std::optional<geometry::Body>> reported(const Document& document, ObjectId id,
                                               Result<void>&& resolved) {
    if (!resolved) {
        return makeError(resolved.error().code,
                         std::format("{}: {}", label(document, id), resolved.error().message));
    }
    // A drawing object owns no geometry. Nothing is published, so nothing can
    // be stale, and a failure has nothing to roll back (ADR-014).
    return std::optional<geometry::Body>{};
}

} // namespace

void registerHandlers(features::Regenerator& regenerator) {
    // A sheet names nothing outside itself, so this cannot fail for a sheet
    // that was created or loaded through Sheet::create(), which validates.
    // It is registered anyway: the cost is one struct check, and the
    // alternative is one object kind that regeneration is structurally unable
    // to report on.
    regenerator.registerHandler(
        std::string{Sheet::kTypeName},
        [](Document& document, ObjectId object,
           const features::Regenerator&) -> Result<std::optional<geometry::Body>> {
            const auto* sheet = document.findObjectAs<Sheet>(SheetId::fromValue(object.value()));
            if (sheet == nullptr) {
                return makeError(ErrorCode::Internal, std::format("{} is not a sheet", object));
            }
            return reported(document, object, validate(sheet->definition()));
        });

    regenerator.registerHandler(
        std::string{View::kTypeName},
        [](Document& document, ObjectId object,
           const features::Regenerator&) -> Result<std::optional<geometry::Body>> {
            const auto* view = document.findObjectAs<View>(ViewId::fromValue(object.value()));
            if (view == nullptr) {
                return makeError(ErrorCode::Internal, std::format("{} is not a view", object));
            }
            return reported(document, object, resolveViewReferences(document, view->viewId()));
        });

    regenerator.registerHandler(
        std::string{Dimension::kTypeName},
        [](Document& document, ObjectId object,
           const features::Regenerator& current) -> Result<std::optional<geometry::Body>> {
            const auto* dimension =
                document.findObjectAs<Dimension>(DimensionId::fromValue(object.value()));
            if (dimension == nullptr) {
                return makeError(ErrorCode::Internal, std::format("{} is not a dimension", object));
            }
            return reported(document, object,
                            resolveDimensionTargets(document, dimension->dimensionId(),
                                                    bodiesOf(current)));
        });

    regenerator.registerHandler(
        std::string{Annotation::kTypeName},
        [](Document& document, ObjectId object,
           const features::Regenerator& current) -> Result<std::optional<geometry::Body>> {
            const auto* annotation =
                document.findObjectAs<Annotation>(AnnotationId::fromValue(object.value()));
            if (annotation == nullptr) {
                return makeError(ErrorCode::Internal, std::format("{} is not an annotation", object));
            }
            return reported(document, object,
                            resolveAnnotationTarget(document, annotation->annotationId(),
                                                    bodiesOf(current)));
        });
}

} // namespace bettercad::drawing
