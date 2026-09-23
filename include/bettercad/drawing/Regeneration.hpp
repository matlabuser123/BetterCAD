#pragma once

#include <bettercad/drawing/Export.hpp>
#include <bettercad/features/Regenerator.hpp>

// Drawing objects in the dependency graph (ADR-014, ADR-023).
//
// A drawing owns no derived state. Its projection, dimension values, BOM rows
// and annotation items are computed on demand and never stored, so there is
// nothing here to invalidate, publish or keep fresh -- and nothing that can go
// stale. What regeneration is for, in the drawing layer, is the other half of
// the problem: saying out loud when a drawing has stopped naming something
// that exists.
//
// Without a handler an object is silently marked UpToDate and never validated
// (src/features/Regenerator.cpp:344-347), so a dimension whose face has gone
// regenerates as a success. That is the defect P13-REGEN-001 fixed for mates.
//
// A handler resolves what its object NAMES and never computes what its object
// DRAWS (ADR-023). It must: the assembly solve is a final pass and has not run
// when the objects are built, so the full drawing resolvers -- which move a
// balloon's anchor by its occurrence's solved transform -- would read the
// PREVIOUS pass's transforms, or none at all.

namespace bettercad::drawing {

/// Registers the regeneration handlers for sheets, views, dimensions and
/// annotations.
///
/// Call it on any regenerator that will be used with a document carrying a
/// drawing, exactly as assembly::registerHandlers() is called for a document
/// carrying an assembly. Both are needed for a document carrying both: an
/// assembly drawing's view resolves through the components.
///
/// The handlers own no geometry and return std::nullopt, so registering them
/// adds no body to the regenerator and changes no geometry. What it changes is
/// which failures are reported: a drawing object whose reference does not
/// resolve becomes Failed, with the resolver's own error code and message,
/// and anything downstream of it is blocked.
///
/// It is not registered by features::validateDocument(), which builds a bare
/// Regenerator and so validates neither drawings nor assemblies -- layer 2
/// cannot call layer 3 or 4 (ARCHITECTURE.md).
BETTERCAD_DRAWING_EXPORT void registerHandlers(features::Regenerator& regenerator);

} // namespace bettercad::drawing
