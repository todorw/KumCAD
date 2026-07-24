#include "core/core3d/StepIges.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <StlAPI_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <TopoDS_Compound.hxx>

namespace lcad {

namespace {

// Features that are valid and never referenced as any other feature's
// inputA/inputB -- these are the document's "final bodies" (see StepIges.h).
std::vector<int> tipFeatureIndices(const Document3D& doc) {
    const auto& features = doc.features();
    std::vector<bool> referenced(features.size(), false);
    for (const Feature3D& f : features) {
        if (f.inputA >= 0) referenced[static_cast<std::size_t>(f.inputA)] = true;
        if (f.inputB >= 0) referenced[static_cast<std::size_t>(f.inputB)] = true;
    }
    std::vector<int> tips;
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!referenced[i] && doc.isValid(static_cast<int>(i))) tips.push_back(static_cast<int>(i));
    }
    return tips;
}

} // namespace

bool writeStep(const Document3D& doc, const std::string& path) {
    std::vector<TopoDS_Shape> shapes;
    for (int index : tipFeatureIndices(doc)) shapes.push_back(doc.shapeAt(index));
    return writeStep(shapes, path);
}

bool writeIges(const Document3D& doc, const std::string& path) {
    std::vector<TopoDS_Shape> shapes;
    for (int index : tipFeatureIndices(doc)) shapes.push_back(doc.shapeAt(index));
    return writeIges(shapes, path);
}

bool writeStep(const std::vector<TopoDS_Shape>& shapes, const std::string& path) {
    STEPControl_Writer writer;
    bool any = false;
    for (const TopoDS_Shape& shape : shapes) {
        if (shape.IsNull()) continue;
        if (writer.Transfer(shape, STEPControl_AsIs) == IFSelect_RetDone) any = true;
    }
    if (!any) return false;
    return writer.Write(path.c_str()) == IFSelect_RetDone;
}

bool writeIges(const std::vector<TopoDS_Shape>& shapes, const std::string& path) {
    IGESControl_Writer writer;
    bool any = false;
    for (const TopoDS_Shape& shape : shapes) {
        if (shape.IsNull()) continue;
        if (writer.AddShape(shape)) any = true;
    }
    if (!any) return false;
    writer.ComputeModel();
    return writer.Write(path.c_str());
}

TopoDS_Shape readStep(const std::string& path) {
    STEPControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) return TopoDS_Shape();
    reader.TransferRoots();
    if (reader.NbShapes() < 1) return TopoDS_Shape();
    return reader.OneShape();
}

TopoDS_Shape readIges(const std::string& path) {
    IGESControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) return TopoDS_Shape();
    reader.TransferRoots();
    if (reader.NbShapes() < 1) return TopoDS_Shape();
    return reader.OneShape();
}

bool writeStl(const Document3D& doc, const std::string& path, double linearDeflection) {
    std::vector<TopoDS_Shape> shapes;
    for (int index : tipFeatureIndices(doc)) shapes.push_back(doc.shapeAt(index));
    return writeStl(shapes, path, linearDeflection);
}

bool writeStl(const std::vector<TopoDS_Shape>& shapes, const std::string& path, double linearDeflection) {
    // STL has no concept of separate named bodies (unlike STEP/IGES) --
    // every shape combines into one compound before tessellation, exactly
    // matching a real STL viewer/slicer's own "it's all just triangles"
    // view of the file.
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    bool any = false;
    for (const TopoDS_Shape& shape : shapes) {
        if (shape.IsNull()) continue;
        builder.Add(compound, shape);
        any = true;
    }
    if (!any) return false;

    BRepMesh_IncrementalMesh mesher(compound, linearDeflection, Standard_False, 0.3, Standard_True);
    if (!mesher.IsDone()) return false;

    StlAPI_Writer writer;
    return writer.Write(compound, path.c_str());
}

TopoDS_Shape readStl(const std::string& path) {
    StlAPI_Reader reader;
    TopoDS_Shape shape;
    if (!reader.Read(shape, path.c_str())) return TopoDS_Shape();
    return shape;
}

} // namespace lcad
