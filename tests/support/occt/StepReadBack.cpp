#include "support/occt/StepReadBack.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_Label.hxx>
#include <NCollection_Sequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <fstream>

namespace bettercad::test {

namespace occ = opencascade;

std::optional<StepContents> readStepFile(const std::filesystem::path& path) {
    // Keep the translator's statistics out of the test output.
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
    try {
        // Read through a stream so Unicode paths work on every platform.
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        STEPControl_Reader reader;
        if (reader.ReadStream("export.step", in) != IFSelect_RetDone) {
            return std::nullopt;
        }
        StepContents contents;
        contents.roots = static_cast<std::size_t>(reader.TransferRoots());
        const TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            return std::nullopt;
        }
        for (TopExp_Explorer solids(shape, TopAbs_SOLID); solids.More(); solids.Next()) {
            ++contents.solids;
        }
        // Gauss-Kronrod over knot spans: the adaptive Gauss integration misses
        // the volumes of solids with B-spline or elliptic faces (P12-SKETCH-002
        // kernel probe). Areas are read as before; they are exact for the
        // faces the export tests compare (planes and elementary surfaces).
        GProp_GProps volume;
        BRepGProp::VolumePropertiesGK(shape, volume, 1e-10, /*OnlyClosed=*/true, /*IsUseSpan=*/true);
        GProp_GProps area;
        BRepGProp::SurfaceProperties(shape, area, 1e-10);
        contents.volumeMm3 = volume.Mass();
        contents.areaMm2 = area.Mass();
        contents.valid = BRepCheck_Analyzer(shape).IsValid();
        Bnd_Box box;
        BRepBndLib::AddOptimal(shape, box, /*useTriangulation=*/false, /*useShapeTolerance=*/false);
        if (!box.IsVoid()) {
            box.Get(contents.minMm[0], contents.minMm[1], contents.minMm[2], contents.maxMm[0], contents.maxMm[1],
                    contents.maxMm[2]);
        }
        return contents;
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
}

namespace {

std::string labelName(const TDF_Label& label) {
    occ::handle<TDataStd_Name> name;
    if (label.IsNull() || !label.FindAttribute(TDataStd_Name::GetID(), name)) {
        return {};
    }
    return std::string(TCollection_AsciiString(name->Get()).ToCString());
}

/// Volume, bounds and centre of volume of one placed shape.
StepShape measure(const TopoDS_Shape& shape) {
    StepShape measured;
    GProp_GProps volume;
    BRepGProp::VolumePropertiesGK(shape, volume, 1e-10, /*OnlyClosed=*/true, /*IsUseSpan=*/true);
    measured.volumeMm3 = volume.Mass();
    const gp_Pnt centre = volume.CentreOfMass();
    measured.centroidMm = {centre.X(), centre.Y(), centre.Z()};
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, /*useTriangulation=*/false, /*useShapeTolerance=*/false);
    if (!box.IsVoid()) {
        box.Get(measured.minMm[0], measured.minMm[1], measured.minMm[2], measured.maxMm[0], measured.maxMm[1],
                measured.maxMm[2]);
    }
    return measured;
}

} // namespace

std::optional<StepStructure> readStepStructure(const std::filesystem::path& path) {
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        occ::handle<TDocStd_Document> document;
        XCAFApp_Application::GetApplication()->NewDocument(TCollection_ExtendedString("MDTV-XCAF"), document);
        STEPCAFControl_Reader reader;
        reader.SetNameMode(true);
        if (reader.ReadStream("readback.step", in) != IFSelect_RetDone) {
            return std::nullopt;
        }
        if (!reader.Transfer(document)) {
            return std::nullopt;
        }
        const occ::handle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document->Main());

        StepStructure structure;
        NCollection_Sequence<TDF_Label> roots;
        shapes->GetFreeShapes(roots);
        bool everyShapeValid = true;

        for (int i = 1; i <= roots.Length(); ++i) {
            const TDF_Label root = roots.Value(i);
            if (shapes->IsAssembly(root)) {
                structure.isAssembly = true;
                structure.name = labelName(root);
                NCollection_Sequence<TDF_Label> components;
                shapes->GetComponents(root, components);
                for (int c = 1; c <= components.Length(); ++c) {
                    const TDF_Label component = components.Value(c);
                    // The component carries the placement; the product it
                    // refers to carries the geometry. Reading them apart is
                    // what proves instancing rather than duplication.
                    TDF_Label referred;
                    const bool isReference = shapes->GetReferredShape(component, referred);
                    const TopoDS_Shape placed = shapes->GetShape(component);
                    if (placed.IsNull()) {
                        continue;
                    }
                    StepShape instance = measure(placed);
                    instance.name = labelName(component);
                    instance.product = isReference ? labelName(referred) : std::string{};
                    if (instance.name.empty()) {
                        instance.name = instance.product;
                    }
                    everyShapeValid = everyShapeValid && BRepCheck_Analyzer(placed).IsValid();
                    structure.instances.push_back(std::move(instance));
                }
            } else {
                const TopoDS_Shape shape = shapes->GetShape(root);
                if (shape.IsNull()) {
                    continue;
                }
                StepShape instance = measure(shape);
                instance.name = labelName(root);
                instance.product = instance.name;
                everyShapeValid = everyShapeValid && BRepCheck_Analyzer(shape).IsValid();
                structure.instances.push_back(std::move(instance));
            }
        }

        // The distinct products, in the order they are first placed.
        for (const StepShape& instance : structure.instances) {
            if (instance.product.empty()) {
                continue;
            }
            if (std::find(structure.products.begin(), structure.products.end(), instance.product) ==
                structure.products.end()) {
                structure.products.push_back(instance.product);
            }
        }
        structure.valid = everyShapeValid && !structure.instances.empty();
        XCAFApp_Application::GetApplication()->Close(document);
        return structure;
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
}

} // namespace bettercad::test
