#pragma once

#include "support/TestFiles.hpp"

#include <AssemblyReferenceModels.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>

// Shared machinery for the assembly reference models (P13-REFMOD-001).
//
// Everything here drives the PRODUCTION path: build through the public
// builders, regenerate through features::Regenerator with the assembly
// handlers registered, and read the transforms the final pass published.
// There is no back door that solves an assembly some other way, because a
// reference model that proved a private path worked would prove nothing
// about the product.
namespace bettercad::test::asmref {

/// A reference model built and regenerated, with its solve.
///
/// Holds the Regenerator rather than copying results out of it, because the
/// transforms the pass published belong to that regenerator and reading them
/// from anywhere else would be reading a copy of derived state.
class Assembled {
public:
    explicit Assembled(Document document) : document_(std::move(document)) {
        assembly::registerHandlers(regenerator_, nullptr, &pass_);
        auto report = regenerator_.regenerateAll(document_);
        INFO((report ? std::string{} : report.error().message));
        REQUIRE(report.has_value());
        regenerated_ = report->succeeded();
        if (!report->succeeded()) {
            for (const auto& [id, error] : report->errors) {
                problems_ += (problems_.empty() ? "" : "; ") + error.message;
            }
        }
    }

    [[nodiscard]] Document& document() noexcept { return document_; }
    [[nodiscard]] const Document& document() const noexcept { return document_; }
    [[nodiscard]] bool regenerated() const noexcept { return regenerated_; }
    [[nodiscard]] const std::string& problems() const noexcept { return problems_; }
    [[nodiscard]] const assembly::AssemblyRegeneration& pass() const noexcept { return pass_; }
    [[nodiscard]] const features::Regenerator& regenerator() const noexcept { return regenerator_; }

    /// The solve, run through the same public entry point the pass uses, with
    /// the bodies this regeneration produced so face targets resolve.
    [[nodiscard]] Result<assembly::AssemblySolveResult> solve() const {
        const assembly::BodyLookup bodies = [this](ObjectId object) { return regenerator_.body(object); };
        return assembly::solve(document_, {}, bodies);
    }

    /// The published transform of @p component, or nullptr.
    [[nodiscard]] const RigidTransform3D* transform(ComponentId component) const noexcept {
        return regenerator_.transform(component);
    }

private:
    Document document_;
    features::Regenerator regenerator_;
    assembly::AssemblyRegeneration pass_{};
    bool regenerated_ = false;
    std::string problems_{};
};

/// Builds @p kind through the public catalogue.
[[nodiscard]] inline Document build(reference::AssemblyReferenceModelKind kind) {
    auto document = reference::buildAssemblyReferenceModel(kind);
    INFO((document ? std::string{} : document.error().message));
    REQUIRE(document.has_value());
    return std::move(*document);
}

/// Where a component ended up, in millimetres.
[[nodiscard]] inline std::array<double, 3> positionMm(const RigidTransform3D& motion) {
    const Translation3D& t = motion.translationPart();
    return {t.x.si() * 1000.0, t.y.si() * 1000.0, t.z.si() * 1000.0};
}

/// The component's own Z axis, in world.
[[nodiscard]] inline std::array<double, 3> axisMm(const RigidTransform3D& motion) {
    const std::array<double, 9>& r = motion.matrix();
    return {r[2], r[5], r[8]};
}

/// The component's own X axis, in world: what a turn about Z shows up in.
[[nodiscard]] inline std::array<double, 3> rollMm(const RigidTransform3D& motion) {
    const std::array<double, 9>& r = motion.matrix();
    return {r[0], r[3], r[6]};
}

/// Degrees to the direction cosines of a turn about Z, computed here rather
/// than asked of the thing under test.
[[nodiscard]] inline std::array<double, 3> spin(double degrees) {
    const double radians = degrees * std::numbers::pi / 180.0;
    return {std::cos(radians), std::sin(radians), 0.0};
}

/// Tolerance for a solved position. The solver's own gate is 1e-9 m; these
/// assemblies are tens of millimetres across, so 1e-6 mm is far inside the
/// solution and far outside anything that could hide a misplacement.
inline constexpr double kPlaceMm = 1e-6;
/// Direction cosines are dimensionless and come straight out of the matrix.
inline constexpr double kDirection = 1e-9;

inline void checkPosition(const RigidTransform3D& motion, const std::array<double, 3>& expectedMm) {
    const std::array<double, 3> measured = positionMm(motion);
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("axis " << i << ": measured " << measured[i] << " mm, expected " << expectedMm[i] << " mm");
        CHECK_THAT(measured[i], Catch::Matchers::WithinAbs(expectedMm[i], kPlaceMm));
    }
}

inline void checkDirection(const std::array<double, 3>& measured, const std::array<double, 3>& expected) {
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("component " << i << ": measured " << measured[i] << ", expected " << expected[i]);
        CHECK_THAT(measured[i], Catch::Matchers::WithinAbs(expected[i], kDirection));
    }
}

/// The component of @p document named @p name.
[[nodiscard]] inline ComponentId componentNamed(const Document& document, std::string_view name) {
    for (const ComponentId id : assembly::components(document)) {
        const assembly::Component* component = assembly::findComponent(document, id);
        if (component != nullptr && component->name() == name) {
            return id;
        }
    }
    FAIL("no component named '" << name << "'");
    return {};
}

} // namespace bettercad::test::asmref
