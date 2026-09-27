#include <bettercad/core/materials/MechanicalProperties.hpp>

#include <bettercad/core/units/Format.hpp>

#include <cmath>
#include <cstdlib>
#include <utility>
#include <format>
#include <optional>

namespace bettercad::materials {

namespace {

constexpr MechanicalPropertyKind kKinds[] = {
    MechanicalPropertyKind::Density,
    MechanicalPropertyKind::YoungsModulus,
    MechanicalPropertyKind::PoissonRatio,
    MechanicalPropertyKind::ShearModulus,
    MechanicalPropertyKind::BulkModulus,
    MechanicalPropertyKind::YieldStrength,
    MechanicalPropertyKind::UltimateTensileStrength,
    MechanicalPropertyKind::UltimateCompressiveStrength,
    MechanicalPropertyKind::ShearStrength,
    MechanicalPropertyKind::Elongation,
    MechanicalPropertyKind::Hardness,
};

/// A known strength or modulus must be a finite positive stress. Adds a problem
/// to @p problems if it is not; says nothing about an Unknown one.
void checkPositiveStress(const MaterialProperty<Stress>& property, std::string_view name,
                         std::vector<std::string>& problems) {
    const std::optional<Stress> value = property.value();
    if (!value) {
        return;
    }
    if (!isFinite(*value)) {
        problems.emplace_back(std::format("{} is not a finite value", name));
        return;
    }
    if (value->si() <= 0.0) {
        problems.emplace_back(
            std::format("{} must be greater than zero, not {}", name, toString(*value)));
    }
}

/// A property's reference temperature, if it recorded one. The field is on the
/// one property wrapper, so a modulus measured at 20 C is as ordinary as a
/// conductivity measured there and is checked the same way.
template <typename Value>
void checkReferenceTemperature(const MaterialProperty<Value>& property, std::string_view name,
                               std::vector<std::string>& problems) {
    if (std::optional<std::string> problem =
            referenceTemperatureProblem(property.referenceTemperature(), name)) {
        problems.push_back(std::move(*problem));
    }
}

} // namespace

std::span<const MechanicalPropertyKind> mechanicalPropertyKinds() noexcept {
    return std::span<const MechanicalPropertyKind>{kKinds};
}

std::string_view toString(MechanicalPropertyKind kind) noexcept {
    switch (kind) {
    case MechanicalPropertyKind::Density:
        return "density";
    case MechanicalPropertyKind::YoungsModulus:
        return "Young's modulus";
    case MechanicalPropertyKind::PoissonRatio:
        return "Poisson's ratio";
    case MechanicalPropertyKind::ShearModulus:
        return "shear modulus";
    case MechanicalPropertyKind::BulkModulus:
        return "bulk modulus";
    case MechanicalPropertyKind::YieldStrength:
        return "yield strength";
    case MechanicalPropertyKind::UltimateTensileStrength:
        return "ultimate tensile strength";
    case MechanicalPropertyKind::UltimateCompressiveStrength:
        return "ultimate compressive strength";
    case MechanicalPropertyKind::ShearStrength:
        return "shear strength";
    case MechanicalPropertyKind::Elongation:
        return "elongation";
    case MechanicalPropertyKind::Hardness:
        return "hardness";
    }
    return "unknown property";
}

bool isDerivedKind(MechanicalPropertyKind kind) noexcept {
    return kind == MechanicalPropertyKind::ShearModulus
           || kind == MechanicalPropertyKind::BulkModulus;
}

namespace {

/// A STORED property may be Known or Unknown and never Derived.
///
/// `MaterialProperty::derived()` is public because the derivation functions are
/// ordinary code, which means a caller can build a Derived property and put it
/// in a slot. That would be a stored value claiming to have been computed, with
/// nothing to have computed it from -- exactly the "a supplied property and a
/// derived property must not become indistinguishable" failure. So a Derived
/// value in a stored slot is not data, and is refused here.
template <typename Value>
void checkNotDerived(const MaterialProperty<Value>& property, std::string_view name,
                     std::vector<std::string>& problems) {
    if (property.isDerived()) {
        problems.emplace_back(
            std::format("{} is marked derived, but a stored property is supplied or unknown", name));
    }
}

} // namespace

Result<void> validate(const MechanicalProperties& properties) {
    std::vector<std::string> problems;

    checkNotDerived(properties.density, "density", problems);
    checkNotDerived(properties.youngsModulus, "Young's modulus", problems);
    checkNotDerived(properties.poissonRatio, "Poisson's ratio", problems);
    checkNotDerived(properties.yieldStrength, "yield strength", problems);
    checkNotDerived(properties.ultimateTensileStrength, "ultimate tensile strength", problems);
    checkNotDerived(properties.ultimateCompressiveStrength, "ultimate compressive strength",
                    problems);
    checkNotDerived(properties.shearStrength, "shear strength", problems);
    checkNotDerived(properties.elongation, "elongation", problems);
    checkNotDerived(properties.hardness, "hardness", problems);

    if (const std::optional<Density> density = properties.density.value()) {
        if (!isFinite(*density)) {
            problems.emplace_back("density is not a finite value");
        } else if (density->si() <= 0.0) {
            problems.emplace_back(
                std::format("density must be greater than zero, not {}", toString(*density)));
        }
    }

    if (const std::optional<ElasticModulus> modulus = properties.youngsModulus.value()) {
        if (!isFinite(*modulus)) {
            problems.emplace_back("Young's modulus is not a finite value");
        } else if (modulus->si() <= 0.0) {
            problems.emplace_back(std::format("Young's modulus must be greater than zero, not {}",
                                              toString(*modulus)));
        }
    }

    if (const std::optional<bettercad::PoissonRatio> ratio = properties.poissonRatio.value()) {
        if (!isFinite(*ratio)) {
            problems.emplace_back("Poisson's ratio is not a finite value");
        } else if (ratio->value() <= limits::minPoissonRatio
                   || ratio->value() >= limits::maxPoissonRatio) {
            // Both ends excluded: each one makes a derivation divide by zero.
            problems.emplace_back(std::format("Poisson's ratio {} is outside {} < nu < {}",
                                              ratio->value(), limits::minPoissonRatio,
                                              limits::maxPoissonRatio));
        }
    }

    checkPositiveStress(properties.yieldStrength, "yield strength", problems);
    checkPositiveStress(properties.ultimateTensileStrength, "ultimate tensile strength", problems);
    checkPositiveStress(properties.ultimateCompressiveStrength, "ultimate compressive strength",
                        problems);
    checkPositiveStress(properties.shearStrength, "shear strength", problems);

    if (const std::optional<Elongation> elongation = properties.elongation.value()) {
        if (!isFinite(*elongation)) {
            problems.emplace_back("elongation is not a finite value");
        } else if (elongation->value() < 0.0) {
            // No upper limit. Elongations above 100 % are ordinary for
            // elastomers, and an invented ceiling would reject real data.
            problems.emplace_back(std::format("elongation must not be negative, not {} %",
                                              elongation->percent()));
        }
    }

    if (const std::optional<Hardness> hardness = properties.hardness.value()) {
        if (!isFinite(*hardness)) {
            problems.emplace_back("hardness is not a finite value");
        } else if (hardness->value() <= 0.0) {
            problems.emplace_back(std::format("hardness must be greater than zero, not {}",
                                              toString(*hardness)));
        }
        // No per-scale range is imposed. The usable range of each scale comes
        // from its standard, which BetterCAD does not have here, and inventing
        // one would reject real measurements.
    }

    checkReferenceTemperature(properties.density, "density", problems);
    checkReferenceTemperature(properties.youngsModulus, "Young's modulus", problems);
    checkReferenceTemperature(properties.poissonRatio, "Poisson's ratio", problems);
    checkReferenceTemperature(properties.yieldStrength, "yield strength", problems);
    checkReferenceTemperature(properties.ultimateTensileStrength, "ultimate tensile strength",
                              problems);
    checkReferenceTemperature(properties.ultimateCompressiveStrength,
                              "ultimate compressive strength", problems);
    checkReferenceTemperature(properties.shearStrength, "shear strength", problems);
    checkReferenceTemperature(properties.elongation, "elongation", problems);
    checkReferenceTemperature(properties.hardness, "hardness", problems);

    if (problems.empty()) {
        return {};
    }
    std::string joined;
    for (const std::string& problem : problems) {
        if (!joined.empty()) {
            joined += "; ";
        }
        joined += problem;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("the mechanical properties are not valid: {}", joined));
}

std::vector<std::string> mechanicalInconsistencies(const MechanicalProperties& properties) {
    std::vector<std::string> found;

    const std::optional<Stress> yield = properties.yieldStrength.value();
    const std::optional<Stress> ultimate = properties.ultimateTensileStrength.value();
    if (yield && ultimate && isFinite(*yield) && isFinite(*ultimate)) {
        // The ultimate tensile strength is the highest stress a tensile test
        // reaches, so it cannot be below the stress at which yielding began.
        // Equal is allowed: a perfectly brittle material yields and breaks
        // together. The tolerance is relative and tight -- these are two numbers
        // a user typed, not an accumulation -- so it only absorbs the last bit
        // of a decimal conversion.
        const double tolerance = std::abs(yield->si()) * 1e-12;
        if (ultimate->si() < yield->si() - tolerance) {
            found.emplace_back(std::format(
                "the ultimate tensile strength {} is below the yield strength {}",
                toString(*ultimate), toString(*yield)));
        }
    }

    return found;
}

MaterialProperty<ElasticModulus> derivedShearModulus(const MechanicalProperties& properties) {
    if (!hasLinearElasticConstants(properties)) {
        return MaterialProperty<ElasticModulus>::unknown();
    }
    const ElasticModulus modulus = *properties.youngsModulus.value();
    const double ratio = properties.poissonRatio.value()->value();
    // Quantity arithmetic, so the result IS a modulus by construction rather
    // than by a fromSi() a reader has to trust. 1 + nu cannot be zero here:
    // hasLinearElasticConstants() has already refused nu <= -1.
    return MaterialProperty<ElasticModulus>::derived(modulus / (2.0 * (1.0 + ratio)));
}

MaterialProperty<ElasticModulus> derivedBulkModulus(const MechanicalProperties& properties) {
    if (!hasLinearElasticConstants(properties)) {
        return MaterialProperty<ElasticModulus>::unknown();
    }
    const ElasticModulus modulus = *properties.youngsModulus.value();
    const double ratio = properties.poissonRatio.value()->value();
    // 1 - 2nu cannot be zero here: nu >= 0.5 has already been refused.
    return MaterialProperty<ElasticModulus>::derived(modulus / (3.0 * (1.0 - 2.0 * ratio)));
}

bool hasLinearElasticConstants(const MechanicalProperties& properties) {
    const std::optional<ElasticModulus> modulus = properties.youngsModulus.value();
    const std::optional<bettercad::PoissonRatio> ratio = properties.poissonRatio.value();
    if (!modulus || !ratio) {
        return false;
    }
    if (!isFinite(*modulus) || modulus->si() <= 0.0) {
        return false;
    }
    if (!isFinite(*ratio) || ratio->value() <= limits::minPoissonRatio
        || ratio->value() >= limits::maxPoissonRatio) {
        return false;
    }
    return true;
}

bool hasDensity(const MechanicalProperties& properties) {
    const std::optional<Density> density = properties.density.value();
    return density && isFinite(*density) && density->si() > 0.0;
}

} // namespace bettercad::materials
