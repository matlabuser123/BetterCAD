// Build-failure tests for provenance and completeness (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which must
// compile.
//
// Most of these prove an ABSENCE, and the absences are the point of the milestone.
//
//   - There is no ONE completeness flag. "Complete" is meaningless without a
//     consumer, so a report cannot be used as a boolean and there is no
//     isComplete(): a caller has to say complete FOR WHAT.
//   - There is no unvalidated Date. A date is constructed through a factory that
//     returns std::optional, so "2026-02-30" cannot be stored and then formatted as
//     though it were real.
//   - There is no CFD consumer. ADR-028 names P19 as a future consumer and defines
//     no requirements for it, so inventing them would be guessing at physics.
//   - Provenance cannot be mistaken for a value, and a value cannot be mistaken for
//     provenance, even though both are "metadata about a property" in loose speech.
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    Document document{"Part"};

    // The control: the intended way to say all of this.
    const Result<MaterialId> id = features::createMaterial(document, "Steel");
    if (!id) {
        return 1;
    }
    materials::PropertyProvenance provenance;
    provenance.kind = materials::SourceKind::Measured;
    provenance.source = "a test report";
    const std::optional<materials::Date> date = materials::Date::of(2026, 9, 28);
    if (!date) {
        return 1;
    }
    provenance.date = date;
    [[maybe_unused]] const Result<bool> attached = features::setMaterialPropertyProvenance(
        document, *id, materials::MechanicalPropertyKind::Density, provenance);
    [[maybe_unused]] const Result<materials::CompletenessReport> report =
        features::materialCompleteness(document, *id, materials::ConsumerKind::MassProperties);
    [[maybe_unused]] const materials::PropertyRequirement requirement =
        materials::requiredProperties(materials::ConsumerKind::ThermalTransient);
    materials::MaterialProvenance whole;
    whole.material = provenance;
    [[maybe_unused]] const materials::PropertyProvenance effective =
        materials::effectiveProvenance(whole, materials::MechanicalPropertyKind::Density);
    materials::MechanicalProperties mechanical;

#if defined(BETTERCAD_CF_REPORT_IS_NOT_A_BOOLEAN)
    // THE CENTRAL ABSENCE. A report cannot be used as a yes/no, because "is this
    // material complete" has no answer without a consumer. A caller has to read the
    // state, or ask ready() having already chosen a consumer.
    if (report.has_value() && *report) {
        return 1;
    }
#elif defined(BETTERCAD_CF_REPORT_HAS_NO_IS_COMPLETE)
    // And no isComplete(), for the same reason P15-MECH-001 refused one: complete
    // for a mass, for a stress, or for a transient thermal solve are three different
    // questions with three different answers.
    [[maybe_unused]] const bool complete = report->isComplete();
#elif defined(BETTERCAD_CF_MATERIAL_HAS_NO_IS_COMPLETE)
    // Nor is there one on the material, which is where it would be most tempting
    // and most wrong.
    [[maybe_unused]] const bool complete = features::findMaterial(document, *id)->isComplete();
#elif defined(BETTERCAD_CF_COMPLETENESS_NEEDS_A_CONSUMER)
    // The consumer is not optional: there is no overload that answers without one.
    [[maybe_unused]] const auto wrong = features::materialCompleteness(document, *id);
#elif defined(BETTERCAD_CF_CONSUMER_IS_TYPED)
    // And it is a ConsumerKind, not an integer a loop counter could wander into.
    [[maybe_unused]] const auto wrong = features::materialCompleteness(document, *id, 0);
#elif defined(BETTERCAD_CF_NO_CFD_CONSUMER)
    // ADR-028 names P19 CFD as a future consumer and defines no requirements for it.
    // Adding one now would be inventing physics; the mechanism extends to it without
    // a second framework when the requirements are actually decided.
    [[maybe_unused]] const auto wrong =
        materials::requiredProperties(materials::ConsumerKind::Cfd);
#elif defined(BETTERCAD_CF_DATE_HAS_NO_PUBLIC_CONSTRUCTOR)
    // A date is validated on the way in, so there is no way to build one directly
    // from three integers and skip the check.
    [[maybe_unused]] const materials::Date wrong{2026, 2, 30};
#elif defined(BETTERCAD_CF_DATE_FACTORY_RETURNS_OPTIONAL)
    // And the factory's result cannot be used without checking it: an invalid date
    // gives nothing, not a silently wrong day.
    [[maybe_unused]] const materials::Date wrong = materials::Date::of(2026, 2, 30);
#elif defined(BETTERCAD_CF_PROVENANCE_IS_NOT_A_VALUE)
    // Provenance is not a property value. Both are "about a property", and that is
    // exactly why the types must not mix.
    mechanical.density = provenance;
#elif defined(BETTERCAD_CF_VALUE_IS_NOT_PROVENANCE)
    whole.mechanical[materials::MechanicalPropertyKind::Density] = mechanical.density;
#elif defined(BETTERCAD_CF_PROVENANCE_KIND_IS_NOT_A_PROPERTY_STATE)
    // A property's STATE (Known, Unknown, Derived) and its source KIND are different
    // enumerations answering different questions, and neither converts to the other.
    provenance.kind = materials::PropertyState::Known;
#elif defined(BETTERCAD_CF_PROPERTY_STATE_IS_NOT_A_SOURCE_KIND)
    [[maybe_unused]] const materials::PropertyState wrong = materials::SourceKind::Measured;
#elif defined(BETTERCAD_CF_ISSUE_IS_NOT_A_STRING)
    // Diagnostics are structured. A caller reads `kind` and the property, and does
    // not compare the message to a literal -- which is what makes them translatable.
    [[maybe_unused]] const bool matched = report->issues.at(0) == "missing property";
#elif defined(BETTERCAD_CF_NO_DEFAULT_DENSITY)
    // There is no fabricated value anywhere to reach for: no default density, no
    // typical modulus, no nu = 0.3 (ADR-027, ADR-028).
    [[maybe_unused]] const auto wrong = materials::defaultDensity();
#elif defined(BETTERCAD_CF_PROPERTY_HAS_NO_VALUE_OR)
    // MaterialProperty deliberately offers no valueOr(): it would let an Unknown
    // property become a number somewhere far from here, which is the failure the
    // whole type exists to prevent.
    [[maybe_unused]] const auto wrong = mechanical.density.valueOr(Density::fromSi(7850.0));
#endif

    return 0;
}
