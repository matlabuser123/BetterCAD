#include <bettercad/core/units/Format.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <string_view>

namespace bettercad {

namespace {

struct BaseSymbol {
    std::string_view symbol;
    int Dimension::* exponent;
};

// Conventional SI ordering: kg*m/s^2, kg/m^3, rad/s.
constexpr std::array kBaseSymbols{
    BaseSymbol{"kg", &Dimension::mass},        BaseSymbol{"m", &Dimension::length},
    BaseSymbol{"s", &Dimension::time},         BaseSymbol{"K", &Dimension::temperature},
    BaseSymbol{"rad", &Dimension::angle},
};

void appendTerm(std::string& out, std::string_view symbol, int exponent) {
    if (!out.empty()) {
        out += '*';
    }
    out += symbol;
    if (exponent != 1) {
        out += '^';
        out += std::to_string(exponent);
    }
}

} // namespace

std::string siUnitSymbol(const Dimension& dimension) {
    // Named coherent derived units.
    if (dimension == dimensions::force) {
        return "N";
    }
    if (dimension == dimensions::pressure) {
        return "Pa";
    }

    std::string numerator;
    std::string denominator;
    int denominatorTerms = 0;
    for (const BaseSymbol& base : kBaseSymbols) {
        const int exponent = dimension.*base.exponent;
        if (exponent > 0) {
            appendTerm(numerator, base.symbol, exponent);
        } else if (exponent < 0) {
            appendTerm(denominator, base.symbol, -exponent);
            ++denominatorTerms;
        }
    }

    if (denominator.empty()) {
        return numerator;
    }
    if (denominatorTerms > 1) {
        denominator = '(' + denominator + ')';
    }
    return (numerator.empty() ? std::string{"1"} : numerator) + '/' + denominator;
}

std::string describeDimension(const Dimension& dimension) {
    struct NamedDimension {
        Dimension dimension;
        std::string_view name;
    };
    static constexpr std::array kNames{
        NamedDimension{dimensions::dimensionless, "dimensionless"},
        NamedDimension{dimensions::length, "length"},
        NamedDimension{dimensions::area, "area"},
        NamedDimension{dimensions::volume, "volume"},
        NamedDimension{dimensions::angle, "angle"},
        NamedDimension{dimensions::mass, "mass"},
        NamedDimension{dimensions::time, "time"},
        NamedDimension{dimensions::temperature, "temperature"},
        NamedDimension{dimensions::velocity, "velocity"},
        NamedDimension{dimensions::acceleration, "acceleration"},
        NamedDimension{dimensions::force, "force"},
        NamedDimension{dimensions::pressure, "pressure"},
        NamedDimension{dimensions::density, "density"},
    };
    for (const NamedDimension& named : kNames) {
        if (named.dimension == dimension) {
            return std::string{named.name};
        }
    }
    return "quantity [" + siUnitSymbol(dimension) + ']';
}

} // namespace bettercad
