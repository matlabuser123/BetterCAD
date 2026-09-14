# BetterCAD — Roadmap

> A modern, programmable, simulation-native, AI-ready mechanical CAD/CAE platform.

BetterCAD aims to combine:

```text
Parametric CAD
+ Assemblies
+ Drawings
+ Simulation
+ Optimization
+ Automation
+ Version control
+ AI-assisted engineering
```

The project must remain **verification-driven**.

A phase is complete only when its implementation, tests, validation cases, and acceptance gates pass.

---

# Vision

BetterCAD should eventually support the complete engineering workflow:

```text
Requirements
    ↓
Parametric CAD
    ↓
Assemblies
    ↓
Simulation
    ↓
Optimization
    ↓
Verification
    ↓
Drawings / Manufacturing
    ↓
Revision / Collaboration
```

Long term:

```text
Engineer
   ↓
Natural-language requirement
   ↓
Engineering agent
   ↓
Parametric model
   ↓
FEA / CFD / Thermal
   ↓
Optimization
   ↓
Verified design
   ↓
Manufacturing output
```

---

# Development Principles

## 1. Correctness before features

Never add large amounts of functionality on top of an unstable foundation.

Every major subsystem requires:

* deterministic tests;
* regression tests;
* analytical or independent validation where possible;
* failure diagnostics;
* reproducible evidence.

---

## 2. Stable engineering data model

Engineering intent must be stored explicitly.

Do not reduce the model to rendered meshes.

Preserve:

```text
parameters
constraints
sketches
features
bodies
assemblies
materials
loads
boundary conditions
simulation settings
requirements
results
revision history
```

---

## 3. Native units

Engineering values must retain their physical dimensions.

Prefer:

```cpp
Length width = 100_mm;
Force load = 5_kN;
Pressure pressure = 2.5_MPa;
```

over:

```cpp
double width = 100.0;
```

---

## 4. Stable identity

Persistent model objects require stable IDs.

Do not rely on transient:

```text
vector index
Face23
Edge11
object memory address
```

for long-term identity.

---

## 5. Dependency-driven regeneration

A parameter change should rebuild only affected objects.

Example:

```text
width
  ↓
Sketch001
  ↓
Extrude001
  ↓
Fillet001
  ↓
Body001
```

---

## 6. Headless core

The CAD engine must work without the GUI.

Target architecture:

```text
                ┌───────────────┐
                │ Desktop GUI   │
                └───────┬───────┘
                        │
                ┌───────▼───────┐
                │ BetterCAD API │
                └───────┬───────┘
                        │
     ┌──────────────────┼──────────────────┐
     ↓                  ↓                  ↓
 Parametric CAD      Simulation        Automation
     │                  │                  │
     └──────────────────┼──────────────────┘
                        ↓
                    Core Model
```

CLI, GUI, Python, tests, and eventually AI should use the same core engine.

---

# Phase Overview

```text
P0   Repository / Build Foundation
P1   Engineering Core
P2   Document Architecture
P3   Geometry Kernel
P4   Sketch System
P5   Constraint Solver
P6   Parametric Features
P7   Regeneration Engine
P8   Persistence / File Formats
P9   Desktop CAD Application
P10  Production Part Modeling
P11  Assemblies
P12  Technical Drawings
P13  Materials / Engineering Data
P14  Meshing
P15  Structural FEA
P16  Thermal Analysis
P17  CFD Integration
P18  Design Optimization
P19  Semantic Topology
P20  Versioning / Collaboration
P21  Automation / Python
P22  AI Engineering Agent
P23  Manufacturing / CAM Foundation
P24  Performance / GPU / Scale
P25  Production Hardening
P26  BetterCAD 1.0
```

---

# P0 — Repository Foundation

## Goal

Create a clean, reproducible C++ engineering project.

## Deliverables

* C++20/23 project;
* CMake build system;
* Debug and Release configurations;
* core library;
* CLI executable;
* desktop executable;
* test infrastructure;
* CI;
* formatting;
* static analysis;
* documentation skeleton.

Suggested structure:

```text
BetterCAD/
├── apps/
├── cmake/
├── data/
├── docs/
├── examples/
├── include/
├── src/
├── tests/
├── tools/
├── CMakeLists.txt
├── README.md
├── ROADMAP.md
└── TODO.md
```

## Gate

```text
configure PASS
build PASS
tests PASS
CLI smoke test PASS
desktop smoke test PASS
CI PASS
```

---

# P1 — Engineering Core

## Goal

Create the fundamental types used everywhere else.

## Scope

### Units

Implement strongly typed:

```text
Length
Area
Volume
Angle
Mass
Time
Velocity
Acceleration
Force
Torque
Pressure
Stress
Density
Temperature
Power
Energy
```

### Stable IDs

Implement:

```text
DocumentId
ObjectId
ParameterId
SketchId
EntityId
ConstraintId
FeatureId
BodyId
FaceId
EdgeId
VertexId
ComponentId
MaterialId
SimulationId
```

### Parameters

Support:

```text
name
value
unit
stable ID
optional expression
revision
metadata
```

## Gate

* conversion tests pass;
* invalid dimensional operations are prevented;
* parameter serialization round-trip passes;
* stable identity survives load/save.

---

# P2 — Document Architecture

## Goal

Create the persistent engineering document model.

## Model

```text
Document
├── Parameters
├── Sketches
├── Features
├── Bodies
├── Materials
├── Assemblies
├── Simulations
└── Metadata
```

## Implement

* document creation;
* object registry;
* name lookup;
* ID lookup;
* dirty state;
* revisions;
* transactions;
* command architecture;
* undo;
* redo.

## Gate

A sequence such as:

```text
create
modify
delete
undo
undo
redo
redo
```

must return deterministic equivalent document states.

---

# P3 — Geometry Kernel Foundation

## Goal

Introduce robust solid geometry.

Initial kernel:

```text
Open CASCADE Technology
```

BetterCAD must wrap it behind its own geometry interfaces.

Do not expose OCCT throughout the application.

## Implement

### Primitive geometry

```text
Box
Cylinder
Sphere
Cone
Torus
```

### Properties

```text
volume
surface area
centroid
bounding box
mass properties
```

### Boolean operations

```text
union
difference
intersection
```

## Validation

Compare numerical geometry properties to analytical solutions.

Example:

$$
V_\text{cylinder}=\pi r^2h
$$

## Gate

All primitive and Boolean regression tests pass with valid B-Rep topology.

---

# P4 — Sketch System

## Goal

Create robust 2D parametric sketches.

## Geometry

Implement:

```text
Point
Line
Circle
Arc
Ellipse
Spline
```

## Sketch coordinate system

Support:

```text
origin
X axis
Y axis
normal
plane transformation
```

Start with global planes:

```text
XY
XZ
YZ
```

Later support arbitrary planar faces.

## Queries

Provide:

```text
length
radius
center
endpoints
bounding box
intersection
distance
```

## Gate

Sketch geometry must remain deterministic under save/load and transformations.

---

# P5 — Constraint Solver

## Goal

Turn sketches into fully parametric geometric systems.

## Constraints

Implement:

```text
Coincident
Horizontal
Vertical
Parallel
Perpendicular
Tangent
Equal
Concentric
Distance
HorizontalDistance
VerticalDistance
Radius
Diameter
Angle
Fixed
Symmetric
Midpoint
```

## Solver states

The solver must distinguish:

```text
UNDER_CONSTRAINED
FULLY_CONSTRAINED
OVER_CONSTRAINED
INCONSISTENT
SOLVER_FAILURE
```

## Diagnostics

Do not simply report:

```text
"Sketch failed"
```

Report conflicting or redundant constraints where possible.

## Validation cases

At minimum:

```text
rectangle
triangle
bolt-circle layout
tangent arc
concentric circles
fully constrained profile
under-constrained profile
conflicting dimensions
redundant constraints
```

## Gate

Deterministic solver regression suite passes.

---

# P6 — Parametric Feature Engine

## Goal

Create feature-based solid modeling.

Implement approximately in this order:

```text
Extrude
Revolve
Hole
Chamfer
Fillet
Linear Pattern
Circular Pattern
Mirror
Sweep
Loft
Shell
Draft
Rib
```

Feature structure:

```text
Feature
├── stable ID
├── type
├── inputs
├── parameters
├── dependencies
├── status
└── output
```

## Gate

Every feature gets:

* nominal case;
* parameter-change case;
* failure case;
* serialization case;
* regeneration regression.

---

# P7 — Regeneration Engine

## Goal

Build the engine that makes BetterCAD truly parametric.

## Dependency graph

Example:

```text
width
   ↓
Sketch001
   ↓
Extrude001
   ↓
Fillet001
   ↓
Pattern001
```

Implement:

```text
dependency graph
dirty propagation
topological ordering
partial regeneration
cycle detection
failure propagation
transactional rebuild
```

## Requirements

A failed downstream feature must not silently corrupt the document.

## Gate

Changing one parameter regenerates only affected nodes and produces deterministic geometry.

---

# P8 — Persistence and Interchange

## Goal

Create reliable native documents and industry-compatible exchange.

## Native format

Initial format:

```text
.bcad
```

Store:

```text
document metadata
stable IDs
parameters
sketches
constraints
features
dependencies
materials
configuration
optional geometry cache
```

## Import/export

Prioritize:

```text
STEP
STL
DXF
IGES
OBJ
```

Later:

```text
3MF
glTF
Parasolid where licensing permits
JT
```

## Gate

Round trip:

```text
create
save
close
load
regenerate
export
```

must preserve engineering intent and geometry.

---

# P9 — Desktop CAD Application

## Goal

Provide the first practical interactive CAD environment.

Initial GUI technology:

```text
Qt 6
```

## Major UI areas

```text
Menu / command system
Feature toolbar
Model tree
Property editor
3D viewport
Sketch environment
Status bar
Command search
Diagnostics panel
```

## Navigation

Implement:

```text
orbit
pan
zoom
fit
standard views
perspective
orthographic
```

## Selection

Support:

```text
body
face
edge
vertex
sketch
feature
```

## Gate

A user must be able to create a parametric mechanical part entirely from the GUI.

---

# P10 — Production Part Modeling

## Goal

Move from a demonstration CAD tool to practical mechanical part design.

Add:

```text
datum planes
datum axes
coordinate systems
reference geometry
multi-body parts
construction geometry
advanced patterns
variable fillets
draft
shell
split body
combine
direct measurement
mass properties
materials
appearance
design equations
configurations
```

## Example acceptance parts

Build real reference parts:

```text
shaft
bearing housing
mounting bracket
pulley
gear blank
flange
motor adapter
machine frame component
```

These become permanent regression models.

---

# P11 — Assemblies

## Goal

Support mechanical systems composed of multiple parts.

## Component model

Each component needs:

```text
part reference
instance ID
transform
configuration
visibility
suppression state
```

## Mates

Implement:

```text
Fixed
Coincident
Concentric
Parallel
Perpendicular
Distance
Angle
Tangent
Lock
Gear
Rack-and-pinion
Screw
```

## Solver

Assembly state should solve component degrees of freedom.

## Additional capability

Eventually:

```text
collision detection
interference detection
clearance analysis
exploded views
motion constraints
assembly mass properties
```

## Gate

Reference assemblies must solve deterministically without unstable placement.

---

# P12 — Technical Drawings

## Goal

Generate manufacturing-ready drawings from the parametric model.

Implement:

```text
drawing sheets
standard views
projected views
section views
detail views
broken views
dimensions
center marks
center lines
annotations
GD&T foundation
BOM tables
balloons
revision tables
title blocks
```

Export:

```text
PDF
DXF
SVG
```

## Gate

Model changes must propagate into drawing views and dimensions.

---

# P13 — Materials and Engineering Data

## Goal

Make engineering properties part of the model.

Material database should support:

```text
density
elastic modulus
Poisson ratio
yield strength
ultimate strength
thermal conductivity
specific heat
thermal expansion
viscosity
electrical conductivity
```

Support custom materials.

## Gate

Material assignments persist through save/load and propagate into mass and simulation calculations.

---

# P14 — Meshing

## Goal

Create a common simulation mesh infrastructure.

Support initially:

```text
1D
2D
3D
```

Element types:

```text
beam
triangle
quadrilateral
tetrahedron
hexahedron later
```

Capabilities:

```text
global sizing
local sizing
surface refinement
boundary layers later
quality metrics
mesh validation
mesh convergence tools
```

Potential integration:

```text
Gmsh
```

with BetterCAD-owned abstractions.

---

# P15 — Structural FEA

## Goal

Integrate structural analysis directly with the CAD model.

Start with:

```text
linear elasticity
small deformation
static analysis
isotropic materials
```

Support:

```text
fixed supports
forces
pressures
gravity
moments
```

Results:

```text
displacement
strain
stress
von Mises stress
reaction forces
factor of safety
```

## Verification

Use analytical benchmarks:

```text
uniaxial bar
cantilever beam
simply supported beam
plate problems
```

## Gate

FEA must satisfy verification tolerances before optimization uses its results.

---

# P16 — Thermal Analysis

Implement:

```text
steady conduction
transient conduction
heat generation
prescribed temperature
heat flux
convection
thermal contact later
```

Couple temperature fields into structural analysis for thermal expansion.

---

# P17 — CFD

## Goal

Integrate computational fluid dynamics into the same engineering model.

Long-term scope:

```text
incompressible flow
laminar
turbulent
thermal flow
compressible flow
multiphase later
```

Architecture:

```text
CAD
 ↓
fluid volume extraction
 ↓
mesh
 ↓
boundary conditions
 ↓
CFD solver
 ↓
results
 ↓
design parameters
```

Simulation results should remain linked to the originating CAD revision.

---

# P18 — Design Optimization

## Goal

Turn the CAD model into a design-space exploration platform.

Design variables may include:

```text
dimensions
material
feature suppression
angles
thicknesses
mesh parameters
```

Objectives:

```text
minimum mass
minimum cost
minimum pressure drop
minimum drag
minimum stress
maximum stiffness
maximum efficiency
```

Constraints:

```text
stress < allowable
displacement < limit
temperature < limit
factor of safety > target
manufacturing limits
```

Algorithms can eventually include:

```text
parameter sweeps
gradient methods
genetic algorithms
Bayesian optimization
surrogate models
topology optimization
```

---

# P19 — Semantic Topology

## Goal

Reduce one of the most important weaknesses of traditional parametric CAD: fragile topological references.

Do not permanently identify model intent using only:

```text
Face12
Edge27
```

Develop semantic references.

Example:

```text
planar face
generated by Extrude001
normal approximately +Z
largest area
adjacent to Hole003
```

A semantic resolver attempts to recover intended entities after topology-changing edits.

## Research topics

```text
persistent naming
geometric signatures
feature provenance
adjacency graphs
semantic matching
confidence scores
ambiguity detection
```

This is a major differentiating capability for BetterCAD.

---

# P20 — Engineering Version Control

## Goal

Create Git-like concepts for engineering models.

Support:

```text
commit
branch
merge
history
diff
revision
tag
```

But diffs must be semantic.

Example:

```text
Sketch002.width
100 mm → 120 mm

Hole003.diameter
8 mm → 10 mm

Material
Al 6061 → Al 7075
```

Eventually enable branch comparison and controlled merging of independent mechanical changes.

---

# P21 — Python Automation

## Goal

Make the entire platform programmable.

Example:

```python
part = cad.new_part("Bracket")

sketch = part.sketch("XY")
sketch.rectangle(width="100 mm", height="50 mm")

body = sketch.extrude("20 mm")

body.hole(
    diameter="10 mm",
    through_all=True
)

body.fillet(radius="3 mm")
```

Expose:

```text
documents
parameters
sketches
constraints
features
assemblies
materials
simulation
optimization
export
```

The GUI should not have capabilities unavailable to the API without a strong reason.

---

# P22 — AI Engineering Agent

## Goal

Introduce AI only after the engineering APIs are reliable.

AI must operate through structured, validated commands.

Do not allow arbitrary modification of internal geometry.

Architecture:

```text
Natural language
      ↓
Intent extraction
      ↓
Engineering plan
      ↓
Structured API calls
      ↓
Validation
      ↓
CAD modification
      ↓
Simulation / verification
      ↓
Result
```

Example:

```text
"Create a 150 × 80 × 10 mm mounting plate
with four M8 holes 15 mm from each corner."
```

should compile into explicit modeling operations.

Advanced request:

```text
"Reduce this bracket's mass by 20% while keeping
maximum stress below 120 MPa and displacement
below 0.5 mm."
```

Pipeline:

```text
requirement
 ↓
design variables
 ↓
candidate models
 ↓
FEA
 ↓
constraint evaluation
 ↓
optimization
 ↓
verified model
```

---

# P23 — Manufacturing / CAM Foundation

## Goal

Connect design intent to manufacturing.

Initial scope:

```text
manufacturing metadata
machining stock
holes
threads
tolerances
surface finish
sheet thickness
bend information
```

Later:

```text
2.5D CAM
tool library
toolpaths
feeds and speeds
G-code postprocessing
sheet-metal unfolding
additive manufacturing preparation
```

---

# P24 — Performance and GPU

## Goal

Scale BetterCAD to large real-world engineering models.

Profile first.

Optimize based on evidence.

Potential targets:

```text
model regeneration
constraint solving
tessellation
rendering
large assemblies
meshing
FEA
CFD
geometry queries
selection
collision detection
```

GPU usage may include:

```text
rendering
large mesh visualization
selection acceleration
simulation kernels
matrix operations
post-processing
```

Never claim acceleration without CPU/GPU benchmark evidence.

---

# P25 — Production Hardening

## Goal

Prepare BetterCAD for serious engineering use.

Focus on:

```text
crash recovery
autosave
document corruption detection
migration between file versions
large assembly stability
undo/redo reliability
thread safety
determinism
security
plugin isolation
logging
diagnostics
performance profiling
installer
automatic updates
release qualification
```

Testing expands to:

```text
unit
integration
regression
property-based
fuzz
performance
stress
large-model
file compatibility
GUI acceptance
```

---

# P26 — BetterCAD 1.0

A 1.0 release should not mean:

```text
"we implemented many features"
```

It should mean:

```text
reliable enough to design real mechanical systems
```

Minimum expected 1.0 capabilities:

* robust parametric parts;
* stable sketch solver;
* production feature modeling;
* assemblies;
* drawings;
* STEP interoperability;
* deterministic save/load;
* semantic engineering parameters;
* materials;
* mass properties;
* automation API;
* reliable undo/redo;
* regression reference models;
* verified installers/releases;
* comprehensive user documentation.

Simulation and AI may be included in 1.0 only if sufficiently mature.

Do not weaken CAD reliability merely to include them.

---

# Beyond 1.0

Possible future directions:

## Generative engineering

```text
requirements
→ generated concepts
→ simulation
→ ranking
→ manufacturability checks
→ verified candidates
```

## Requirements engineering

Allow explicit requirements such as:

```text
REQ-101
Mass < 2.0 kg

REQ-102
Factor of safety ≥ 2.0

REQ-103
Maximum displacement < 0.25 mm

REQ-104
Manufacturing cost < $50
```

Automatically link verification evidence.

## Digital thread

Connect:

```text
Requirement
→ CAD parameter
→ analysis
→ drawing
→ manufacturing
→ test result
```

## Engineering knowledge

Reusable:

```text
design rules
company standards
materials
preferred suppliers
manufacturing constraints
validation procedures
```

## Cloud / distributed solving

Keep the desktop application functional offline while allowing optional remote:

```text
FEA
CFD
optimization
rendering
collaboration
```

---

# Proposed Release Milestones

## v0.1 — Modeling Foundation

Target:

```text
units
documents
parameters
Open CASCADE
sketch basics
basic constraints
extrude
save/load
CLI
```

A user should be able to create a simple parametric solid.

---

## v0.2 — Practical Part Modeling

Target:

```text
revolve
holes
fillets
chamfers
patterns
datum geometry
improved sketch solver
STEP import/export
desktop workflow
```

---

## v0.3 — Production Part Design

Target:

```text
multi-body parts
shell
draft
sweep
loft
equations
configurations
materials
mass properties
production GUI
```

---

## v0.4 — Assemblies

Target:

```text
components
mates
assembly solver
interference
assembly tree
assembly mass properties
```

---

## v0.5 — Drawings

Target:

```text
drawing sheets
views
sections
dimensions
BOM
PDF/DXF export
```

---

## v0.6 — Simulation Foundation

Target:

```text
meshing
materials
linear structural FEA
thermal foundation
verified benchmarks
```

---

## v0.7 — Multiphysics / Optimization

Target:

```text
CFD integration
thermal coupling
design studies
parameter optimization
simulation-linked CAD
```

---

## v0.8 — Engineering Platform

Target:

```text
Python API
plugin system
engineering version control
semantic diff
automation
```

---

## v0.9 — AI-Native Engineering

Target:

```text
natural-language modeling
model inspection
design modification
simulation orchestration
optimization agent
requirement checking
```

---

## v1.0 — Production BetterCAD

Target:

```text
stable CAD
stable assemblies
stable drawings
interoperability
automation
release qualification
documentation
real engineering reference projects
```

---

# Reference Models

Maintain permanent models that exercise BetterCAD functionality.

Examples:

```text
01_simple_block
02_flanged_shaft
03_bearing_housing
04_mounting_bracket
05_pulley
06_gearbox_housing
07_motor_mount
08_four_bar_linkage
09_small_gearbox_assembly
10_machine_frame
```

Later simulation references:

```text
cantilever_beam
pressure_vessel_segment
heat_sink
pipe_flow
lid_driven_cavity
airfoil_flow
```

These should become regression assets rather than disposable demos.

---

# Quality Gates

Every release candidate should verify:

```text
build
unit tests
integration tests
regression models
serialization
import/export
geometry validity
solver determinism
GUI smoke test
CLI smoke test
performance regression
installer/package
release artifact hashes
```

No checkbox should be marked complete because code merely exists.

Completion means:

```text
implemented
+
tested
+
validated
+
evidence recorded
```

---

# Immediate Development Priority

Current implementation order:

```text
P0 Repository Foundation
 ↓
P1 Engineering Core
 ↓
P2 Document Model
 ↓
P3 Geometry Kernel
 ↓
P4 Sketch Data Model
 ↓
P5 Constraint Solver
 ↓
P6 Extrude
 ↓
P7 Dependency / Regeneration Engine
 ↓
P8 Persistence
 ↓
P9 Minimal Desktop CAD Workflow
```

Do **not** start:

```text
assemblies
FEA
CFD
AI
CAM
cloud collaboration
```

until the parametric modeling foundation is reliable.

The first major objective is simple:

> **Create a small CAD system that can build, edit, regenerate, save, reload, and export a real parametric mechanical part without breaking.**

Once that foundation is trustworthy, BetterCAD can grow into the larger engineering platform described in this roadmap.
