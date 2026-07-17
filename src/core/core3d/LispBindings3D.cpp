#include "core/core3d/LispBindings3D.h"

#include "core/core3d/Document3D.h"
#include "core/core3d/StepIges.h"
#include "core/lisp/LispInterpreter.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

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
}

} // namespace lcad
