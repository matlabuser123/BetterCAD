// P19-TURB-ARCH-001: the turbulence architecture, which adds no types (ADR-079).
//
// WHAT THIS FILE IS FOR. ADR-058 defers turbulence in full from P19 v1 and names
// this milestone as the one that decides the architecture "before any model".
// ADR-079 decides that architecture without types: no mode field, no model
// enumerator, no turbulence variable, no wall-treatment record and no turbulence
// result array. Those are claims about ABSENCE, and a negative claim with no test
// is prose -- the lesson P19-TRANSIENT-001 recorded twice.
//
// WHAT WAS ALREADY ENFORCED, AND IS NOT RESTATED HERE, because a second assertion
// of the same claim is a second authority that can drift from the first:
//
//     the definition holds exactly twelve members    CfdDataTests.cpp's
//                                                    PermittedDefinition mirror
//     the fingerprint covers every TOP-LEVEL field   CfdDataTests.cpp's 40-row
//                                                    truth table and two goldens
//     CfdResultFields is exactly three arrays        CfdDataTests.cpp's
//                                                    PermittedFields mirror
//     CfdResultMetadata is exactly five members      CfdDataTests.cpp's
//                                                    PermittedMetadata mirror
//     a fourth CfdVariable enumerator                -Werror=switch, at
//                                                    src/cfd/CfdFields.cpp:63 and
//                                                    src/cfd/CfdInitialField.cpp:296
//
// WHAT THIS FILE ADDS, and every item was measured before it was claimed. A
// mutation analysis then found two of the first version's claims overstated and
// three of its assertions tautological; both are corrected below rather than
// quietly dropped.
//
//     G1  THE ENUM'S CARDINALITY, not just its table's length. The first version
//         pinned kCfdVariables.size() == 3 and said that made the table "frozen".
//         It does not: the pin measures the ARRAY, and the array is not the type.
//         A fourth enumerator the tables do not know about is invisible to it,
//         and `-Werror=switch` -- which does catch that -- is silenced by one
//         `default:` label. So the table pins are kept for the deliberate
//         widening they really do catch, and the repository's own idiom is added
//         beside them: toString of the value one past the end must still be the
//         unknown fallback, which is how CfdDataTests.cpp:594 pins
//         CfdBoundaryConditionKind. That catches the enumerator a switch handles.
//     G2  CfdBoundaryTreatment had NO SIZE OR MEMBER MIRROR anywhere in tests, so
//         a fourth STORED policy could be added unnoticed. Nothing else in the
//         tree caught that. Note the limit precisely: both instruments count
//         non-static data members, so a member FUNCTION or a static member is
//         invisible to them, and the ownership checker's pattern set is what
//         stands between those and the claim.
//     G3  THE THREE NESTED SETTINGS RECORDS had no mirror and no binding -- the
//         only records the definition reaches that did not. PermittedDefinition
//         is structurally blind to them: it declares the same member TYPES, so
//         both sides of its sizeof grow together, and the 40-row fingerprint
//         table enumerates only fields it already knows. A `bool wallModelled`
//         added to CfdSteadySettings therefore defeated every gate in the tree
//         AND the pre-existing invariant "the fingerprint covers every field".
//
// WHAT THIS FILE DOES NOT DO. It builds no mesh, no topology and no layout, and it
// solves nothing: every claim is about a frozen table or a pure function. That
// turbulence appears nowhere in the tree at all is not expressible in C++ and is
// architecture.turbulence-ownership's.

#include "TestHelpers.hpp"

#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdFields.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

using bettercad::cfd::CfdAnalysisDefinition;
using bettercad::cfd::CfdBoundaryConditionKind;
using bettercad::cfd::CfdBoundaryPolicy;
using bettercad::cfd::CfdBoundaryTreatment;
using bettercad::cfd::CfdLinearSolverSettings;
using bettercad::cfd::CfdOutputSettings;
using bettercad::cfd::CfdResidualHistory;
using bettercad::cfd::CfdSteadySettings;
using bettercad::cfd::CfdVariable;

// ---------------------------------------------------------------------------
// G1 -- the frozen variable table, by table length AND by enum cardinality
// ---------------------------------------------------------------------------

TEST_CASE("CfdTurbulenceArchitecture_TheVariableTableIsCountPinnedAtTheThreeLaminarFields",
          "[cfd][turbarch]") {
    // THE DELIBERATE WIDENING, which is the case these two really own. Measured:
    // a fourth enumerator alone fails at `-Werror=switch`; a fourth enumerator
    // with both switches satisfied and both tables widened passes everything in
    // CfdFields.hpp and fails ONLY here. So the change has to come here and say so.
    static_assert(bettercad::cfd::kCfdVariables.size() == 3,
                  "ADR-058 refuses a fourth CFD variable in v1 and ADR-079 adds none: a turbulence field, a "
                  "species or an energy equation widening this table is a formulation change and its own "
                  "milestone's decision, not an implicit extension");
    static_assert(bettercad::cfd::kCfdFields.size() == 3,
                  "the descriptor table is parallel to kCfdVariables and is pinned with it; CfdFields.hpp's own "
                  "assertions compare the two row by row and would accept four rows in both");

    // The three, by identity and not by count alone: a reordering that kept the
    // size would pass the assertions above.
    CHECK(bettercad::cfd::kCfdVariables[0] == CfdVariable::Pressure);
    CHECK(bettercad::cfd::kCfdVariables[1] == CfdVariable::Velocity);
    CHECK(bettercad::cfd::kCfdVariables[2] == CfdVariable::MassFlux);

    // THE ENUM'S CARDINALITY, by the repository's own idiom (CfdDataTests.cpp:594).
    // A size assertion cannot see an enumerator the table does not list, and
    // `-Werror=switch` stops seeing one the moment a `default:` label is added. But
    // an enumerator somebody bothered to NAME in toString shows up here: the value
    // one past the end must still fall through to the unknown string.
    const auto onePastTheEnd = static_cast<CfdVariable>(bettercad::cfd::kCfdVariables.size());
    CHECK(bettercad::cfd::toString(onePastTheEnd) == "unknown variable");

    // And it is in neither table, so no layout can be computed for it.
    CHECK_FALSE(bettercad::cfd::isRecognised(onePastTheEnd));
    CHECK_FALSE(bettercad::cfd::descriptorOf(onePastTheEnd).has_value());
}

// ---------------------------------------------------------------------------
// G2 -- the boundary treatment and the two frozen enums beside it
// ---------------------------------------------------------------------------

TEST_CASE("CfdTurbulenceArchitecture_ABoundaryTreatmentHoldsOnePolicyPerLaminarVariableAndNoOther",
          "[cfd][turbarch]") {
    // THE INSTRUMENT CfdDataTests USES FOR THE DEFINITION, APPLIED TO ADR-067's
    // TABLE ROW, which had no mirror of any kind. ADR-079 decision 4 keeps the
    // wall-treatment interface out of the tree, and ADR-067 already rejected the
    // generic per-face Robin form "though a later one may want it" because the
    // coefficients ARE a discretization. A turbulence policy added to this record
    // is that rejected design arriving one member at a time.
    struct PermittedTreatment {
        CfdBoundaryPolicy pressure;
        CfdBoundaryPolicy velocity;
        CfdBoundaryPolicy massFlux;
    };
    static_assert(sizeof(CfdBoundaryTreatment) == sizeof(PermittedTreatment),
                  "a boundary treatment is one policy per ADR-066 variable -- pressure, velocity, mass flux -- "
                  "and no fourth STORED policy. A turbulence policy, a wall-function tag, a y+ or a set of Robin "
                  "coefficients added as a data member breaks this");

    // A SIZE CANNOT SEE A MEMBER HIDDEN IN PADDING: three std::uint8_t enums leave
    // room for a fourth inside the same size on most ABIs. So the member COUNT is
    // pinned too, by a structured binding that names exactly three.
    [[maybe_unused]] const auto& [pressure, velocity, massFlux] = CfdBoundaryTreatment{};

    // THE TWO ENUMS THE TABLE IS BUILT FROM, pinned the same two ways. Neither had
    // a count pin; a fifth policy or a seventh condition kind left every gate in
    // this milestone green, because the treatment's size is unchanged by an
    // enumerator and the kinds array is iterated rather than counted.
    static_assert(bettercad::cfd::kCfdBoundaryPolicies.size() == 4,
                  "ADR-067 Decision 2 chose FOUR policies over two, and a fifth -- a wall model, a Robin form, a "
                  "turbulence treatment -- is that decision being reopened");
    CHECK(bettercad::cfd::toString(
              static_cast<CfdBoundaryPolicy>(bettercad::cfd::kCfdBoundaryPolicies.size())) == "unknown");
    CHECK(bettercad::cfd::toString(static_cast<CfdBoundaryConditionKind>(
              bettercad::cfd::kCfdBoundaryConditionKinds.size())) == "unknown");

    // And the table still answers for every condition kind with those three and no
    // fourth question. treatmentOf is the only place ADR-067's table exists, and
    // the kinds come from kCfdBoundaryConditionKinds rather than a hand-written
    // list, so a seventh kind is answered here too instead of silently skipped.
    //
    // THE LOOP ASSERTS THE BINDING AND NOT A sizeof. An adversarial review pointed
    // out that `sizeof(treatment) == sizeof(PermittedTreatment)` inside this loop
    // was a compile-time constant re-checked at runtime once per kind -- six
    // assertions that could not fail, and a second authority for the static_assert
    // above. The binding is what carries information here: it is the only thing
    // that stops compiling if treatmentOf starts returning something wider.
    for (const CfdBoundaryConditionKind kind : bettercad::cfd::kCfdBoundaryConditionKinds) {
        const CfdBoundaryTreatment treatment = bettercad::cfd::treatmentOf(kind);
        const auto& [p, u, phi] = treatment;
        CHECK((p == treatment.pressure && u == treatment.velocity && phi == treatment.massFlux));
        CHECK(bettercad::cfd::toString(kind) != "unknown");
    }
}

// ---------------------------------------------------------------------------
// G3 -- the nested settings records, which were the way into canonical intent
// ---------------------------------------------------------------------------

TEST_CASE("CfdTurbulenceArchitecture_TheNestedSettingsRecordsHoldNoTurbulenceField", "[cfd][turbarch]") {
    // THE HOLE A MUTATION ANALYSIS FOUND, AND IT DEFEATED MORE THAN THIS MILESTONE.
    // CfdDataTests.cpp's PermittedDefinition mirror pins the definition's twelve
    // members, but it declares the same member TYPES -- `std::optional<CfdSteadySettings> steady;`
    // -- so a field added INSIDE one of those records grows both sides of its sizeof
    // equally and its twelve-name binding still sees twelve. The 40-row fingerprint
    // truth table enumerates fields it already knows, and the goldens are unchanged
    // because canonicalText was not touched. So `bool wallModelled = false;` on
    // CfdSteadySettings was a persisted, undoable, operator==-compared field that
    // NO digest and NO mirror in the tree could see.
    //
    // These three records are the only ones the definition reaches that had neither
    // a mirror nor a binding. Pinning them closes ADR-079 decision 1 at the depth
    // the attack used, and restores the pre-existing invariant "the fingerprint
    // covers every field" as a checkable statement rather than an aspiration.
    struct PermittedSteadySettings {
        std::uint32_t maxIterations;
    };
    static_assert(sizeof(CfdSteadySettings) == sizeof(PermittedSteadySettings),
                  "a steady analysis's settings are one iteration budget. A turbulence mode, a model variant, a "
                  "wall-treatment flag or a mixing length added here is canonical intent that the definition's "
                  "own twelve-member mirror cannot see");
    [[maybe_unused]] const auto& [maxIterations] = CfdSteadySettings{};

    struct PermittedLinearSolverSettings {
        double relativeResidualTolerance;
    };
    static_assert(sizeof(CfdLinearSolverSettings) == sizeof(PermittedLinearSolverSettings),
                  "the linear-solve settings are one tolerance; which solver runs is P19-LINSOLVE-001's decision "
                  "and not a field, and a closure's own tolerance would be a second algorithm selector");
    [[maybe_unused]] const auto& [relativeResidualTolerance] = CfdLinearSolverSettings{};

    struct PermittedOutputSettings {
        CfdResidualHistory residualHistory;
    };
    static_assert(sizeof(CfdOutputSettings) == sizeof(PermittedOutputSettings),
                  "the output settings are one residual-history policy. ADR-061 Decision 4: the output settings "
                  "do not select fields, so a turbulence field could not be requested here either");
    [[maybe_unused]] const auto& [residualHistory] = CfdOutputSettings{};
}

// ---------------------------------------------------------------------------
// Decisions 1 and 7 -- nothing turbulent reaches the persisted representation
// ---------------------------------------------------------------------------

TEST_CASE("CfdTurbulenceArchitecture_TheCanonicalTextNamesNoTurbulenceQuantity", "[cfd][turbarch]") {
    // ADR-079 decision 1 refuses a turbulence mode as a field while only one value
    // is qualified (ADR-061 Decision 4: "a selector would be a field with one legal
    // value"), and decision 7 refuses a model field in a persisted representation.
    // The canonical text is what P19-PERSIST-001 will write, so it is where both
    // claims become checkable before that milestone exists.
    //
    // THIS IS NOT THE GOLDEN TEST. CfdDataTests.cpp owns the full expected text and
    // the published digest, and duplicating either here would be a second authority
    // for the same claim. This asserts only the absence, which that test does not.
    //
    // BOTH DIALECTS ARE CHECKED. The first version checked camelCase spellings only,
    // while every persisted key in this repository is snake_case (`time_step`,
    // `reference_pressure`), so a persistence violation would naturally have been
    // written in exactly the dialect this test could not see.
    const std::string text = bettercad::cfd::canonicalText(CfdAnalysisDefinition{});

    using Catch::Matchers::ContainsSubstring;
    for (const std::string_view banned : {"turbulen", "Turbulen", "eddy", "y_plus", "yPlus", "wall_function",
                                          "wallFunction", "k_epsilon", "kEpsilon", "k_omega", "nu_t",
                                          "mixing", "closure"}) {
        CHECK_THAT(text, !ContainsSubstring(std::string{banned}));
    }

    // AND THE SCHEMA LABEL IS UNCHANGED -- WHICH IS A WEAKER CLAIM THAN IT LOOKS,
    // and the first version of this file described it wrongly. `cfd-definition-v1`
    // is a hardcoded literal at src/cfd/CfdAnalysisObject.cpp:201; adding, renaming
    // or removing a field does NOT touch it, so this cannot catch a mode field and
    // the earlier comment saying it could was false. What it does say is that this
    // milestone did not bump the persisted schema, which is a real consequence of
    // deciding to add no field -- and the mirrors above are what see a field.
    CHECK_THAT(text, ContainsSubstring("cfd-definition-v1"));
}
