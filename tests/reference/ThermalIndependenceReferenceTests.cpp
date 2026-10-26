// The independence gate of the thermal reference suite (P18-REFMOD-001).
//
// A reference value that the production code computed is not a reference. The oracle header `ThermalAnalytic.hpp` is the one
// place where expected values come from, so it must not be able to reach the code it checks: it may include the standard
// library and nothing else, and it may not name a BetterCAD namespace. This test reads the file as TEXT and refuses it
// otherwise. The scanner is itself tested on samples, because a gate that cannot fail is not a gate.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <vector>

#ifndef BETTERCAD_THERMAL_ORACLE_PATH
#error "BETTERCAD_THERMAL_ORACLE_PATH must name tests/reference/ThermalAnalytic.hpp"
#endif

namespace {

/// The text of @p source with every comment replaced by a space (newlines kept, so line structure survives). A hand-written scanner,
/// not a regular expression: it understands string and character literals (a `//` inside a string is not a comment) and cannot
/// overflow the stack on a long header.
[[nodiscard]] std::string withoutComments(const std::string& source) {
    std::string out;
    out.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : '\0';
        if (c == '/' && next == '/') {
            while (i < source.size() && source[i] != '\n') {
                ++i;
            }
            out.push_back('\n');
        } else if (c == '/' && next == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
                if (source[i] == '\n') {
                    out.push_back('\n');
                }
                ++i;
            }
            ++i;
            out.push_back(' ');
        } else if (c == '"' || c == '\'') {
            out.push_back(c);
            ++i;
            while (i < source.size() && source[i] != c) {
                if (source[i] == '\\' && i + 1 < source.size()) {
                    out.push_back(source[i++]);
                }
                out.push_back(source[i++]);
            }
            out.push_back(c);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/// The only headers the oracle may include: the standard library's, by an angle-bracket name with no path.
[[nodiscard]] bool isStandardHeader(const std::string& name) {
    static const std::set<std::string> allowed{"algorithm", "array",   "cmath",    "cstddef", "cstdint", "functional", "limits",
                                               "map",       "numbers", "numeric",  "optional", "set",     "span",       "string",
                                               "tuple",     "utility", "vector"};
    return allowed.contains(name);
}

/// The reasons @p text depends on the code under test. A WHITELIST, not a blacklist: after comments are removed, the only
/// preprocessor lines allowed are `#pragma once` and `#include <name>` with `name` a standard header; any other directive (a quoted
/// include, a path, a digraph `%:include`, a macro include, a `#define` that could smuggle one, a line continuation) is a finding,
/// and no production namespace may be named, with or without space before the `::` and in any letter case.
[[nodiscard]] std::vector<std::string> dependenciesOf(const std::string& text) {
    std::vector<std::string> found;
    std::string code = withoutComments(text);
    if (code.find("\\\n") != std::string::npos || code.find("\\\r\n") != std::string::npos) {
        found.emplace_back("uses a line continuation (a directive could be hidden across lines)");
    }
    static const std::regex directive{R"(^[ \t]*(#|%:)[ \t]*([A-Za-z_]*)(.*)$)"};
    static const std::regex standardInclude{R"(^[ \t]*#[ \t]*include[ \t]*<([A-Za-z_]+)>[ \t\r]*$)"};
    static const std::regex pragmaOnce{R"(^[ \t]*#[ \t]*pragma[ \t]+once[ \t\r]*$)"};
    std::size_t begin = 0;
    while (begin <= code.size()) {
        std::size_t end = code.find('\n', begin);
        if (end == std::string::npos) {
            end = code.size();
        }
        std::string line = code.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (std::regex_match(line, directive)) {
            std::smatch m;
            if (std::regex_match(line, m, standardInclude)) {
                if (!isStandardHeader(m[1].str())) {
                    found.push_back("includes '" + m[1].str() + "', which is not on the standard-header list");
                }
            } else if (!std::regex_match(line, pragmaOnce)) {
                found.push_back("has the preprocessor line '" + line + "' (only `#pragma once` and `#include <std-header>` are allowed)");
            }
        }
        begin = end + 1;
    }
    // the oracle's OWN namespace is the one mention of the project name it may make
    code = std::regex_replace(code, std::regex{R"(namespace[ \t]+bettercad::test::analytic::heat)"}, "namespace oracle");
    static const std::regex productionName{
        R"((bettercad|thermal|meshing|features|units|catch)[ \t\n]*::|ThermalConductivity|HeatTransferCoefficient|computeTet4)",
        std::regex::icase};
    std::smatch m;
    if (std::regex_search(code, m, productionName)) {
        found.push_back("names the production symbol '" + m.str() + "'");
    }
    return found;
}
} // namespace

TEST_CASE("ThermalReference_Independence_OracleIncludesNoProductionCode", "[refmod][thermal][independence]") {
    std::ifstream in(BETTERCAD_THERMAL_ORACLE_PATH, std::ios::binary);
    REQUIRE(in.good());
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    REQUIRE(text.size() > 1000); // not an empty or truncated file
    // it really is the oracle: it defines the closed forms the suite uses
    CHECK(text.find("namespace bettercad::test::analytic::heat") != std::string::npos);
    CHECK(text.find("solveConduction") != std::string::npos);
    const std::vector<std::string> reasons = dependenciesOf(text);
    for (const std::string& reason : reasons) {
        INFO("the oracle " << reason);
        CHECK(reason.empty());
    }
    CHECK(reasons.empty());
}

TEST_CASE("ThermalReference_Independence_TheGateCanFail", "[refmod][thermal][independence]") {
    // Each sample is a header that a careless (or evasive) edit could produce; the scanner must refuse every one and pass the clean ones.
    const std::string clean = "#pragma once\n#include <array>\n#include <cmath>\nnamespace bettercad::test::analytic::heat { inline int one() { return 1; } }\n";
    CHECK(dependenciesOf(clean).empty());
    CHECK(dependenciesOf("#include <array>\n// No bettercad:: header, no thermal:: symbol, no #include \"x.hpp\" is used here.\n/* thermal::kelvin */ inline int one() { return 1; }\n").empty());
    CHECK(dependenciesOf("#include <array>\ninline const char* s() { return \"// not a comment\"; }\n").empty());
    const std::vector<std::string> violating{
        "#include <array>\n#include \"reference/ThermalReferenceSupport.hpp\"\n",
        "#include <bettercad/thermal/ThermalPost.hpp>\n",
        "#include <bettercad/core/Units.hpp>\n",
        "#include <BetterCAD/thermal/ThermalPost.hpp>\n",
        "#include <ThermalReferenceModels.hpp>\n",
        "#include <reference/ThermalReferenceSupport.hpp>\n",
        "#include <support/TestFiles.hpp>\n",
        "#include <catch2/catch_test_macros.hpp>\n",
        "%:include \"x.hpp\"\n",
        "#define H <bettercad/thermal/ThermalPost.hpp>\n#include H\n",
        "#include <ar\\\nray>\n",
        "#include <array>\ninline double f() { return thermal ::kelvin(1.0); }\n",
        "#include <array>\ninline double f() { return Thermal::kelvin(1.0); }\n",
        "#include <array>\ninline double f(bettercad::Temperature t) { return t.si(); }\n",
        "#include <array>\nusing thermal::computeTet4Conduction; inline int f() { return computeTet4Conduction(); }\n",
    };
    for (const std::string& sample : violating) {
        INFO(sample);
        CHECK_FALSE(dependenciesOf(sample).empty());
    }
}