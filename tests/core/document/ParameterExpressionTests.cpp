#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/core/parameters/Expression.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-PARAM-001: parameter expressions in a document — dependencies, order,
// cycles, failures and edits.

namespace {

/// An object that depends on the given items (a stand-in for a sketch).
class Consumer final : public DocumentObject {
public:
    Consumer(std::string name, std::vector<ObjectId> inputs)
        : DocumentObject(std::move(name)), inputs_(std::move(inputs)) {}
    [[nodiscard]] std::string_view typeName() const noexcept override { return "test_consumer"; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override {
        return std::make_unique<Consumer>(*this);
    }
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override {
        return inputs_ == static_cast<const Consumer&>(other).inputs_;
    }
    [[nodiscard]] std::vector<ObjectId> dependencies() const override { return inputs_; }

private:
    std::vector<ObjectId> inputs_;
};

ParameterId length(Document& doc, const std::string& name, Length value) {
    const auto id = doc.createParameter(name, value, units::mm);
    REQUIRE(id.has_value());
    return *id;
}

void drive(Document& doc, ParameterId id, const std::string& expression) {
    const auto set = doc.setParameterExpression(id, expression);
    INFO(expression);
    if (!set) {
        FAIL(set.error().message);
    }
}

double si(const Document& doc, ParameterId id) {
    return doc.parameters().find(id)->siValue();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

TEST_CASE("DocumentExpression_SetRefusesMalformedTextAndChangesNothing", "[parameters][expressions][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId height = length(doc, "height", 50_mm);
    const std::uint64_t revision = doc.revision();

    for (const std::string text : {"width /", "2 width", "(width", "", "width $ 2"}) {
        CAPTURE(text);
        const auto set = doc.setParameterExpression(height, text);
        REQUIRE_FALSE(set.has_value());
        CHECK(set.error().code == ErrorCode::ParseError);
        CHECK_THAT(set.error().message, ContainsSubstring("parameter 'height': expression '" + text + "': "));
    }
    CHECK(doc.revision() == revision);
    CHECK_FALSE(doc.parameters().find(height)->expression().has_value());

    // A syntactically valid expression is stored even if its names do not
    // exist yet; evaluation reports those.
    CHECK(doc.setParameterExpression(height, "wdth / 2").value());
    CHECK(doc.parameters().find(height)->expression() == "wdth / 2");
    CHECK(doc.revision() == revision + 1);
    CHECK(si(doc, height) == 0.05); // not evaluated yet

    // Restoring a state and inserting a parameter check the text the same way.
    Parameter bad = *doc.parameters().find(width);
    REQUIRE(bad.setExpression("width *").has_value());
    CHECK(errorCode(doc.restoreParameter(bad)) == ErrorCode::ParseError);
    CHECK_FALSE(doc.parameters().find(width)->expression().has_value());
    auto loose = Parameter::create(ParameterId::fromValue(99), "loose", 1_mm, units::mm);
    REQUIRE(loose.has_value());
    REQUIRE(loose->setExpression("1 +").has_value());
    CHECK(errorCode(doc.insertParameter(*loose)) == ErrorCode::ParseError);
    CHECK_FALSE(doc.contains(ObjectId::fromValue(99)));
}

TEST_CASE("DocumentExpression_DrivenParameterRefusesDirectValues", "[parameters][expressions][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId height = length(doc, "height", 50_mm);
    drive(doc, height, "width / 2");
    const Document before = doc.clone();

    const auto direct = doc.setParameterValue(height, 60_mm);
    CHECK(errorCode(direct) == ErrorCode::FailedPrecondition);
    CHECK(direct.error().message ==
          "parameter 'height' is driven by the expression 'width / 2'; clear the expression to set its value");
    CHECK(errorCode(doc.setParameterValue(height, 60.0, describe(units::mm))) == ErrorCode::FailedPrecondition);
    CHECK(errorCode(doc.setParameterSiValue(height, dimensions::length, 0.06)) == ErrorCode::FailedPrecondition);

    CommandHistory history;
    CHECK(errorCode(history.execute(doc, ModifyParameterCommand::setValue(height, 60_mm))) ==
          ErrorCode::FailedPrecondition);
    CHECK_FALSE(history.canUndo());
    CHECK(equivalent(doc, before));

    // Free parameters, and the display unit of a driven one, still change.
    CHECK(doc.setParameterValue(width, 120_mm).value());
    CHECK(doc.setParameterDisplayUnit(height, describe(units::cm)).value());

    // Clearing the expression frees the value, in one undoable command.
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  height, ParameterChanges{.value = DimensionedValue::of(60_mm),
                                                           .expression = std::optional<std::string>{}}))
                .has_value());
    CHECK_FALSE(doc.parameters().find(height)->expression().has_value());
    CHECK(si(doc, height) == 0.06);
    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.parameters().find(height)->expression() == "width / 2");
    CHECK(si(doc, height) == 0.05);
}

TEST_CASE("DocumentExpression_DependenciesAreGraphEdges", "[parameters][expressions][dependencies][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId edge = length(doc, "edge_distance", 15_mm);
    const ParameterId spacing = length(doc, "hole_spacing", 1_mm);
    const ParameterId height = length(doc, "height", 1_mm);
    const ParameterId odd = length(doc, "odd", 1_mm);
    drive(doc, spacing, "width - 2 * edge_distance");
    drive(doc, height, "width / 2");
    const ObjectId sketch = doc.addObject(std::make_unique<Consumer>("Base", std::vector<ObjectId>{height})).value();
    drive(doc, odd, "Base * 2 + nothing + width");

    const DocumentGraph built = buildDependencyGraph(doc);
    CHECK(built.graph.dependenciesOf(spacing) == std::set<ObjectId>{width, edge});
    CHECK(built.graph.dependenciesOf(height) == std::set<ObjectId>{width});
    CHECK(built.graph.dependenciesOf(odd) == std::set<ObjectId>{width});
    CHECK(built.graph.dependenciesOf(width).empty());
    // A change of width reaches the driven parameters and what uses them.
    CHECK(built.graph.downstreamOf({width}) == std::set<ObjectId>{width, spacing, height, odd, sketch});
    CHECK(built.graph.downstreamOf({edge}) == std::set<ObjectId>{edge, spacing});
    CHECK(built.missing.empty());

    // Names that are not parameters are listed, in name order.
    REQUIRE(built.unresolved.size() == 2);
    CHECK(built.unresolved[0].parameter == odd);
    CHECK(built.unresolved[0].error.code == ErrorCode::InvalidArgument);
    CHECK(built.unresolved[0].error.message ==
          "'Base' is not a parameter but a document object of type 'test_consumer' at offset 0");
    CHECK(built.unresolved[1].parameter == odd);
    CHECK(built.unresolved[1].error.code == ErrorCode::NotFound);
    CHECK(built.unresolved[1].error.message == "unknown parameter 'nothing' at offset 11");
}

TEST_CASE("DocumentExpression_EvaluatesInDependencyOrderNotCreationOrder", "[parameters][expressions][p12]") {
    Document doc;
    // Created last-first: c = b * 2, b = a + 1 mm, a = 10 mm.
    const ParameterId c = length(doc, "c", 0_mm);
    const ParameterId b = length(doc, "b", 0_mm);
    const ParameterId a = length(doc, "a", 10_mm);
    const ParameterId d = length(doc, "d", 0_mm);
    drive(doc, c, "b * 2");
    drive(doc, b, "a + 1 mm");
    drive(doc, d, "(c - a) / 4 + b");

    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    CHECK(report.succeeded());
    CHECK(report.evaluated == std::vector<ParameterId>{b, c, d});
    CHECK(report.changed == std::vector<ParameterId>{b, c, d});
    // Hand-computed in the same order: b = 0.010 + 0.001, c = 2b, d = (c - a) / 4 + b.
    const double bValue = 0.010 + 0.001;
    const double cValue = bValue * 2.0;
    const double dValue = (cValue - 0.010) / 4.0 + bValue;
    CHECK(bits(si(doc, b)) == bits(bValue));
    CHECK(bits(si(doc, c)) == bits(cValue));
    CHECK(bits(si(doc, d)) == bits(dValue));
    CHECK_THAT(si(doc, d), WithinRel(0.014, 1e-15));

    // Evaluating again changes nothing, not even the revision.
    const std::uint64_t revision = doc.revision();
    const ParameterEvaluationReport again = evaluateParameterExpressions(doc);
    CHECK(again.evaluated == std::vector<ParameterId>{b, c, d});
    CHECK(again.changed.empty());
    CHECK(doc.revision() == revision);

    // A new input value flows through the chain.
    REQUIRE(doc.setParameterValue(a, 30_mm).value());
    const ParameterEvaluationReport changed = evaluateParameterExpressions(doc);
    CHECK(changed.changed == std::vector<ParameterId>{b, c, d});
    CHECK_THAT(si(doc, c), WithinRel(0.062, 1e-15));
    CHECK_THAT(si(doc, d), WithinRel((0.062 - 0.030) / 4.0 + 0.031, 1e-15));
}

TEST_CASE("DocumentExpression_EvaluatesEveryDimension", "[parameters][expressions][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId height = length(doc, "height", 40_mm);
    const ParameterId area = doc.createParameter("face_area", 1_mm2, units::mm2).value();
    const ParameterId angle = doc.createParameter("half_angle", 1_deg, units::deg).value();
    const ParameterId base = doc.createParameter("draft", 3_deg, units::deg).value();
    const ParameterId count = doc.createParameter("count", 1.0, kUnitless).value();
    const ParameterId pitch = length(doc, "pitch", 20_mm);
    drive(doc, area, "width * height");
    drive(doc, angle, "draft / 2");
    drive(doc, count, "width / pitch + 1");

    const DocumentGraph graph = buildDependencyGraph(doc);
    CHECK(graph.graph.dependenciesOf(area) == std::set<ObjectId>{width, height});
    CHECK(graph.graph.dependenciesOf(count) == std::set<ObjectId>{width, pitch});
    CHECK(graph.graph.dependenciesOf(angle) == std::set<ObjectId>{base});

    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    CHECK(report.succeeded());
    CHECK_THAT(si(doc, area), WithinRel(0.1 * 0.04, 1e-15));
    CHECK_THAT(doc.parameters().find(angle)->as<Angle>()->in(units::deg), WithinRel(1.5, 1e-14));
    CHECK(si(doc, count) == 6.0);
    CHECK(doc.parameters().find(base)->expression() == std::nullopt);
}

TEST_CASE("DocumentExpression_CyclesAreRefusedAndKeepTheirValues", "[parameters][expressions][p12]") {
    Document doc;
    const ParameterId a = length(doc, "a", 1_mm);
    const ParameterId b = length(doc, "b", 2_mm);
    const ParameterId c = length(doc, "c", 3_mm);
    const ParameterId free = length(doc, "free", 4_mm);
    const ParameterId other = length(doc, "other", 5_mm);
    drive(doc, a, "b + 1 mm");
    drive(doc, b, "a * 2");
    drive(doc, c, "a / 2");       // downstream of the cycle
    drive(doc, other, "free * 3"); // unrelated

    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    REQUIRE(report.cycles.size() == 1);
    CHECK(report.cycles.front() == std::vector<ParameterId>{a, b});
    CHECK(report.blocked == std::vector<ParameterId>{c});
    CHECK(report.failed.empty());
    CHECK(report.evaluated == std::vector<ParameterId>{other});
    CHECK_FALSE(report.succeeded());
    CHECK(si(doc, a) == 0.001);
    CHECK(si(doc, b) == 0.002);
    CHECK(si(doc, c) == 0.003);
    CHECK(si(doc, free) == 0.004);
    CHECK_THAT(si(doc, other), WithinRel(0.012, 1e-15));

    // The graph shows the same cycle.
    const auto cycles = buildDependencyGraph(doc).graph.cycles();
    REQUIRE(cycles.size() == 1);
    CHECK(cycles.front() == std::vector<ObjectId>{a, b});

    // A parameter that names itself is a cycle of one.
    drive(doc, b, "b * 2");
    drive(doc, a, "3 mm");
    const ParameterEvaluationReport self = evaluateParameterExpressions(doc);
    REQUIRE(self.cycles.size() == 1);
    CHECK(self.cycles.front() == std::vector<ParameterId>{b});
    CHECK(si(doc, a) == 0.003);
    const auto direct = evaluateParameterExpression(doc, b);
    CHECK(errorCode(direct) == ErrorCode::FailedPrecondition);
    CHECK(direct.error().message == "parameter 'b' = b * 2: the expression uses the parameter 'b' itself at offset 0");

    // Breaking the cycle lets everything evaluate.
    drive(doc, b, "a * 2");
    const ParameterEvaluationReport fixed = evaluateParameterExpressions(doc);
    CHECK(fixed.succeeded());
    CHECK(si(doc, b) == 0.006);
    CHECK(si(doc, c) == 0.0015);
}

TEST_CASE("DocumentExpression_FailedExpressionKeepsLastValueAndBlocksDependents",
          "[parameters][expressions][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId gap = length(doc, "gap", 25_mm);
    const ParameterId ratio = doc.createParameter("ratio", 0.0, kUnitless).value();
    const ParameterId scaled = doc.createParameter("scaled", 0.0, kUnitless).value();
    const ParameterId unrelated = length(doc, "unrelated", 0_mm);
    drive(doc, ratio, "width / gap");
    drive(doc, scaled, "ratio * 2");
    drive(doc, unrelated, "width + 1 mm");
    CHECK(buildDependencyGraph(doc).graph.downstreamOf({width}) ==
          std::set<ObjectId>{width, ratio, scaled, unrelated});
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK(si(doc, ratio) == 4.0);
    CHECK(si(doc, scaled) == 8.0);

    REQUIRE(doc.setParameterValue(gap, 0_mm).value());
    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    REQUIRE(report.failed.size() == 1);
    const Error& error = report.failed.at(ratio);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "parameter 'ratio' = width / gap: division by zero at offset 6: 'gap' is zero");
    CHECK(report.blocked == std::vector<ParameterId>{scaled});
    CHECK(report.evaluated == std::vector<ParameterId>{unrelated});
    // Last good values are kept; nothing is silently replaced.
    CHECK(si(doc, ratio) == 4.0);
    CHECK(si(doc, scaled) == 8.0);

    // The failure is reported again until it is fixed, then everything recovers.
    CHECK(evaluateParameterExpressions(doc).failed.contains(ratio));
    REQUIRE(doc.setParameterValue(gap, 50_mm).value());
    const ParameterEvaluationReport fixed = evaluateParameterExpressions(doc);
    CHECK(fixed.succeeded());
    CHECK(fixed.changed == std::vector<ParameterId>{ratio, scaled});
    CHECK(si(doc, ratio) == 2.0);
    CHECK(si(doc, scaled) == 4.0);
}

TEST_CASE("DocumentExpression_DimensionErrorsAreStructuredFailures", "[parameters][expressions][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId draft = doc.createParameter("draft", 2_deg, units::deg).value();
    const ParameterId height = length(doc, "height", 7_mm);

    drive(doc, height, "width * draft");
    const auto product = evaluateParameterExpression(doc, height);
    CHECK(errorCode(product) == ErrorCode::DimensionMismatch);
    CHECK(product.error().message == "parameter 'height' = width * draft: the result has dimension "
                                     "quantity [m*rad], but the parameter has dimension length");

    drive(doc, height, "width + draft");
    const auto sum = evaluateParameterExpression(doc, height);
    CHECK(errorCode(sum) == ErrorCode::DimensionMismatch);
    CHECK(sum.error().message ==
          "parameter 'height' = width + draft: cannot add 'width' (length) and 'draft' (angle) at offset 6");

    drive(doc, height, "width / width");
    CHECK(errorCode(evaluateParameterExpression(doc, height)) == ErrorCode::DimensionMismatch);

    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    CHECK(report.failed.at(height).code == ErrorCode::DimensionMismatch);
    CHECK(si(doc, height) == 0.007);

    // A dimensionless result for a dimensionless parameter is fine.
    const ParameterId ratio = doc.createParameter("ratio", 0.0, kUnitless).value();
    drive(doc, ratio, "width / width");
    CHECK(evaluateParameterExpression(doc, ratio).value() == DimensionedValue{dimensions::dimensionless, 1.0});
    CHECK(evaluateParameterExpression(doc, width).error().code == ErrorCode::FailedPrecondition);
    CHECK(evaluateParameterExpression(doc, ParameterId::fromValue(404)).error().code == ErrorCode::NotFound);
    CHECK(draft.isValid());
}

TEST_CASE("DocumentExpression_UnknownAndNonParameterNamesAreReported", "[parameters][expressions][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId height = length(doc, "height", 1_mm);
    drive(doc, height, "width / 2");
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK(si(doc, height) == 0.05);

    SECTION("a deleted input") {
        CommandHistory history;
        REQUIRE(history.execute(doc, std::make_unique<DeleteObjectCommand>(width)).has_value());
        const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
        CHECK(report.failed.at(height).code == ErrorCode::NotFound);
        CHECK(report.failed.at(height).message ==
              "parameter 'height' = width / 2: unknown parameter 'width' at offset 0");
        CHECK(si(doc, height) == 0.05);

        // Undo brings the input back, and the expression evaluates again.
        REQUIRE(history.undo(doc).has_value());
        CHECK(evaluateParameterExpressions(doc).succeeded());
    }
    SECTION("a renamed input") {
        REQUIRE(doc.rename(width, "plate_width").value());
        CHECK(evaluateParameterExpressions(doc).failed.at(height).code == ErrorCode::NotFound);
        drive(doc, height, "plate_width / 2");
        CHECK(evaluateParameterExpressions(doc).succeeded());
        CHECK(si(doc, height) == 0.05);
    }
    SECTION("the name of an object") {
        REQUIRE(doc.addObject(std::make_unique<Consumer>("Sketch1", std::vector<ObjectId>{})).has_value());
        drive(doc, height, "Sketch1 / 2");
        const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
        CHECK(report.failed.at(height).code == ErrorCode::InvalidArgument);
        CHECK_THAT(report.failed.at(height).message,
                   ContainsSubstring("'Sketch1' is not a parameter but a document object of type 'test_consumer' "
                                     "at offset 0"));
    }
    SECTION("a name that is later created") {
        drive(doc, height, "depth * 3");
        CHECK(evaluateParameterExpressions(doc).failed.at(height).code == ErrorCode::NotFound);
        length(doc, "depth", 4_mm);
        CHECK(evaluateParameterExpressions(doc).succeeded());
        CHECK_THAT(si(doc, height), WithinRel(0.012, 1e-15));
    }
}

TEST_CASE("DocumentExpression_UndoRedoRestoresExpressionAndValue", "[parameters][expressions][commands][p12]") {
    Document doc;
    const ParameterId width = length(doc, "width", 100_mm);
    const ParameterId height = length(doc, "height", 30_mm);
    CommandHistory history;

    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  height, ParameterChanges{.expression = std::optional<std::string>{"width / 2"}}))
                .has_value());
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    const Parameter driven = *doc.parameters().find(height);
    CHECK(driven.siValue() == 0.05);

    REQUIRE(history.undo(doc).has_value());
    CHECK_FALSE(doc.parameters().find(height)->expression().has_value());
    CHECK(si(doc, height) == 0.03);
    CHECK(evaluateParameterExpressions(doc).evaluated.empty());

    // Redo restores the text; evaluation gives the same value, bit for bit.
    REQUIRE(history.redo(doc).has_value());
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK(equivalent(*doc.parameters().find(height), driven));

    // Changing the input, undoing and re-evaluating restores the value exactly.
    REQUIRE(history.execute(doc, ModifyParameterCommand::setValue(width, 123.4_mm)).has_value());
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK(bits(si(doc, height)) == bits((123.4_mm).si() / 2.0));
    REQUIRE(history.undo(doc).has_value());
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK(bits(si(doc, height)) == bits(driven.siValue()));
}
