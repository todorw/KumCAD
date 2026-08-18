#include "core/core3d/LispBindings3D.h"

#include "core/core3d/Document3D.h"
#include "core/core3d/StepIges.h"
#include "core/lisp/LispInterpreter.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <stdexcept>

namespace lcad {

namespace {

using Value = LispInterpreter::Value;
using ConsCell = LispInterpreter::ConsCell;

double num(const std::vector<Value>& args, std::size_t i, const std::string& fnName) {
    if (i >= args.size() || args[i].kind != LispInterpreter::Kind::Number) {
        throw std::runtime_error(fnName + ": expected a number argument " + std::to_string(i + 1));
    }
    return args[i].number;
}

Value listFromDoubles(const std::vector<double>& values) {
    Value result = Value::nil();
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        Value cell;
        cell.kind = LispInterpreter::Kind::Cons;
        cell.cell = std::make_shared<ConsCell>();
        cell.cell->car = Value::num(*it);
        cell.cell->cdr = result;
        result = cell;
    }
    return result;
}

// Every creation function's featureIndex-or-nil return convention.
Value indexOrNil(const Document3D& doc, int index) {
    return doc.isValid(index) ? Value::num(index) : Value::nil();
}

// Reuses an existing point at (x, y) if one already sits there (exact-
// match tolerance, not the interactive editor's own screen-pixel-radius
// snap -- a script names a shared vertex by typing the SAME coordinate
// twice, e.g. one SKETCHLINE3D call's end and the next call's start,
// not a fuzzy click), otherwise adds a new fixed point. Without this,
// consecutive SKETCHLINE3D calls would each get their OWN independent
// pair of points -- structurally disconnected, so SketchToFace.cpp's
// loop-chaining (which walks points SHARED by exactly two lines) could
// never find a closed profile to extrude at all.
int findOrAddPoint(Sketch& sketch, double x, double y) {
    for (std::size_t i = 0; i < sketch.points().size(); ++i) {
        const Point2D& p = sketch.points()[i];
        if (std::abs(p.x - x) < 1e-9 && std::abs(p.y - y) < 1e-9) return static_cast<int>(i);
    }
    return sketch.addPoint(Point2D(x, y), true);
}

} // namespace

void registerLisp3DBindings(LispInterpreter& interp, Document3D& doc) {
    interp.registerBuiltin("BOX3D", [&doc](std::vector<Value>& args) -> Value {
        Feature3D f;
        f.type = FeatureType::Box;
        f.p1 = num(args, 0, "BOX3D");
        f.p2 = num(args, 1, "BOX3D");
        f.p3 = num(args, 2, "BOX3D");
        if (args.size() > 3) f.posX = num(args, 3, "BOX3D");
        if (args.size() > 4) f.posY = num(args, 4, "BOX3D");
        if (args.size() > 5) f.posZ = num(args, 5, "BOX3D");
        return indexOrNil(doc, doc.addFeature(f));
    });

    interp.registerBuiltin("CYLINDER3D", [&doc](std::vector<Value>& args) -> Value {
        Feature3D f;
        f.type = FeatureType::Cylinder;
        f.p1 = num(args, 0, "CYLINDER3D");
        f.p2 = num(args, 1, "CYLINDER3D");
        if (args.size() > 2) f.posX = num(args, 2, "CYLINDER3D");
        if (args.size() > 3) f.posY = num(args, 3, "CYLINDER3D");
        if (args.size() > 4) f.posZ = num(args, 4, "CYLINDER3D");
        return indexOrNil(doc, doc.addFeature(f));
    });

    interp.registerBuiltin("SPHERE3D", [&doc](std::vector<Value>& args) -> Value {
        Feature3D f;
        f.type = FeatureType::Sphere;
        f.p1 = num(args, 0, "SPHERE3D");
        if (args.size() > 1) f.posX = num(args, 1, "SPHERE3D");
        if (args.size() > 2) f.posY = num(args, 2, "SPHERE3D");
        if (args.size() > 3) f.posZ = num(args, 3, "SPHERE3D");
        return indexOrNil(doc, doc.addFeature(f));
    });

    auto registerBoolean = [&interp, &doc](const char* name, FeatureType type) {
        interp.registerBuiltin(name, [&doc, type, name](std::vector<Value>& args) -> Value {
            Feature3D f;
            f.type = type;
            f.inputA = static_cast<int>(num(args, 0, name));
            f.inputB = static_cast<int>(num(args, 1, name));
            return indexOrNil(doc, doc.addFeature(f));
        });
    };
    registerBoolean("UNION3D", FeatureType::Union);
    registerBoolean("CUT3D", FeatureType::Cut);
    registerBoolean("INTERSECT3D", FeatureType::Intersect);

    // Closes LispBindings3D.h's own disclosed "no Lisp mini-language for
    // describing a NEW sketch profile" scope cut: SKETCHNEW3D creates an
    // empty sketch (default XY plane, same as the interactive editor's
    // own starting point) and returns its index; SKETCHLINE3D/
    // SKETCHCIRCLE3D/SKETCHARC3D add fixed-point geometry to an existing
    // sketch by that index -- fixed (not solver-free), since a scripted
    // profile is describing exact coordinates directly, the same
    // convention ExternalGeometry.h's own projected points already use.
    // Every one of these still needs a valid sketch index or returns nil,
    // the same "callable from a script without needing its own try/catch"
    // contract every other creation builtin here already has.
    interp.registerBuiltin("SKETCHNEW3D", [&doc](std::vector<Value>&) -> Value {
        return Value::num(doc.addSketch(Sketch{}));
    });

    interp.registerBuiltin("SKETCHLINE3D", [&doc](std::vector<Value>& args) -> Value {
        const int sketchIdx = static_cast<int>(num(args, 0, "SKETCHLINE3D"));
        if (sketchIdx < 0 || sketchIdx >= static_cast<int>(doc.sketches().size())) return Value::nil();
        Sketch& sketch = doc.sketches()[static_cast<std::size_t>(sketchIdx)];
        const int p1 = findOrAddPoint(sketch, num(args, 1, "SKETCHLINE3D"), num(args, 2, "SKETCHLINE3D"));
        const int p2 = findOrAddPoint(sketch, num(args, 3, "SKETCHLINE3D"), num(args, 4, "SKETCHLINE3D"));
        return Value::num(sketch.addLine(p1, p2));
    });

    interp.registerBuiltin("SKETCHCIRCLE3D", [&doc](std::vector<Value>& args) -> Value {
        const int sketchIdx = static_cast<int>(num(args, 0, "SKETCHCIRCLE3D"));
        if (sketchIdx < 0 || sketchIdx >= static_cast<int>(doc.sketches().size())) return Value::nil();
        Sketch& sketch = doc.sketches()[static_cast<std::size_t>(sketchIdx)];
        const int center = findOrAddPoint(sketch, num(args, 1, "SKETCHCIRCLE3D"), num(args, 2, "SKETCHCIRCLE3D"));
        return Value::num(sketch.addCircle(center, num(args, 3, "SKETCHCIRCLE3D")));
    });

    interp.registerBuiltin("SKETCHARC3D", [&doc](std::vector<Value>& args) -> Value {
        const int sketchIdx = static_cast<int>(num(args, 0, "SKETCHARC3D"));
        if (sketchIdx < 0 || sketchIdx >= static_cast<int>(doc.sketches().size())) return Value::nil();
        Sketch& sketch = doc.sketches()[static_cast<std::size_t>(sketchIdx)];
        const int center = findOrAddPoint(sketch, num(args, 1, "SKETCHARC3D"), num(args, 2, "SKETCHARC3D"));
        const int start = findOrAddPoint(sketch, num(args, 3, "SKETCHARC3D"), num(args, 4, "SKETCHARC3D"));
        const int end = findOrAddPoint(sketch, num(args, 5, "SKETCHARC3D"), num(args, 6, "SKETCHARC3D"));
        const double radius = num(args, 7, "SKETCHARC3D");
        const bool ccw = args.size() <= 8 || num(args, 8, "SKETCHARC3D") != 0.0; // optional, defaults true
        return Value::num(sketch.addArc(center, start, end, radius, ccw));
    });

    interp.registerBuiltin("PAD3D", [&doc](std::vector<Value>& args) -> Value {
        Feature3D f;
        f.type = FeatureType::Pad;
        f.sketchIndex = static_cast<int>(num(args, 0, "PAD3D"));
        f.p1 = num(args, 1, "PAD3D");
        return indexOrNil(doc, doc.addFeature(f));
    });

    interp.registerBuiltin("VOLUME3D", [&doc](std::vector<Value>& args) -> Value {
        const int index = static_cast<int>(num(args, 0, "VOLUME3D"));
        if (!doc.isValid(index)) return Value::nil();
        GProp_GProps props;
        BRepGProp::VolumeProperties(doc.shapeAt(index), props);
        return Value::num(props.Mass());
    });

    interp.registerBuiltin("BBOX3D", [&doc](std::vector<Value>& args) -> Value {
        const int index = static_cast<int>(num(args, 0, "BBOX3D"));
        if (!doc.isValid(index)) return Value::nil();
        Bnd_Box box;
        BRepBndLib::Add(doc.shapeAt(index), box);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        return listFromDoubles({xmin, ymin, zmin, xmax, ymax, zmax});
    });

    interp.registerBuiltin("EXPORTSTEP3D", [&doc](std::vector<Value>& args) -> Value {
        if (args.empty() || args[0].kind != LispInterpreter::Kind::String) {
            throw std::runtime_error("EXPORTSTEP3D: expected a file path string");
        }
        return writeStep(doc, args[0].text) ? Value::t() : Value::nil();
    });

    interp.registerBuiltin("EXPORTSTL3D", [&doc](std::vector<Value>& args) -> Value {
        if (args.empty() || args[0].kind != LispInterpreter::Kind::String) {
            throw std::runtime_error("EXPORTSTL3D: expected a file path string");
        }
        return writeStl(doc, args[0].text) ? Value::t() : Value::nil();
    });
}

} // namespace lcad
