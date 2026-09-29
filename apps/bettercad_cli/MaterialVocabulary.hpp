#pragma once

#include "Arguments.hpp"

#include <bettercad/core/Error.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The words the material CLI uses, and the only place it decides any of them
// (P15-CLI-001).
//
// Every code here is a name for something the CORE already distinguishes. The
// CLI adds no property, no consumer, no issue and no state of its own; if a
// code appears in this file there is an enumerator behind it.
//
// THE CODES ARE THE PERSISTED KEYS. `src/io/json/MaterialJson.cpp` already had
// a stable machine name for each stored property -- "density",
// "youngs_modulus", "yield_strength" -- so adopting a second spelling would
// have meant a document and a command line disagreeing about what to call the
// same number. They are NOT shared by lifting that table into core, and the
// reason is an invariant rather than convenience: kMechanicalKeys has 9 entries
// for 11 kinds, because ADR-027 derives the shear and bulk moduli and never
// stores them, so NO KEY EXISTS and a file cannot claim one. This table needs
// all 11, since the CLI must print the derived pair. A shared table would have
// had to contain "shear_modulus", turning that structural guarantee into a
// convention. Instead the two tables are separate and a test asserts they are
// byte-identical for every kind that has both -- drift fails the suite, and the
// guarantee is untouched. See AUDIT.md finding 2.
namespace bettercad::cli {

/// A property the CLI can name: mechanical or thermal, never both.
using PropertyKind = std::variant<materials::MechanicalPropertyKind, materials::ThermalPropertyKind>;

/// The stable machine code of a property: "density", "youngs_modulus", ...
[[nodiscard]] std::string_view propertyCode(materials::MechanicalPropertyKind kind) noexcept;
[[nodiscard]] std::string_view propertyCode(materials::ThermalPropertyKind kind) noexcept;
[[nodiscard]] std::string_view propertyCode(const PropertyKind& kind) noexcept;

/// Every property, mechanical then thermal, each in enumeration order. The
/// order a report prints, fixed here and not by any container's iteration.
[[nodiscard]] std::vector<PropertyKind> allPropertyKinds();

/// The property @p code names. InvalidArgument, listing what is accepted, for
/// anything else.
[[nodiscard]] Result<PropertyKind> parsePropertyCode(std::string_view code);

/// Whether @p kind is DERIVED (ADR-027: the shear and bulk moduli). A derived
/// property has no stored slot, so it can be printed but never set.
[[nodiscard]] bool isDerivedProperty(const PropertyKind& kind) noexcept;

/// @p kind's value with its unit, or "UNKNOWN".
///
/// UNKNOWN, never 0 and never a blank. A material nobody has weighed has no
/// density; printing 0 would be a claim about a massless solid, and printing
/// nothing would be indistinguishable from a formatting fault.
[[nodiscard]] std::string formatProperty(const materials::MechanicalProperties& properties,
                                         materials::MechanicalPropertyKind kind);
[[nodiscard]] std::string formatProperty(const materials::ThermalProperties& properties,
                                         materials::ThermalPropertyKind kind);

/// The unit a property is PRINTED in, for a report's heading. Empty when the
/// property is dimensionless or carries its own unit in the value.
[[nodiscard]] std::string_view displayUnit(const PropertyKind& kind) noexcept;

/// Parses @p text and stores it in @p properties.
///
/// A BARE NUMBER IS SI. One rule for every property, so `210` can never quietly
/// mean 210 GPa where `235` means 235 MPa; write the unit to use another.
/// Refuses a derived property, naming ADR-027 rather than reporting it as an
/// unknown code.
[[nodiscard]] Result<void> setProperty(materials::MechanicalProperties& properties,
                                       materials::MechanicalPropertyKind kind, std::string_view text);
[[nodiscard]] Result<void> setProperty(materials::ThermalProperties& properties,
                                       materials::ThermalPropertyKind kind, std::string_view text);

/// Consumer codes: "mass_properties", "fea_linear_static", ...
[[nodiscard]] std::string_view consumerCode(materials::ConsumerKind consumer) noexcept;
[[nodiscard]] Result<materials::ConsumerKind> parseConsumer(std::string_view code);
/// Every consumer code, comma separated, for a diagnostic that has to teach it.
[[nodiscard]] std::string consumerCodes();

/// Issue codes: "missing_property", "inconsistent_values", "orphan_provenance",
/// "provenance_incomplete".
[[nodiscard]] std::string_view issueCode(materials::IssueKind kind) noexcept;

/// One issue as a machine code: "missing_property:density" where the issue is
/// about a property, "inconsistent_values" where it is about the material.
///
/// Structured rather than a table of combinations, so a property added to core
/// cannot leave a code unassigned.
[[nodiscard]] std::string issueCode(const materials::MaterialIssue& issue);

/// State codes: "ready", "incomplete", "invalid".
[[nodiscard]] std::string_view stateCode(materials::CompletenessState state) noexcept;

/// The stable code for a material selector that did not resolve:
/// "material_not_found", "material_ambiguous", "not_a_material".
///
/// A mechanical map from the Error resolveMaterial() produced, which is why
/// ambiguity was given its OWN ErrorCode there. Without that, "two materials
/// are designated Steel" and "that object is a sketch" would arrive here
/// indistinguishable, and no script could tell them apart.
///
/// Shared by the reports and the edits so that one situation has one code
/// whichever command met it.
[[nodiscard]] std::string_view materialSelectorCode(const Error& error) noexcept;

} // namespace bettercad::cli
