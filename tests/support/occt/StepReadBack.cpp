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
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

#include <fstream>

namespace bettercad::test {

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

} // namespace bettercad::test
