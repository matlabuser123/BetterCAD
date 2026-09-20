#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>

#include <vector>

// Assembly configurations: which components and mates are in force
// (P13-CONF-001, implementing ADR-007).
//
// There is one configuration system. A configuration overrides parameter
// values (P12-PARAM-002) and, from this milestone, component and mate
// suppression -- so the "Large" build is wider AND has the bracket, said
// once. ADR-007 records why a second AssemblyConfigurationId was rejected.
//
// The semantics are the ones parameters already have, and the reason they
// were reused:
//
//     base state -> the active configuration's override -> what is in force
//
// An object's own `suppressed` flag is its BASE state and no configuration
// edits it. What the solver sees is isComponentSuppressed() /
// isMateSuppressed(), the base with the active configuration's override
// applied. That is why switching A -> B -> A restores A exactly: the base
// never moved, so there is nothing to drift.
//
// Why these live here rather than on Document: reading a base state means
// reading ComponentDefinition and MateDefinition, which are layer 3. `core`
// stores the overrides -- it needs only the ID types, which are its own --
// and never learns what a component is.
namespace bettercad {
class Document;
}

namespace bettercad::assembly {

/// Whether @p id is suppressed under the document's active configuration:
/// its own flag, or the active configuration's override of it.
///
/// False for a component that does not exist, because a component that is
/// not there is not a suppressed one -- callers that care about existence
/// ask findComponent().
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT bool isComponentSuppressed(const Document& document,
                                                                   ComponentId id) noexcept;
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT bool isMateSuppressed(const Document& document, MateId id) noexcept;

/// Whether @p id takes part in the active configuration's solve: it is not
/// suppressed, and neither is any component it names.
///
/// The second half is the rule suppression makes newly possible, and it is
/// not the same as an unresolved reference. A mate on a suppressed component
/// still resolves perfectly well -- the component is in the document with its
/// ID, its geometry and its placement intact -- but constraining a part that
/// is not in this build would be describing a relationship that does not
/// exist in it. So such a mate is INACTIVE, which is a state of this
/// configuration, and not UNRESOLVED, which is a fault in the model.
///
/// Suppressing a component therefore takes its mates out with it, without
/// touching them: their own flags are unchanged and they come back when the
/// component does.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT bool isMateActive(const Document& document, MateId id) noexcept;

/// The components and mates in force under the active configuration, in
/// ascending ID order. What the solver builds its problem from.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<ComponentId> activeComponents(const Document& document);
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<MateId> activeMates(const Document& document);

/// Sets whether @p component is suppressed while @p configuration is active,
/// leaving its base state untouched.
///
/// Fails with NotFound if the configuration or the component does not exist,
/// and InvalidArgument if the ID names an object that is not a component --
/// a suppression override on a sketch would be a statement about nothing.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<bool> suppressComponent(Document& document,
                                                                       ConfigurationId configuration,
                                                                       ComponentId component, bool suppressed);
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<bool> suppressMate(Document& document,
                                                                  ConfigurationId configuration, MateId mate,
                                                                  bool suppressed);
/// Removes the override, so the object takes its base state again in this
/// configuration. Removing one that is not there is not a change.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<bool> clearComponentSuppression(Document& document,
                                                                               ConfigurationId configuration,
                                                                               ComponentId component);
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<bool> clearMateSuppression(Document& document,
                                                                          ConfigurationId configuration,
                                                                          MateId mate);

} // namespace bettercad::assembly
