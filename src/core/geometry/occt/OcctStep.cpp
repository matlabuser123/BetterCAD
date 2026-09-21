#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctSession.hpp"

#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/core/geometry/Exchange.hpp>

#include <APIHeaderSection_MakeHeader.hxx>
#include <DESTEP_Parameters.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <NCollection_HArray1.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <StepData_StepModel.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <UnitsMethods_LengthUnit.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <cmath>
#include <format>
#include <sstream>
#include <vector>

namespace bettercad::geometry {

namespace {

/// An XCAF document (shapes with names) that is closed again on scope exit,
/// so the kernel's application session does not accumulate documents.
class XcafDocument {
public:
    XcafDocument() : application_(XCAFApp_Application::GetApplication()) {
        application_->NewDocument(TCollection_ExtendedString("MDTV-XCAF"), document_);
    }
    ~XcafDocument() {
        if (!document_.IsNull()) {
            application_->Close(document_);
        }
    }
    XcafDocument(const XcafDocument&) = delete;
    XcafDocument& operator=(const XcafDocument&) = delete;

    [[nodiscard]] const occ::handle<TDocStd_Document>& get() const noexcept { return document_; }

private:
    occ::handle<XCAFApp_Application> application_;
    occ::handle<TDocStd_Document> document_;
};

/// Header strings are plain ASCII in ISO 10303-21; other bytes become '_'.
occ::handle<TCollection_HAsciiString> headerString(const std::string& text) {
    std::string ascii = text;
    for (char& c : ascii) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte > 0x7E) {
            c = '_';
        }
    }
    return new TCollection_HAsciiString(ascii.c_str());
}

occ::handle<NCollection_HArray1<occ::handle<TCollection_HAsciiString>>> headerList(const std::string& text) {
    occ::handle<NCollection_HArray1<occ::handle<TCollection_HAsciiString>>> list =
        new NCollection_HArray1<occ::handle<TCollection_HAsciiString>>(1, 1);
    list->SetValue(1, headerString(text));
    return list;
}

void setName(const TDF_Label& label, const std::string& name) {
    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str(), /*isMultiByte=*/true));
}

/// Translates @p document, writes the header and returns the STEP text.
/// Shared so the part path and the assembly path cannot drift apart in the
/// schema they write, the unit they declare or the header they carry.
Result<std::string> transferAndWrite(const XcafDocument& document, const StepOptions& options) {
    DESTEP_Parameters parameters;
    parameters.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
    parameters.WriteSchema = DESTEP_Parameters::WriteMode_StepSchema_AP214IS;
    STEPCAFControl_Writer writer;
    writer.SetNameMode(true);
    if (!writer.Transfer(document.get(), parameters, STEPControl_AsIs)) {
        return makeError(ErrorCode::Internal, "STEP export: the translator could not convert the bodies");
    }

    APIHeaderSection_MakeHeader header(writer.ChangeWriter().Model());
    header.SetName(headerString(options.name));
    header.SetAuthor(headerList(options.author));
    header.SetOrganization(headerList(options.organization));
    header.SetOriginatingSystem(headerString(options.originatingSystem.empty()
                                                 ? std::format("BetterCAD {}", buildInfo().version)
                                                 : options.originatingSystem));
    if (options.timeStamp) {
        header.SetTimeStamp(headerString(*options.timeStamp));
    }

    std::ostringstream stream;
    if (writer.WriteStream(stream) != IFSelect_RetDone || !stream) {
        return makeError(ErrorCode::Internal, "STEP export: writing the STEP data failed");
    }
    return std::move(stream).str();
}

} // namespace

Result<std::string> writeStep(std::span<const NamedBody> bodies, const StepOptions& options) {
    if (bodies.empty()) {
        return makeError(ErrorCode::InvalidArgument, "STEP export: there are no bodies to write");
    }
    for (const NamedBody& body : bodies) {
        if (body.body.isEmpty()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("STEP export: body '{}' is empty", body.name));
        }
    }

    occt::initializeSession();
    return occt::guardKernelCall("STEP export", [&]() -> Result<std::string> {
        XcafDocument document;
        XCAFDoc_DocumentTool::SetLengthUnit(document.get(), 1.0, UnitsMethods_LengthUnit_Millimeter);
        const occ::handle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document.get()->Main());
        for (const NamedBody& body : bodies) {
            const TDF_Label label = shapes->AddShape(*occt::BodyAccess::shape(body.body), /*makeAssembly=*/false);
            setName(label, body.name);
        }
        return transferAndWrite(document, options);
    });
}

Result<std::string> writeStepAssembly(const StepAssembly& assembly, const StepOptions& options) {
    if (assembly.instances.empty()) {
        return makeError(ErrorCode::InvalidArgument, "STEP export: the assembly has no instances to write");
    }
    for (const NamedBody& part : assembly.parts) {
        if (part.body.isEmpty()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("STEP export: part '{}' is empty", part.name));
        }
    }
    std::vector<bool> used(assembly.parts.size(), false);
    for (const StepInstance& instance : assembly.instances) {
        if (instance.part >= assembly.parts.size()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("STEP export: instance '{}' names part {}, of which there are {}",
                                         instance.name, instance.part, assembly.parts.size()));
        }
        used[instance.part] = true;
        for (const double value : instance.placement.matrix()) {
            if (!std::isfinite(value)) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("STEP export: instance '{}' has a placement that is not finite",
                                             instance.name));
            }
        }
        if (!isFinite(instance.placement.translationPart())) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("STEP export: instance '{}' has a placement that is not finite",
                                         instance.name));
        }
    }
    // A part nobody places would be written as an unreferenced product: a
    // reader would show a part that is not in the assembly, which is worse
    // than refusing to write the file.
    for (std::size_t i = 0; i < used.size(); ++i) {
        if (!used[i]) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("STEP export: part '{}' has no instance", assembly.parts[i].name));
        }
    }

    occt::initializeSession();
    return occt::guardKernelCall("STEP export", [&]() -> Result<std::string> {
        XcafDocument document;
        XCAFDoc_DocumentTool::SetLengthUnit(document.get(), 1.0, UnitsMethods_LengthUnit_Millimeter);
        const occ::handle<XCAFDoc_ShapeTool> shapes = XCAFDoc_DocumentTool::ShapeTool(document.get()->Main());

        // Each part once, as its own product...
        std::vector<TDF_Label> parts;
        parts.reserve(assembly.parts.size());
        for (const NamedBody& part : assembly.parts) {
            const TDF_Label label = shapes->AddShape(*occt::BodyAccess::shape(part.body), /*makeAssembly=*/false);
            setName(label, part.name);
            parts.push_back(label);
        }

        // ...then the assembly that places them. AddComponent references the
        // part's product rather than copying its geometry, which is what
        // makes two instances of one part two placements of one product.
        const TDF_Label root = shapes->NewShape();
        setName(root, assembly.name);
        for (const StepInstance& instance : assembly.instances) {
            const TDF_Label component =
                shapes->AddComponent(root, parts[instance.part], TopLoc_Location(occt::toModel(instance.placement)));
            setName(component, instance.name);
        }
        shapes->UpdateAssemblies();

        return transferAndWrite(document, options);
    });
}

} // namespace bettercad::geometry
