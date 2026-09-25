#pragma once

#include "BuildSupport.hpp"

#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>

#include <string>

// Drawing-side helpers for the reference-model builders (P14-REFMOD-001).
//
// Separate from BuildSupport.hpp on purpose: every part builder includes that
// one, and none of them should be made to pull in the drawing module to get a
// sketch helper.
//
// WHY THIS IS NOT ModelBuilder::feature<F>. A feature is created by
// `F::create(name, definition)` and then added to the document. A sheet, view,
// dimension and annotation are created by FREE functions that take the
// document, because a drawing object is validated against what is already in
// it -- a view against its sheet, a dimension against its view. Same discipline
// as ModelBuilder (record the first error, keep going, report it at the end),
// different call shape.
namespace bettercad::reference::detail {

/// Sheets, views, dimensions and annotations, added through the public API.
class DrawingBuilder {
public:
    explicit DrawingBuilder(ModelBuilder& builder) noexcept : builder_(builder) {}

    SheetId sheet(std::string name, const drawing::SheetDefinition& definition) {
        const std::string step = name;
        return builder_.need(drawing::createSheet(builder_.document(), std::move(name), definition),
                             step);
    }

    ViewId view(std::string name, const drawing::ViewDefinition& definition) {
        const std::string step = name;
        return builder_.need(drawing::createView(builder_.document(), std::move(name), definition),
                             step);
    }

    DimensionId dimension(std::string name, const drawing::DimensionDefinition& definition) {
        const std::string step = name;
        return builder_.need(
            drawing::createDimension(builder_.document(), std::move(name), definition), step);
    }

    AnnotationId annotation(std::string name, const drawing::AnnotationDefinition& definition) {
        const std::string step = name;
        return builder_.need(
            drawing::createAnnotation(builder_.document(), std::move(name), definition), step);
    }

private:
    ModelBuilder& builder_;
};

} // namespace bettercad::reference::detail
