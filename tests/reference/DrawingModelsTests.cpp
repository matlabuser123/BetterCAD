#include "reference/DrawingTestSupport.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Resolution.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Tolerance.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/DrawingExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

/// A document's canonical intent, as JSON. ADR-011 keeps derived state out of
/// the file, so this text IS the intent and comparing it compares everything
/// that is supposed to persist.
[[nodiscard]] std::string canonicalOf(const Document& document) {
    auto json = io::documentToJson(document);
    INFO(why(json));
    REQUIRE(json.has_value());
    return *json;
}

} // namespace

// The contracts that must hold for EVERY drawing reference model, rather than
// per model. The per-model files hold the independent geometry; this file
// holds what the suite as a whole promises.
//
// Every case here iterates `kDrawingReferenceModels`, so a model added to the
// catalogue is exercised by all of them without anyone remembering to.

TEST_CASE("DrawingReference_EveryModelBuildsRegeneratesAndDrawsOnThePage",
          "[reference][drawing][all]") {
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label << " " << info.name << " -- " << info.purpose);
        Drawn d = drawn(info.kind);

        CHECK(d.document().name() == info.name);
        CHECK(features::validateDocument(d.document()).valid());

        // A drawing model without a drawing is not one.
        const std::vector<SheetId> sheets = drawing::sheets(d.document());
        REQUIRE_FALSE(sheets.empty());
        for (const SheetId sheet : sheets) {
            INFO("sheet " << sheet.value());
            CHECK_FALSE(drawing::viewsOn(d.document(), sheet).empty());
            // sceneOf REQUIREs the scene builds and validates, and validation
            // refuses anything drawn off the page -- so every placement in
            // every model is asserted here.
            const drawing::DrawingScene scene = sceneOf(d, sheet);
            CHECK_FALSE(scene.items.lines.empty());
        }
    }
}

TEST_CASE("DrawingReference_EveryModelRegeneratesDeterministically",
          "[reference][drawing][all]") {
    // Built twice in one process, and the canonical state must be identical.
    // ADR-011 keeps derived state out of the file, so the JSON IS the intent.
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        auto first = reference::buildDrawingReferenceModel(info.kind);
        auto second = reference::buildDrawingReferenceModel(info.kind);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(canonicalOf(*first) == canonicalOf(*second));

        // And the drawn result is identical too: regenerating twice from the
        // same intent draws the same picture.
        Drawn a{std::move(*first)};
        Drawn b{std::move(*second)};
        REQUIRE(a.succeeded());
        REQUIRE(b.succeeded());
        for (const SheetId sheet : drawing::sheets(a.document())) {
            const drawing::DrawingScene sceneA = sceneOf(a, sheet);
            const drawing::DrawingScene sceneB = sceneOf(b, sheet);
            CHECK(sceneA.items.lines == sceneB.items.lines);
            CHECK(sceneA.items.arcs == sceneB.items.arcs);
            CHECK(sceneA.items.texts == sceneB.items.texts);
        }
    }
}

TEST_CASE("DrawingReference_EveryModelSurvivesSaveAndLoad", "[reference][drawing][all][io]") {
    // create -> save -> DESTROY -> load -> regenerate -> compare. Not a file
    // size and not an object count: the canonical intent, the identities, and
    // the picture the drawing actually draws.
    TempDir dir;
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        auto built = reference::buildDrawingReferenceModel(info.kind);
        REQUIRE(built.has_value());
        const std::string canonical = canonicalOf(*built);

        const auto path = dir.path() / (std::string{info.fileStem} + ".bcad");
        REQUIRE(io::saveDocument(*built, path).has_value());

        Drawn before{std::move(*built)};
        REQUIRE(before.succeeded());

        auto loaded = io::loadDocument(path);
        INFO(why(loaded));
        REQUIRE(loaded.has_value());
        // The intent came back exactly -- ids, references, datum order, the
        // lot. Derived geometry is not in the file and is rebuilt below.
        CHECK(canonicalOf(*loaded) == canonical);

        Drawn after{std::move(*loaded)};
        INFO(after.why());
        REQUIRE(after.succeeded());
        for (const SheetId sheet : drawing::sheets(after.document())) {
            const drawing::DrawingScene a = sceneOf(before, sheet);
            const drawing::DrawingScene b = sceneOf(after, sheet);
            CHECK(a.items.lines == b.items.lines);
            CHECK(a.items.arcs == b.items.arcs);
            CHECK(a.items.texts == b.items.texts);
        }
    }
}

TEST_CASE("DrawingReference_EveryModelExportsToAllThreeFormats",
          "[reference][drawing][all][export]") {
    // Written out, and read back with P14-EXPORT-001's own parsers -- the same
    // readers that milestone used, not a second copy that could share a blind
    // spot with them.
    TempDir dir;
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        Drawn d = drawn(info.kind);
        for (const SheetId sheetId : drawing::sheets(d.document())) {
            const drawing::DrawingScene scene = sceneOf(d, sheetId);
            const drawing::Sheet* sheet = drawing::findSheet(d.document(), sheetId);
            REQUIRE(sheet != nullptr);
            const double width = scene.width.in(units::mm);
            const double height = scene.height.in(units::mm);

            const auto svg = io::svgDocument(scene);
            const auto dxf = io::dxfDocument(scene);
            const auto pdf = io::pdfDocument(scene);
            INFO(why(svg) << why(dxf) << why(pdf));
            REQUIRE(svg.has_value());
            REQUIRE(dxf.has_value());
            REQUIRE(pdf.has_value());

            // SVG: the page, structurally.
            const std::vector<drawex::SvgElement> elements = drawex::readSvg(*svg);
            const drawex::SvgElement* root = drawex::findSvg(elements, "svg");
            REQUIRE(root != nullptr);
            CHECK_THAT(root->attributes.at("width"), ContainsSubstring("mm"));
            CHECK_FALSE(drawex::allSvg(elements, "polyline").empty());

            // DXF: millimetres, said rather than guessed, and real entities.
            CHECK(drawex::dxfHeader(*dxf, "$INSUNITS") == std::optional<std::string>{"4"});
            const std::vector<drawex::DxfEntity> entities = drawex::dxfEntities(*dxf);
            CHECK_FALSE(entities.empty());
            std::set<std::string> kinds;
            for (const drawex::DxfEntity& entity : entities) {
                kinds.insert(entity.type);
            }
            INFO("entity kinds present");
            CHECK(kinds.contains("LWPOLYLINE") | kinds.contains("LINE"));

            // NOTHING IS SILENTLY DROPPED, and nothing is invented. Every
            // primitive in the scene becomes exactly one entity in the file:
            // a polyline becomes a LINE when it has two points and an
            // LWPOLYLINE when it has more, an arc becomes an ARC or a CIRCLE,
            // and a text run becomes a TEXT.
            //
            // A writer that quietly skipped a primitive it did not recognise
            // would produce a drawing that looked complete and was missing a
            // hole, a dimension or a note -- and would pass every check that
            // only asked whether the file had the right KINDS of thing in it.
            const std::size_t items = scene.items.lines.size() + scene.items.arcs.size() +
                                      scene.items.texts.size();
            CHECK(entities.size() == items);

            // The same count, through the SVG: one element per primitive.
            const std::size_t svgItems = drawex::allSvg(elements, "polyline").size() +
                                         drawex::allSvg(elements, "circle").size() +
                                         drawex::allSvg(elements, "path").size() +
                                         drawex::allSvg(elements, "text").size();
            CHECK(svgItems == items);

            // PDF: the page box, in points, computed HERE.
            const std::vector<double> box = drawex::pdfMediaBox(*pdf);
            REQUIRE(box.size() == 4);
            CHECK_THAT(box[2], WithinAbs(width * 72.0 / 25.4, 1e-3));
            CHECK_THAT(box[3], WithinAbs(height * 72.0 / 25.4, 1e-3));
            CHECK_THAT(*pdf, ContainsSubstring("%PDF-1.4"));
        }
    }
}

TEST_CASE("DrawingReference_EveryFormatIsByteIdenticalForTheSameDrawing",
          "[reference][drawing][all][export]") {
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        Drawn d = drawn(info.kind);
        for (const SheetId sheet : drawing::sheets(d.document())) {
            const drawing::DrawingScene scene = sceneOf(d, sheet);
            CHECK(*io::svgDocument(scene) == *io::svgDocument(scene));
            CHECK(*io::dxfDocument(scene) == *io::dxfDocument(scene));
            CHECK(*io::pdfDocument(scene) == *io::pdfDocument(scene));
        }
    }
}

TEST_CASE("DrawingReference_TheCatalogueNamesEveryModelExactlyOnce",
          "[reference][drawing][all]") {
    std::set<std::string_view> names;
    std::set<std::string_view> stems;
    std::set<std::string_view> labels;
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        CHECK(names.insert(info.name).second);
        CHECK(stems.insert(info.fileStem).second);
        CHECK(labels.insert(info.label).second);
        CHECK_FALSE(info.purpose.empty());
        CHECK_FALSE(info.mainParameter.empty());
        CHECK(info.mainParameterMm > 0.0);
    }
    CHECK(names.size() == reference::kDrawingReferenceModels.size());
}

TEST_CASE("DrawingReference_EveryModelFollowsAChangeToItsMainDimension",
          "[reference][drawing][all][regen]") {
    // ONE CONTROLLED MUTATION PER MODEL, through the production path: set a
    // parameter, regenerate, and require the drawing to have changed. Nothing
    // tells the regenerator which objects to rebuild.
    //
    // "Changed" is checked on the SHEET rather than on the model, because a
    // model that rebuilt while its drawing kept the old picture is exactly
    // the failure this milestone exists to catch.
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label << " " << info.name << ", " << info.mainParameter << " -> "
                        << info.mainParameterMm << " mm");
        Drawn model = drawn(info.kind);
        const SheetId sheet = drawing::sheets(model.document()).front();
        const drawing::DrawingScene before = sceneOf(model, sheet);

        const ParameterId parameter = parameterNamed(model.document(), info.mainParameter);
        const double original =
            model.document().parameters().find(parameter)->siValue() * 1000.0;
        REQUIRE(std::abs(original - info.mainParameterMm) > 1e-9);

        driveMm(model, info.mainParameter, info.mainParameterMm);
        const drawing::DrawingScene after = sceneOf(model, sheet);
        CHECK(after.items.lines != before.items.lines);

        // And back. The restored drawing must be the original one, item for
        // item: a regeneration that left anything behind would show here.
        // And back. The restored drawing must be the original one: the same
        // lines in the same order with the same styles and widths, the same
        // arcs, and the same words.
        //
        // WHY A TOLERANCE AND NOT EQUALITY. The restored sheet is RECOMPUTED,
        // not remembered -- the kernel runs again from the restored parameter
        // -- so a coordinate of order 100 mm may differ in its last bit or
        // two. Measured across the suite the worst case is 2.8e-14 mm, which
        // is 1 to 2 ulp; the bound below is 1e-12 mm, CLAUDE.md's figure for
        // well-conditioned double-precision work and still some thirty times
        // tighter than the observed spread. Anything that had actually failed
        // to regenerate would be out by micrometres at least, and a structural
        // difference -- a line gained, a style changed -- returns infinity
        // from these and fails whatever the bound.
        driveMm(model, info.mainParameter, original);
        const drawing::DrawingScene restored = sceneOf(model, sheet);
        CHECK(maxLineDeviationMm(restored, before) < 1e-12);
        CHECK(maxArcDeviationMm(restored, before) < 1e-12);
        CHECK(restored.items.texts == before.items.texts);
    }
}

TEST_CASE("DrawingReference_EveryModelDrawsTheSameSheetOnEveryRebuild",
          "[reference][drawing][all]") {
    // The scene is derived, so it is built from scratch every time it is
    // asked for. Asked three times from one regenerated document it must give
    // one answer -- no caching, no accumulation, no dependence on how many
    // times anything has been drawn.
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        Drawn model = drawn(info.kind);
        for (const SheetId sheet : drawing::sheets(model.document())) {
            const drawing::DrawingScene first = sceneOf(model, sheet);
            const drawing::DrawingScene second = sceneOf(model, sheet);
            const drawing::DrawingScene third = sceneOf(model, sheet);
            CHECK(sameScene(first, second));
            CHECK(sameScene(second, third));
            // And so do the files written from it.
            CHECK(*io::svgDocument(first) == *io::svgDocument(third));
            CHECK(*io::dxfDocument(first) == *io::dxfDocument(third));
            CHECK(*io::pdfDocument(first) == *io::pdfDocument(third));
        }
    }
}

TEST_CASE("DrawingReference_EveryModelRegeneratedTwiceOverDrawsOneSheet",
          "[reference][drawing][all][regen]") {
    // Regeneration itself must be idempotent: running it again on an
    // unchanged document changes nothing about what the sheet draws.
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        Drawn model = drawn(info.kind);
        const SheetId sheet = drawing::sheets(model.document()).front();
        const drawing::DrawingScene once = sceneOf(model, sheet);
        model.regenerate();
        INFO(model.why());
        REQUIRE(model.succeeded());
        model.regenerate();
        REQUIRE(model.succeeded());
        CHECK(sameScene(sceneOf(model, sheet), once));
    }
}

TEST_CASE("DrawingReference_EveryDrawingObjectResolvesAgainstItsModel",
          "[reference][drawing][all][stref]") {
    // Every view, every dimension and every annotation in the suite points at
    // something that is actually there. A committed reference model with an
    // unresolved reference in it would be a fixture nobody could trust to
    // fail for the right reason.
    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        Drawn model = drawn(info.kind);
        std::size_t checked = 0;
        for (const ViewId view : drawing::views(model.document())) {
            const drawing::Resolution state = drawing::viewResolution(
                model.document(), view, model.bodies(), model.transforms());
            INFO("view " << view.value() << ": " << state.diagnostic);
            CHECK(state.resolved());
            ++checked;
        }
        for (const DimensionId dimension : drawing::dimensions(model.document())) {
            const drawing::Resolution state = drawing::dimensionResolution(
                model.document(), dimension, model.bodies(), model.transforms());
            INFO("dimension " << dimension.value() << ": " << state.diagnostic);
            CHECK(state.resolved());
            ++checked;
        }
        for (const AnnotationId annotation : drawing::annotations(model.document())) {
            const drawing::Resolution state = drawing::annotationResolution(
                model.document(), annotation, model.bodies(), model.transforms());
            INFO("annotation " << annotation.value() << ": " << state.diagnostic);
            CHECK(state.resolved());
            ++checked;
        }
        CHECK(checked > 0);
    }
}

TEST_CASE("DrawingReference_TheSuiteCoversEveryQualifiedDrawingCapability",
          "[reference][drawing][all]") {
    // THE COVERAGE MATRIX, as an assertion rather than as a table in a
    // document that could go stale. Every capability P14 qualified has to
    // have at least one reference model that exercises it; a capability with
    // no owner is a gap, and a gap behind an aggregate PASS is the thing this
    // case exists to stop.
    std::set<std::string> covered;
    std::size_t assemblyModels = 0;
    std::size_t scaledViews = 0;
    std::size_t multiSheetModels = 0;

    for (const reference::DrawingReferenceModelInfo& info : reference::kDrawingReferenceModels) {
        INFO(info.label);
        Drawn model = drawn(info.kind);
        const Document& document = model.document();

        covered.insert("sheets");
        const std::vector<SheetId> sheetsHere = drawing::sheets(document);
        multiSheetModels += sheetsHere.size() > 1 ? 1U : 0U;
        for (const SheetId sheet : sheetsHere) {
            const drawing::Sheet* found = drawing::findSheet(document, sheet);
            REQUIRE(found != nullptr);
            covered.insert(std::string{drawing::toString(found->definition().format)});
            covered.insert(std::string{drawing::toString(found->definition().orientation)});
        }
        for (const ViewId id : drawing::views(document)) {
            const drawing::View* view = drawing::findView(document, id);
            REQUIRE(view != nullptr);
            const drawing::ViewDefinition& d = view->definition();
            covered.insert("view:" + std::string{drawing::toString(d.kind)});
            if (d.orientation) {
                covered.insert("orientation:" + std::string{drawing::toString(*d.orientation)});
            }
            if (d.subject == drawing::ViewSubject::Assembly) {
                covered.insert("view:assembly");
            }
            scaledViews += d.scale.has_value() ? 1U : 0U;
        }
        for (const DimensionId id : drawing::dimensions(document)) {
            const drawing::Dimension* dimension = drawing::findDimension(document, id);
            REQUIRE(dimension != nullptr);
            covered.insert("dimension:" +
                           std::string{drawing::toString(dimension->definition().type)});
            const auto& tolerance = dimension->definition().tolerance;
            if (tolerance) {
                covered.insert(tolerance->fit ? "tolerance:fit" : "tolerance:deviations");
                covered.insert("tolerance:" + std::string{drawing::toString(tolerance->display)});
            }
        }
        for (const AnnotationId id : drawing::annotations(document)) {
            const drawing::Annotation* annotation = drawing::findAnnotation(document, id);
            REQUIRE(annotation != nullptr);
            covered.insert("annotation:" +
                           std::string{drawing::toString(annotation->definition().type)});
        }
        if (info.assembly) {
            ++assemblyModels;
            CHECK_FALSE(assembly::components(document).empty());
        }
        if (!document.configurations().empty()) {
            covered.insert("configurations");
        }
        // Hidden detail is a property of what gets drawn, not of the intent.
        for (const SheetId sheet : drawing::sheets(document)) {
            if (hiddenLineCount(sceneOf(model, sheet)) > 0) {
                covered.insert("hidden-lines");
            }
        }
    }

    const auto owns = [&](std::string_view capability) {
        INFO("no reference model covers " << capability);
        CHECK(covered.contains(std::string{capability}));
    };
    // Views: every kind P14-VIEW-001 and P14-VIEW-002 qualified, and hidden
    // detail actually drawn by one of them.
    //
    // `view:auxiliary` is here because the first draft of this suite did not
    // have one -- eight models, five view kinds qualified, and the one that
    // needs a direction of its own owned by nobody. That is the gap this case
    // exists to make visible.
    for (const std::string_view capability :
         {"view:base", "view:projected", "view:section", "view:detail", "view:auxiliary",
          "view:assembly", "orientation:front", "orientation:top", "hidden-lines"}) {
        owns(capability);
    }
    // Dimensions: EVERY type P14-DIM-001 qualified. The linear family is four
    // different measurements of the same pair of targets -- linear takes the
    // true distance, horizontal and vertical take the parts along the view's
    // axes, aligned takes what is drawn -- and a suite that owned only
    // `linear` would not have exercised the view's axes at all.
    for (const std::string_view capability :
         {"dimension:linear", "dimension:horizontal", "dimension:vertical",
          "dimension:aligned", "dimension:angular", "dimension:radius", "dimension:diameter",
          "dimension:ordinate"}) {
        owns(capability);
    }
    // Tolerances and GD&T.
    for (const std::string_view capability :
         {"tolerance:deviations", "tolerance:fit", "tolerance:limits", "tolerance:plus_minus"}) {
        owns(capability);
    }
    // Annotations, including the ones P14-BOM-001 added.
    for (const std::string_view capability :
         {"annotation:note", "annotation:leader", "annotation:hole_callout",
          "annotation:centremark", "annotation:centreline", "annotation:datum",
          "annotation:feature_control_frame", "annotation:surface_finish",
          "annotation:balloon", "annotation:bom_table"}) {
        owns(capability);
    }
    owns("configurations");
    // Sheets: more than one format, and both orientations. A suite drawn
    // entirely on A3 landscape would never have exercised a page of another
    // shape, and the placement checks would all have been against one size.
    owns("A3");
    owns("A4");
    owns("landscape");
    owns("portrait");
    // And a drawing of more than one sheet.
    CHECK(multiSheetModels > 0);
    // At least one view drawn at a scale of its own, so export scale is
    // exercised rather than assumed to be 1:1 everywhere.
    CHECK(scaledViews > 0);
    // Three assembly models: one for occlusion, one for the BOM, one for
    // configurations.
    CHECK(assemblyModels == 3);
}
