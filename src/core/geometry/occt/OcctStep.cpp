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
#include <UnitsMethods_LengthUnit.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <format>
#include <sstream>

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
            TDataStd_Name::Set(label, TCollection_ExtendedString(body.name.c_str(), /*isMultiByte=*/true));
        }

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
        header.SetOriginatingSystem(headerString(
            options.originatingSystem.empty() ? std::format("BetterCAD {}", buildInfo().version)
                                              : options.originatingSystem));
        if (options.timeStamp) {
            header.SetTimeStamp(headerString(*options.timeStamp));
        }

        std::ostringstream stream;
        if (writer.WriteStream(stream) != IFSelect_RetDone || !stream) {
            return makeError(ErrorCode::Internal, "STEP export: writing the STEP data failed");
        }
        return std::move(stream).str();
    });
}

} // namespace bettercad::geometry
