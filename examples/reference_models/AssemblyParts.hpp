#pragma once

#include "BuildSupport.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Placement.hpp>

#include <string>

// The parts the assembly reference models are built from (P13-REFMOD-001).
//
// An assembly reference model builds its own parts, in its own document,
// because P13 assemblies live inside one document and cross-document
// references are not implemented. So these are deliberately plain shapes --
// a block, a disc, a bored block -- made the way every other reference model
// makes geometry: parameters, a sketch, a feature. Nothing here creates a
// body directly.
//
// Every dimension is a parameter, not a literal. That is what lets a model
// change a part from underneath a mate and watch the assembly follow
// (RM-F), and it is how a production part is built in any case.
namespace bettercad::reference::detail {

/// A rectangular block: the XY rectangle (0,0)..(w,d), extruded h in +Z.
/// So its start cap is z = 0 and its end cap is z = h, whatever h becomes.
struct BlockPart {
    ParameterId width{}, depth{}, height{};
    ObjectId sketch{}, solid{};
};

/// Builds a block named @p name, with parameters `<prefix>_w`, `<prefix>_d`
/// and `<prefix>_h`.
[[nodiscard]] BlockPart blockPart(ModelBuilder& b, const std::string& name, const std::string& prefix, double wMm,
                                  double dMm, double hMm);

/// A cylinder about the Z axis: a circle of radius r at the origin of the XY
/// plane, extruded h in +Z.
struct DiscPart {
    ParameterId radius{}, height{};
    ObjectId sketch{}, solid{};
};

[[nodiscard]] DiscPart discPart(ModelBuilder& b, const std::string& name, const std::string& prefix, double rMm,
                                double hMm);

// --- Components and mates ----------------------------------------------------
//
// The same recording discipline the part builders use: a failed call notes
// the first error and returns an invalid ID, so a builder reads as a
// description of the assembly rather than as a chain of early returns.

/// Places @p part as a component named @p name.
[[nodiscard]] ComponentId place(ModelBuilder& b, const std::string& name, ObjectId part,
                                const ComponentPlacement& placement = {});

/// Adds a mate named @p name.
[[nodiscard]] MateId mate(ModelBuilder& b, const std::string& name, const assembly::MateDefinition& definition);

/// Holds @p component where its placement puts it.
[[nodiscard]] MateId ground(ModelBuilder& b, const std::string& name, ComponentId component);

/// A placement: millimetres along X, Y, Z, and an optional spin about Z.
[[nodiscard]] ComponentPlacement at(double xMm, double yMm, double zMm, double spinDeg = 0.0);

/// The five mates that locate @p moving completely against @p held, with no
/// turn left: decks parallel (2 equations), three distances (1 each) and a
/// square (1). Six equations against a free body's six unknowns, so the
/// component has no freedom left and sits exactly at (x, y, z).
///
/// Written once because three models need it and because the count is the
/// thing being relied on: five mates that happened to add up to five
/// equations would leave a freedom nobody asked about.
///
/// Mates are named "<prefix>Flat", "<prefix>Lift", "<prefix>AlongX",
/// "<prefix>AlongY" and "<prefix>Square".
void locate(ModelBuilder& b, const std::string& prefix, ComponentId held, ComponentId moving, double xMm,
            double yMm, double zMm);

} // namespace bettercad::reference::detail
