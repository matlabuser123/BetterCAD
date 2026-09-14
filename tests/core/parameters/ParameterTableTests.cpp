#include <bettercad/core/Units.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;

namespace {

struct Fixture {
    IdAllocator ids;
    ParameterTable table;

    template <Dimension D>
    ParameterId add(std::string name, const Quantity<D>& value, const Unit<D>& unit) {
        const auto id = ids.allocate<ParameterId>();
        auto parameter = Parameter::create(id, std::move(name), value, unit);
        REQUIRE(parameter.has_value());
        REQUIRE(table.add(std::move(*parameter)).has_value());
        return id;
    }
};

} // namespace

TEST_CASE("Parameters are found by ID and by name", "[parameters][table]") {
    Fixture f;
    const auto width = f.add("width", 100_mm, units::mm);
    const auto height = f.add("height", 50_mm, units::mm);

    CHECK(f.table.size() == 2);
    REQUIRE(f.table.find(width) != nullptr);
    CHECK(f.table.find(width)->name() == "width");
    REQUIRE(f.table.findByName("height") != nullptr);
    CHECK(f.table.findByName("height")->id() == height);
    CHECK(f.table.find(ParameterId::fromValue(99)) == nullptr);
    CHECK(f.table.findByName("depth") == nullptr);
    CHECK(f.table.contains(width));
    CHECK(f.table.highestIdValue() == 2);
}

TEST_CASE("Parameters are listed in ID order", "[parameters][table]") {
    Fixture f;
    f.add("c", 3_mm, units::mm);
    f.add("a", 1_mm, units::mm);
    f.add("b", 2_mm, units::mm);

    std::vector<std::string> names;
    for (const Parameter& p : f.table.all()) {
        names.push_back(p.name());
    }
    CHECK(names == std::vector<std::string>{"c", "a", "b"});
}

TEST_CASE("Duplicate IDs and names are rejected", "[parameters][table]") {
    Fixture f;
    const auto width = f.add("width", 100_mm, units::mm);
    const auto revision = f.table.revision();

    const auto sameId = f.table.add(*Parameter::create(width, "other", 1_mm, units::mm));
    REQUIRE_FALSE(sameId.has_value());
    CHECK(sameId.error().code == ErrorCode::AlreadyExists);

    const auto sameName =
        f.table.add(*Parameter::create(ParameterId::fromValue(50), "width", 1_mm, units::mm));
    REQUIRE_FALSE(sameName.has_value());
    CHECK(sameName.error().code == ErrorCode::AlreadyExists);

    CHECK(f.table.size() == 1);
    CHECK(f.table.revision() == revision);
}

TEST_CASE("Table modifications are dimension-checked", "[parameters][table]") {
    Fixture f;
    const auto width = f.add("width", 100_mm, units::mm);

    REQUIRE(f.table.setValue(width, 120_mm).value());
    CHECK(f.table.find(width)->displayValue() == 120.0);

    const auto wrong = f.table.setValue(width, 2_kg);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().code == ErrorCode::DimensionMismatch);
    CHECK(f.table.find(width)->displayValue() == 120.0);

    const auto missing = f.table.setValue(ParameterId::fromValue(77), 1_mm);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

TEST_CASE("The table revision tracks effective changes only", "[parameters][table]") {
    Fixture f;
    CHECK(f.table.revision() == 0);
    const auto width = f.add("width", 100_mm, units::mm);
    CHECK(f.table.revision() == 1);

    REQUIRE(f.table.setValue(width, 110_mm).value());
    CHECK(f.table.revision() == 2);
    CHECK_FALSE(f.table.setValue(width, 110_mm).value()); // no-op
    CHECK(f.table.revision() == 2);
    REQUIRE_FALSE(f.table.setValue(width, 1_s).has_value()); // failure
    CHECK(f.table.revision() == 2);

    REQUIRE(f.table.setDisplayUnit(width, describe(units::cm)).value());
    REQUIRE(f.table.setExpression(width, "height * 2").value());
    REQUIRE(f.table.rename(width, "plate_width").value());
    CHECK(f.table.revision() == 5);

    REQUIRE(f.table.remove(width).has_value());
    CHECK(f.table.revision() == 6);
}

TEST_CASE("Renaming keeps names unique and the name index current", "[parameters][table]") {
    Fixture f;
    const auto width = f.add("width", 100_mm, units::mm);
    f.add("height", 50_mm, units::mm);

    const auto taken = f.table.rename(width, "height");
    REQUIRE_FALSE(taken.has_value());
    CHECK(taken.error().code == ErrorCode::AlreadyExists);
    CHECK(f.table.find(width)->name() == "width");

    REQUIRE(f.table.rename(width, "plate_width").value());
    CHECK(f.table.findByName("width") == nullptr);
    REQUIRE(f.table.findByName("plate_width") != nullptr);
    CHECK(f.table.findByName("plate_width")->id() == width);
    CHECK_FALSE(f.table.rename(width, "plate_width").value());

    const auto invalid = f.table.rename(width, "plate width");
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Removing returns the parameter and frees its name", "[parameters][table]") {
    Fixture f;
    const auto width = f.add("width", 100_mm, units::mm);

    const auto removed = f.table.remove(width);
    REQUIRE(removed.has_value());
    CHECK(removed->name() == "width");
    CHECK(f.table.empty());
    CHECK(f.table.findByName("width") == nullptr);

    const auto again = f.table.remove(width);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().code == ErrorCode::NotFound);

    // The name is free again, but IDs are never reused.
    const auto replacement = f.add("width", 100_mm, units::mm);
    CHECK(replacement != width);
}
