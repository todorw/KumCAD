#pragma once

#include "core/core3d/Document3D.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace lcad {

// STEP/IGES interchange (Sprint 4). Export writes every "tip" shape in the
// document -- a feature that's valid and not itself consumed as another
// feature's inputA/inputB -- as one shape each in the output file. Most
// simple parts have exactly one tip (the end of a single boolean/feature
// chain); a document with several independent, never-combined solids
// exports all of them, which is the useful and expected behavior (matching
// how a real STEP file can hold more than one body).
bool writeStep(const Document3D& doc, const std::string& path);
bool writeIges(const Document3D& doc, const std::string& path);

// Same real writer, for a caller that already has its own shape list rather
// than a Document3D -- e.g. a BIM model's combined shape (see Bim.h's
// combinedBimShape) or an Assembly's placed components (see Assembly.h's
// assemblyPlacedShapes), neither of which lives in a Document3D. Null
// shapes are skipped; fails (false) if none of shapes transfers/adds
// successfully. The Document3D overloads above are implemented in terms of
// this one.
bool writeStep(const std::vector<TopoDS_Shape>& shapes, const std::string& path);
bool writeIges(const std::vector<TopoDS_Shape>& shapes, const std::string& path);

// Import reads the whole file into a single shape (a compound, if the file
// held more than one top-level body) -- the caller then wraps it in a
// FeatureType::Imported feature via Document3D::addImportedShape. Returns a
// null shape (IsNull() true) on any read failure.
TopoDS_Shape readStep(const std::string& path);
TopoDS_Shape readIges(const std::string& path);

// STL: the de facto standard for 3D printing, and unlike STEP/IGES a pure
// triangulated-mesh format, not an exact B-rep one -- writeStl tessellates
// every shape first (BRepMesh_IncrementalMesh; linearDeflection is an
// absolute distance in the document's own units, e.g. mm, not a fraction
// of shape size) before handing the result to OCCT's own StlAPI_Writer,
// and every shape in the file becomes one combined mesh (STL has no
// concept of separate named bodies the way STEP/IGES do). readStl's
// result is a shell of planar triangular faces, not a real parametric
// solid -- a real, disclosed limitation of the format itself, not
// something this wrapper could recover even in principle; it's meant for
// round-tripping/visualization, not for feeding back into further
// parametric editing the way readStep/readIges's results can be.
bool writeStl(const Document3D& doc, const std::string& path, double linearDeflection = 0.1);
bool writeStl(const std::vector<TopoDS_Shape>& shapes, const std::string& path, double linearDeflection = 0.1);
TopoDS_Shape readStl(const std::string& path);

} // namespace lcad
