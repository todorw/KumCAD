#include "Window3D.h"
#include "AssemblyWindow.h"
#include "SketchEditorDialog.h"
#include "SketchFeatureDialog.h"
#include "SketchView.h"
#include "Viewport3D.h"

#include "core/core3d/Cam3D.h"
#include "core/core3d/Commands3D.h"
#include "core/core3d/Fem.h"
#include "core/core3d/Persistence3D.h"
#include "core/core3d/Pick3D.h"
#include "core/core3d/Piping.h"
#include "core/core3d/StepIges.h"
#include "core/core3d/TechDraw.h"
#include "core/document/Document.h"
#include "core/io/DxfWriter.h"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Pnt.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

using lcad::AddFeature3DCommand;
using lcad::Feature3D;
using lcad::FeatureType;
using lcad::UpdateFeature3DCommand;

namespace {

QString typeName(FeatureType type) {
    switch (type) {
    case FeatureType::Box: return QStringLiteral("Box");
    case FeatureType::Cylinder: return QStringLiteral("Cylinder");
    case FeatureType::Sphere: return QStringLiteral("Sphere");
    case FeatureType::Cone: return QStringLiteral("Cone");
    case FeatureType::Torus: return QStringLiteral("Torus");
    case FeatureType::Wedge: return QStringLiteral("Wedge");
    case FeatureType::Union: return QStringLiteral("Union");
    case FeatureType::Cut: return QStringLiteral("Cut");
    case FeatureType::Intersect: return QStringLiteral("Intersect");
    case FeatureType::Pad: return QStringLiteral("Pad");
    case FeatureType::Revolve: return QStringLiteral("Revolve");
    case FeatureType::Fillet: return QStringLiteral("Fillet");
    case FeatureType::Chamfer: return QStringLiteral("Chamfer");
    case FeatureType::LinearPattern: return QStringLiteral("Linear Pattern");
    case FeatureType::PolarPattern: return QStringLiteral("Polar Pattern");
    case FeatureType::Mirror: return QStringLiteral("Mirror");
    case FeatureType::Shell: return QStringLiteral("Shell");
    case FeatureType::Imported: return QStringLiteral("Imported");
    }
    return QStringLiteral("Feature");
}

// Which of p1-p4 are meaningful for type, and what to call them --
// booleans have no editable dimensions here (their shape comes entirely
// from their inputs).
std::vector<QString> paramLabels(FeatureType type) {
    switch (type) {
    case FeatureType::Box: return {QStringLiteral("Dx"), QStringLiteral("Dy"), QStringLiteral("Dz")};
    case FeatureType::Cylinder: return {QStringLiteral("Radius"), QStringLiteral("Height")};
    case FeatureType::Sphere: return {QStringLiteral("Radius")};
    case FeatureType::Cone: return {QStringLiteral("Bottom Radius"), QStringLiteral("Top Radius"), QStringLiteral("Height")};
    case FeatureType::Torus: return {QStringLiteral("Major Radius"), QStringLiteral("Minor Radius")};
    case FeatureType::Wedge:
        return {QStringLiteral("Dx"), QStringLiteral("Dy"), QStringLiteral("Dz"), QStringLiteral("Ltx")};
    default:
        return {};
    }
}

// A simple dimension/position editor for one feature -- not a full
// property browser, just enough to make the feature tree's params
// genuinely editable rather than fixed at creation time.
class FeatureEditDialog : public QDialog {
public:
    explicit FeatureEditDialog(const Feature3D& feature, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Edit %1").arg(typeName(feature.type)));
        auto* form = new QFormLayout(this);

        m_result = feature;
        const std::vector<QString> labels = paramLabels(feature.type);
        const double initial[4] = {feature.p1, feature.p2, feature.p3, feature.p4};
        for (std::size_t i = 0; i < labels.size() && i < 4; ++i) {
            auto* spin = new QDoubleSpinBox(this);
            spin->setRange(0.0, 1e6);
            spin->setDecimals(3);
            spin->setValue(initial[i]);
            form->addRow(labels[i], spin);
            m_paramSpins.push_back(spin);
        }

        for (const auto& [label, value] : std::initializer_list<std::pair<QString, double*>>{
                 {QStringLiteral("Position X"), &m_result.posX},
                 {QStringLiteral("Position Y"), &m_result.posY},
                 {QStringLiteral("Position Z"), &m_result.posZ}}) {
            auto* spin = new QDoubleSpinBox(this);
            spin->setRange(-1e6, 1e6);
            spin->setDecimals(3);
            spin->setValue(*value);
            form->addRow(label, spin);
            m_posSpins.push_back(spin);
        }

        // Only meaningful for primitives/Imported (see Feature3D.h's own
        // field comment); shown for every type anyway, same precedent as
        // Position above always being shown even for types that ignore it.
        for (const auto& [label, value] : std::initializer_list<std::pair<QString, double*>>{
                 {QStringLiteral("Rotation Axis X"), &m_result.rotAxisX},
                 {QStringLiteral("Rotation Axis Y"), &m_result.rotAxisY},
                 {QStringLiteral("Rotation Axis Z"), &m_result.rotAxisZ}}) {
            auto* spin = new QDoubleSpinBox(this);
            spin->setRange(-1.0, 1.0);
            spin->setDecimals(3);
            spin->setValue(*value);
            form->addRow(label, spin);
            m_rotAxisSpins.push_back(spin);
        }
        m_rotAngleSpin = new QDoubleSpinBox(this);
        m_rotAngleSpin->setRange(-360.0, 360.0);
        m_rotAngleSpin->setDecimals(2);
        m_rotAngleSpin->setValue(m_result.rotAngle);
        form->addRow(QStringLiteral("Rotation Angle (deg):"), m_rotAngleSpin);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    Feature3D result() const {
        Feature3D f = m_result;
        double* const params[4] = {&f.p1, &f.p2, &f.p3, &f.p4};
        for (std::size_t i = 0; i < m_paramSpins.size(); ++i) *params[i] = m_paramSpins[i]->value();
        f.posX = m_posSpins[0]->value();
        f.posY = m_posSpins[1]->value();
        f.posZ = m_posSpins[2]->value();
        f.rotAxisX = m_rotAxisSpins[0]->value();
        f.rotAxisY = m_rotAxisSpins[1]->value();
        f.rotAxisZ = m_rotAxisSpins[2]->value();
        f.rotAngle = m_rotAngleSpin->value();
        return f;
    }

private:
    Feature3D m_result;
    std::vector<QDoubleSpinBox*> m_paramSpins;
    std::vector<QDoubleSpinBox*> m_posSpins;
    std::vector<QDoubleSpinBox*> m_rotAxisSpins;
    QDoubleSpinBox* m_rotAngleSpin = nullptr;
};

// Flat lengths/bend angles are entered as comma-separated numbers rather
// than an interactive strip editor -- consistent with this sprint's own
// "no face/edge picking in the still-unverified viewport" scope cut.
class SheetMetalDialog : public QDialog {
public:
    explicit SheetMetalDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Sheet Metal Part"));
        auto* form = new QFormLayout(this);

        m_width = new QDoubleSpinBox(this);
        m_width->setRange(0.1, 1e6);
        m_width->setValue(20.0);
        form->addRow(QStringLiteral("Width:"), m_width);

        m_thickness = new QDoubleSpinBox(this);
        m_thickness->setRange(0.01, 1e6);
        m_thickness->setDecimals(3);
        m_thickness->setValue(2.0);
        form->addRow(QStringLiteral("Thickness:"), m_thickness);

        m_bendRadius = new QDoubleSpinBox(this);
        m_bendRadius->setRange(0.0, 1e6);
        m_bendRadius->setDecimals(3);
        m_bendRadius->setValue(1.0);
        form->addRow(QStringLiteral("Bend Radius:"), m_bendRadius);

        m_kFactor = new QDoubleSpinBox(this);
        m_kFactor->setRange(0.0, 1.0);
        m_kFactor->setDecimals(3);
        m_kFactor->setValue(0.44);
        form->addRow(QStringLiteral("K-Factor:"), m_kFactor);

        m_flatLengths = new QLineEdit(QStringLiteral("30, 25"), this);
        form->addRow(QStringLiteral("Flat Lengths (comma-separated):"), m_flatLengths);

        m_bendAngles = new QLineEdit(QStringLiteral("90"), this);
        form->addRow(QStringLiteral("Bend Angles, degrees (comma-separated, one fewer than flats):"), m_bendAngles);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::SheetMetalPart result() const {
        lcad::SheetMetalPart part;
        part.width = m_width->value();
        part.thickness = m_thickness->value();
        part.bendRadius = m_bendRadius->value();
        part.kFactor = m_kFactor->value();
        part.flatLengths = parseNumbers(m_flatLengths->text());
        part.bendAngles = parseNumbers(m_bendAngles->text());
        return part;
    }

private:
    static std::vector<double> parseNumbers(const QString& text) {
        std::vector<double> values;
        for (const QString& token : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const double value = token.trimmed().toDouble(&ok);
            if (ok) values.push_back(value);
        }
        return values;
    }

    QDoubleSpinBox* m_width;
    QDoubleSpinBox* m_thickness;
    QDoubleSpinBox* m_bendRadius;
    QDoubleSpinBox* m_kFactor;
    QLineEdit* m_flatLengths;
    QLineEdit* m_bendAngles;
};

// Two rays (an edge pick, a reference-face pick, see Pick3D.h) plus the
// flange's own length/angle/thickness -- rays are typed in rather than
// live-clicked, the same "typed-in ray, real picked geometry" precedent
// AssemblyWindow's own PickRayDialog set.
class FaceFlangeDialog : public QDialog {
public:
    explicit FaceFlangeDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Face Flange"));
        auto* form = new QFormLayout(this);

        m_edgeOx = makeSpin(0.0); m_edgeOy = makeSpin(0.0); m_edgeOz = makeSpin(100.0);
        form->addRow(QStringLiteral("Edge-Pick Ray Origin X/Y/Z:"), rowOf({m_edgeOx, m_edgeOy, m_edgeOz}));
        m_edgeDx = makeSpin(0.0); m_edgeDy = makeSpin(0.0); m_edgeDz = makeSpin(-1.0);
        form->addRow(QStringLiteral("Edge-Pick Ray Direction X/Y/Z:"), rowOf({m_edgeDx, m_edgeDy, m_edgeDz}));

        m_faceOx = makeSpin(0.0); m_faceOy = makeSpin(0.0); m_faceOz = makeSpin(100.0);
        form->addRow(QStringLiteral("Face-Pick Ray Origin X/Y/Z:"), rowOf({m_faceOx, m_faceOy, m_faceOz}));
        m_faceDx = makeSpin(0.0); m_faceDy = makeSpin(0.0); m_faceDz = makeSpin(-1.0);
        form->addRow(QStringLiteral("Face-Pick Ray Direction X/Y/Z:"), rowOf({m_faceDx, m_faceDy, m_faceDz}));

        m_length = new QDoubleSpinBox(this);
        m_length->setRange(0.1, 1e6);
        m_length->setValue(20.0);
        form->addRow(QStringLiteral("Flange Length:"), m_length);
        m_bendAngle = new QDoubleSpinBox(this);
        m_bendAngle->setRange(-180.0, 180.0);
        m_bendAngle->setValue(90.0);
        form->addRow(QStringLiteral("Bend Angle (deg, 0=coplanar, 90=perpendicular):"), m_bendAngle);
        m_thickness = new QDoubleSpinBox(this);
        m_thickness->setRange(0.01, 1e6);
        m_thickness->setDecimals(3);
        m_thickness->setValue(2.0);
        form->addRow(QStringLiteral("Thickness:"), m_thickness);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::PickRay edgeRay() const {
        lcad::PickRay r;
        r.origin = {m_edgeOx->value(), m_edgeOy->value(), m_edgeOz->value()};
        r.direction = {m_edgeDx->value(), m_edgeDy->value(), m_edgeDz->value()};
        return r;
    }
    lcad::PickRay faceRay() const {
        lcad::PickRay r;
        r.origin = {m_faceOx->value(), m_faceOy->value(), m_faceOz->value()};
        r.direction = {m_faceDx->value(), m_faceDy->value(), m_faceDz->value()};
        return r;
    }
    double length() const { return m_length->value(); }
    double bendAngle() const { return m_bendAngle->value(); }
    double thickness() const { return m_thickness->value(); }

private:
    QDoubleSpinBox* makeSpin(double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(-1e6, 1e6);
        spin->setDecimals(3);
        spin->setValue(value);
        return spin;
    }
    QWidget* rowOf(std::initializer_list<QWidget*> widgets) {
        auto* container = new QWidget(this);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        for (QWidget* w : widgets) layout->addWidget(w);
        return container;
    }

    QDoubleSpinBox *m_edgeOx, *m_edgeOy, *m_edgeOz, *m_edgeDx, *m_edgeDy, *m_edgeDz;
    QDoubleSpinBox *m_faceOx, *m_faceOy, *m_faceOz, *m_faceDx, *m_faceDy, *m_faceDz;
    QDoubleSpinBox* m_length;
    QDoubleSpinBox* m_bendAngle;
    QDoubleSpinBox* m_thickness;
};

class WallDialog : public QDialog {
public:
    explicit WallDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Wall"));
        auto* form = new QFormLayout(this);
        m_x1 = makeSpin(0.0);
        m_y1 = makeSpin(0.0);
        m_x2 = makeSpin(5000.0);
        m_y2 = makeSpin(0.0);
        m_height = makeSpin(2700.0);
        m_thickness = makeSpin(200.0);
        form->addRow(QStringLiteral("Start X/Y:"), rowOf({m_x1, m_y1}));
        form->addRow(QStringLiteral("End X/Y:"), rowOf({m_x2, m_y2}));
        form->addRow(QStringLiteral("Height:"), m_height);
        form->addRow(QStringLiteral("Thickness:"), m_thickness);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::Wall result() const {
        lcad::Wall wall;
        wall.x1 = m_x1->value();
        wall.y1 = m_y1->value();
        wall.x2 = m_x2->value();
        wall.y2 = m_y2->value();
        wall.height = m_height->value();
        wall.thickness = m_thickness->value();
        return wall;
    }

private:
    QDoubleSpinBox* makeSpin(double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(-1e7, 1e7);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    }
    QWidget* rowOf(std::initializer_list<QWidget*> widgets) {
        auto* container = new QWidget(this);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        for (QWidget* w : widgets) layout->addWidget(w);
        return container;
    }

    QDoubleSpinBox *m_x1, *m_y1, *m_x2, *m_y2, *m_height, *m_thickness;
};

class OpeningDialog : public QDialog {
public:
    OpeningDialog(int wallCount, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Door/Window"));
        auto* form = new QFormLayout(this);

        m_wallIndex = new QComboBox(this);
        for (int i = 0; i < wallCount; ++i) m_wallIndex->addItem(QStringLiteral("Wall %1").arg(i), i);
        form->addRow(QStringLiteral("Wall:"), m_wallIndex);

        m_isWindow = new QCheckBox(QStringLiteral("This is a window (unchecked = door)"), this);
        form->addRow(QString(), m_isWindow);

        m_offset = makeSpin(500.0);
        form->addRow(QStringLiteral("Offset along wall:"), m_offset);
        m_width = makeSpin(900.0);
        form->addRow(QStringLiteral("Width:"), m_width);
        m_height = makeSpin(2100.0);
        form->addRow(QStringLiteral("Height:"), m_height);
        m_sillHeight = makeSpin(0.0);
        form->addRow(QStringLiteral("Sill height (0 for a door):"), m_sillHeight);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::Opening result() const {
        lcad::Opening opening;
        opening.wallIndex = m_wallIndex->currentData().toInt();
        opening.isWindow = m_isWindow->isChecked();
        opening.offsetAlongWall = m_offset->value();
        opening.width = m_width->value();
        opening.height = m_height->value();
        opening.sillHeight = m_sillHeight->value();
        return opening;
    }

private:
    QDoubleSpinBox* makeSpin(double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(0.0, 1e7);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    }

    QComboBox* m_wallIndex;
    QCheckBox* m_isWindow;
    QDoubleSpinBox *m_offset, *m_width, *m_height, *m_sillHeight;
};

class SlabDialog : public QDialog {
public:
    explicit SlabDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Slab"));
        auto* form = new QFormLayout(this);

        m_boundary = new QLineEdit(QStringLiteral("0,0, 5000,0, 5000,4000, 0,4000"), this);
        form->addRow(QStringLiteral("Boundary (x,y pairs, comma-separated):"), m_boundary);
        m_thickness = new QDoubleSpinBox(this);
        m_thickness->setRange(0.1, 1e6);
        m_thickness->setValue(150.0);
        form->addRow(QStringLiteral("Thickness:"), m_thickness);
        m_elevation = new QDoubleSpinBox(this);
        m_elevation->setRange(-1e6, 1e6);
        m_elevation->setValue(0.0);
        form->addRow(QStringLiteral("Elevation:"), m_elevation);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::Slab result() const {
        lcad::Slab slab;
        std::vector<double> values;
        for (const QString& token : m_boundary->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const double v = token.trimmed().toDouble(&ok);
            if (ok) values.push_back(v);
        }
        for (std::size_t i = 0; i + 1 < values.size(); i += 2) slab.boundary.emplace_back(values[i], values[i + 1]);
        slab.thickness = m_thickness->value();
        slab.elevation = m_elevation->value();
        return slab;
    }

private:
    QLineEdit* m_boundary;
    QDoubleSpinBox* m_thickness;
    QDoubleSpinBox* m_elevation;
};

class ColumnDialog : public QDialog {
public:
    explicit ColumnDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Column"));
        auto* form = new QFormLayout(this);
        m_x = makeSpin(0.0);
        m_y = makeSpin(0.0);
        form->addRow(QStringLiteral("Position X/Y:"), rowOf({m_x, m_y}));
        m_baseElevation = makeSpin(0.0);
        form->addRow(QStringLiteral("Base Elevation:"), m_baseElevation);
        m_height = makeSpin(3000.0);
        form->addRow(QStringLiteral("Height:"), m_height);
        m_width = makeSpin(300.0);
        form->addRow(QStringLiteral("Width (diameter if round):"), m_width);
        m_depth = makeSpin(300.0);
        form->addRow(QStringLiteral("Depth (unused if round):"), m_depth);
        m_round = new QCheckBox(QStringLiteral("Round (circular cross-section)"), this);
        form->addRow(QString(), m_round);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::Column result() const {
        lcad::Column column;
        column.x = m_x->value();
        column.y = m_y->value();
        column.baseElevation = m_baseElevation->value();
        column.height = m_height->value();
        column.width = m_width->value();
        column.depth = m_depth->value();
        column.round = m_round->isChecked();
        return column;
    }

private:
    QDoubleSpinBox* makeSpin(double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(-1e7, 1e7);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    }
    QWidget* rowOf(std::initializer_list<QWidget*> widgets) {
        auto* container = new QWidget(this);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        for (QWidget* w : widgets) layout->addWidget(w);
        return container;
    }

    QDoubleSpinBox *m_x, *m_y, *m_baseElevation, *m_height, *m_width, *m_depth;
    QCheckBox* m_round;
};

class BeamDialog : public QDialog {
public:
    explicit BeamDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Beam"));
        auto* form = new QFormLayout(this);
        m_x1 = makeSpin(0.0);
        m_y1 = makeSpin(0.0);
        m_x2 = makeSpin(4000.0);
        m_y2 = makeSpin(0.0);
        form->addRow(QStringLiteral("Start X/Y:"), rowOf({m_x1, m_y1}));
        form->addRow(QStringLiteral("End X/Y:"), rowOf({m_x2, m_y2}));
        m_elevation = makeSpin(2700.0);
        form->addRow(QStringLiteral("Elevation (bottom face):"), m_elevation);
        m_width = makeSpin(300.0);
        form->addRow(QStringLiteral("Width:"), m_width);
        m_depth = makeSpin(400.0);
        form->addRow(QStringLiteral("Depth:"), m_depth);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::Beam result() const {
        lcad::Beam beam;
        beam.x1 = m_x1->value();
        beam.y1 = m_y1->value();
        beam.x2 = m_x2->value();
        beam.y2 = m_y2->value();
        beam.elevation = m_elevation->value();
        beam.width = m_width->value();
        beam.depth = m_depth->value();
        return beam;
    }

private:
    QDoubleSpinBox* makeSpin(double value) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(-1e7, 1e7);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    }
    QWidget* rowOf(std::initializer_list<QWidget*> widgets) {
        auto* container = new QWidget(this);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        for (QWidget* w : widgets) layout->addWidget(w);
        return container;
    }

    QDoubleSpinBox *m_x1, *m_y1, *m_x2, *m_y2, *m_elevation, *m_width, *m_depth;
};

class SpaceDialog : public QDialog {
public:
    explicit SpaceDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Room/Space"));
        auto* form = new QFormLayout(this);

        m_name = new QLineEdit(QStringLiteral("Room"), this);
        form->addRow(QStringLiteral("Name:"), m_name);
        m_boundary = new QLineEdit(QStringLiteral("0,0, 4000,0, 4000,3000, 0,3000"), this);
        form->addRow(QStringLiteral("Boundary (x,y pairs, comma-separated):"), m_boundary);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::Space result() const {
        lcad::Space space;
        space.name = m_name->text().toStdString();
        std::vector<double> values;
        for (const QString& token : m_boundary->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const double v = token.trimmed().toDouble(&ok);
            if (ok) values.push_back(v);
        }
        for (std::size_t i = 0; i + 1 < values.size(); i += 2) space.boundary.emplace_back(values[i], values[i + 1]);
        return space;
    }

private:
    QLineEdit* m_name;
    QLineEdit* m_boundary;
};

class PipeRunDialog : public QDialog {
public:
    explicit PipeRunDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Add Pipe Run"));
        auto* form = new QFormLayout(this);

        m_path = new QLineEdit(QStringLiteral("0,0,0, 500,0,0, 500,500,0"), this);
        form->addRow(QStringLiteral("Path (x,y,z triples, comma-separated):"), m_path);
        m_radius = new QDoubleSpinBox(this);
        m_radius->setRange(0.1, 1e6);
        m_radius->setValue(25.0);
        form->addRow(QStringLiteral("Outer Radius:"), m_radius);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::PipeRun result() const {
        lcad::PipeRun run;
        run.outerRadius = m_radius->value();
        std::vector<double> values;
        for (const QString& token : m_path->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const double v = token.trimmed().toDouble(&ok);
            if (ok) values.push_back(v);
        }
        for (std::size_t i = 0; i + 2 < values.size(); i += 3) run.path.push_back({values[i], values[i + 1], values[i + 2]});
        return run;
    }

private:
    QLineEdit* m_path;
    QDoubleSpinBox* m_radius;
};

class Cam3DDialog : public QDialog {
public:
    explicit Cam3DDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Generate 3D CAM Toolpath"));
        auto* form = new QFormLayout(this);

        m_toolDiameter = new QDoubleSpinBox(this);
        m_toolDiameter->setRange(0.1, 1e4);
        m_toolDiameter->setValue(6.0);
        form->addRow(QStringLiteral("Tool Diameter:"), m_toolDiameter);

        m_side = new QComboBox(this);
        m_side->addItem(QStringLiteral("Outside"), static_cast<int>(lcad::CutSide::Outside));
        m_side->addItem(QStringLiteral("Inside"), static_cast<int>(lcad::CutSide::Inside));
        m_side->addItem(QStringLiteral("On Line"), static_cast<int>(lcad::CutSide::OnLine));
        form->addRow(QStringLiteral("Cut Side:"), m_side);

        m_stepDown = new QDoubleSpinBox(this);
        m_stepDown->setRange(0.01, 1e4);
        m_stepDown->setValue(2.0);
        form->addRow(QStringLiteral("Step Down (Z per level):"), m_stepDown);

        m_feedRate = new QDoubleSpinBox(this);
        m_feedRate->setRange(1.0, 1e5);
        m_feedRate->setValue(800.0);
        form->addRow(QStringLiteral("Feed Rate:"), m_feedRate);

        m_plungeRate = new QDoubleSpinBox(this);
        m_plungeRate->setRange(1.0, 1e5);
        m_plungeRate->setValue(200.0);
        form->addRow(QStringLiteral("Plunge Rate:"), m_plungeRate);

        m_safeHeight = new QDoubleSpinBox(this);
        m_safeHeight->setRange(0.1, 1e4);
        m_safeHeight->setValue(10.0);
        form->addRow(QStringLiteral("Safe Height:"), m_safeHeight);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::Cam3DParams result() const {
        lcad::Cam3DParams params;
        params.toolDiameter = m_toolDiameter->value();
        params.side = static_cast<lcad::CutSide>(m_side->currentData().toInt());
        params.stepDown = m_stepDown->value();
        params.feedRate = m_feedRate->value();
        params.plungeRate = m_plungeRate->value();
        params.safeHeight = m_safeHeight->value();
        return params;
    }

private:
    QDoubleSpinBox* m_toolDiameter;
    QComboBox* m_side;
    QDoubleSpinBox* m_stepDown;
    QDoubleSpinBox* m_feedRate;
    QDoubleSpinBox* m_plungeRate;
    QDoubleSpinBox* m_safeHeight;
};

class FemDialog : public QDialog {
public:
    explicit FemDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Run FEM Analysis"));
        auto* form = new QFormLayout(this);

        m_divisions = new QSpinBox(this);
        m_divisions->setRange(1, 12);
        m_divisions->setValue(5);
        form->addRow(QStringLiteral("Mesh Divisions (coarse, single digits):"), m_divisions);

        m_youngsModulus = new QDoubleSpinBox(this);
        m_youngsModulus->setRange(1.0, 1e7);
        m_youngsModulus->setValue(200000.0);
        form->addRow(QStringLiteral("Young's Modulus:"), m_youngsModulus);

        m_poissonsRatio = new QDoubleSpinBox(this);
        m_poissonsRatio->setRange(0.0, 0.49);
        m_poissonsRatio->setDecimals(3);
        m_poissonsRatio->setValue(0.3);
        form->addRow(QStringLiteral("Poisson's Ratio:"), m_poissonsRatio);

        m_fixedXMax = new QDoubleSpinBox(this);
        m_fixedXMax->setRange(-1e6, 1e6);
        m_fixedXMax->setDecimals(3);
        m_fixedXMax->setValue(0.0);
        form->addRow(QStringLiteral("Fix every node with X <=:"), m_fixedXMax);

        m_loadPoint = new QLineEdit(QStringLiteral("100,0,0"), this);
        form->addRow(QStringLiteral("Load Point (x,y,z, nearest node):"), m_loadPoint);
        m_loadForce = new QLineEdit(QStringLiteral("1000,0,0"), this);
        form->addRow(QStringLiteral("Load Force Vector (x,y,z):"), m_loadForce);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    int divisions() const { return m_divisions->value(); }

    lcad::FemMaterial material() const {
        lcad::FemMaterial material;
        material.youngsModulus = m_youngsModulus->value();
        material.poissonsRatio = m_poissonsRatio->value();
        return material;
    }

    lcad::FemBoundaryCondition boundaryCondition() const {
        lcad::FemBoundaryCondition bc;
        bc.fixedXMax = m_fixedXMax->value();
        return bc;
    }

    std::vector<lcad::FemLoad> loads() const {
        const std::array<double, 3> point = parseTriplet(m_loadPoint->text());
        const std::array<double, 3> force = parseTriplet(m_loadForce->text());
        return {lcad::FemLoad{point, force}};
    }

private:
    static std::array<double, 3> parseTriplet(const QString& text) {
        std::array<double, 3> result{0.0, 0.0, 0.0};
        const QStringList tokens = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (int i = 0; i < 3 && i < tokens.size(); ++i) result[static_cast<std::size_t>(i)] = tokens[i].trimmed().toDouble();
        return result;
    }

    QSpinBox* m_divisions;
    QDoubleSpinBox* m_youngsModulus;
    QDoubleSpinBox* m_poissonsRatio;
    QDoubleSpinBox* m_fixedXMax;
    QLineEdit* m_loadPoint;
    QLineEdit* m_loadForce;
};

// FEMMODAL: no loads (a vibration mode doesn't need any), but adds a
// density field solveLinearStatic never needed.
class FemModalDialog : public QDialog {
public:
    explicit FemModalDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Run FEM Modal Analysis"));
        auto* form = new QFormLayout(this);

        m_divisions = new QSpinBox(this);
        m_divisions->setRange(1, 12);
        m_divisions->setValue(4);
        form->addRow(QStringLiteral("Mesh Divisions (coarse, single digits):"), m_divisions);

        m_youngsModulus = new QDoubleSpinBox(this);
        m_youngsModulus->setRange(1.0, 1e7);
        m_youngsModulus->setValue(200000.0);
        form->addRow(QStringLiteral("Young's Modulus:"), m_youngsModulus);

        m_poissonsRatio = new QDoubleSpinBox(this);
        m_poissonsRatio->setRange(0.0, 0.49);
        m_poissonsRatio->setDecimals(3);
        m_poissonsRatio->setValue(0.3);
        form->addRow(QStringLiteral("Poisson's Ratio:"), m_poissonsRatio);

        m_density = new QDoubleSpinBox(this);
        m_density->setRange(1e-6, 1e6);
        m_density->setDecimals(6);
        m_density->setValue(1.0);
        form->addRow(QStringLiteral("Density:"), m_density);

        m_fixedXMax = new QDoubleSpinBox(this);
        m_fixedXMax->setRange(-1e6, 1e6);
        m_fixedXMax->setDecimals(3);
        m_fixedXMax->setValue(0.0);
        form->addRow(QStringLiteral("Fix every node with X <=:"), m_fixedXMax);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    int divisions() const { return m_divisions->value(); }

    lcad::FemMaterial material() const {
        lcad::FemMaterial material;
        material.youngsModulus = m_youngsModulus->value();
        material.poissonsRatio = m_poissonsRatio->value();
        material.density = m_density->value();
        return material;
    }

    lcad::FemBoundaryCondition boundaryCondition() const {
        lcad::FemBoundaryCondition bc;
        bc.fixedXMax = m_fixedXMax->value();
        return bc;
    }

private:
    QSpinBox* m_divisions;
    QDoubleSpinBox* m_youngsModulus;
    QDoubleSpinBox* m_poissonsRatio;
    QDoubleSpinBox* m_density;
    QDoubleSpinBox* m_fixedXMax;
};

// FEMTHERMAL: steady-state heat conduction -- a fixed-temperature plane
// (like the structural/modal dialogs' own fixedXMax clamp) plus one
// point heat source, analogous to FemDialog's own point load.
class FemThermalDialog : public QDialog {
public:
    explicit FemThermalDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Run FEM Thermal Analysis"));
        auto* form = new QFormLayout(this);

        m_divisions = new QSpinBox(this);
        m_divisions->setRange(1, 12);
        m_divisions->setValue(5);
        form->addRow(QStringLiteral("Mesh Divisions (coarse, single digits):"), m_divisions);

        m_conductivity = new QDoubleSpinBox(this);
        m_conductivity->setRange(1e-6, 1e7);
        m_conductivity->setDecimals(3);
        m_conductivity->setValue(50.0);
        form->addRow(QStringLiteral("Thermal Conductivity:"), m_conductivity);

        m_fixedXMax = new QDoubleSpinBox(this);
        m_fixedXMax->setRange(-1e6, 1e6);
        m_fixedXMax->setDecimals(3);
        m_fixedXMax->setValue(0.0);
        form->addRow(QStringLiteral("Fix every node with X <= to Temperature:"), m_fixedXMax);

        m_fixedTemperature = new QDoubleSpinBox(this);
        m_fixedTemperature->setRange(-1e6, 1e6);
        m_fixedTemperature->setDecimals(3);
        m_fixedTemperature->setValue(20.0);
        form->addRow(QStringLiteral("Fixed Temperature:"), m_fixedTemperature);

        m_loadPoint = new QLineEdit(QStringLiteral("100,0,0"), this);
        form->addRow(QStringLiteral("Heat Source Point (x,y,z, nearest node):"), m_loadPoint);
        m_heatRate = new QDoubleSpinBox(this);
        m_heatRate->setRange(-1e9, 1e9);
        m_heatRate->setDecimals(3);
        m_heatRate->setValue(1000.0);
        form->addRow(QStringLiteral("Heat Rate:"), m_heatRate);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    int divisions() const { return m_divisions->value(); }

    lcad::FemThermalMaterial material() const {
        lcad::FemThermalMaterial material;
        material.thermalConductivity = m_conductivity->value();
        return material;
    }

    lcad::FemThermalBoundaryCondition boundaryCondition() const {
        lcad::FemThermalBoundaryCondition bc;
        bc.fixedXMax = m_fixedXMax->value();
        bc.fixedTemperature = m_fixedTemperature->value();
        return bc;
    }

    std::vector<lcad::ThermalLoad> loads() const {
        const QStringList tokens = m_loadPoint->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
        std::array<double, 3> point{0.0, 0.0, 0.0};
        for (int i = 0; i < 3 && i < tokens.size(); ++i) point[static_cast<std::size_t>(i)] = tokens[i].trimmed().toDouble();
        return {lcad::ThermalLoad{point, m_heatRate->value()}};
    }

private:
    QSpinBox* m_divisions;
    QDoubleSpinBox* m_conductivity;
    QDoubleSpinBox* m_fixedXMax;
    QDoubleSpinBox* m_fixedTemperature;
    QLineEdit* m_loadPoint;
    QDoubleSpinBox* m_heatRate;
};

} // namespace

Window3D::Window3D(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("KumCAD — 3D Modeling (early preview)"));
    resize(1200, 800);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("Import STEP..."), this, &Window3D::importStepFile);
    fileMenu->addAction(QStringLiteral("Import IGES..."), this, &Window3D::importIgesFile);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Export STEP..."), this, &Window3D::exportStepFile);
    fileMenu->addAction(QStringLiteral("Export IGES..."), this, &Window3D::exportIgesFile);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Save As .kcad3d..."), this, &Window3D::saveKcad3dFile);
    fileMenu->addAction(QStringLiteral("Open .kcad3d..."), this, &Window3D::openKcad3dFile);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("New Assembly Window..."), this, &Window3D::openAssemblyWindow);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Generate Drawing Views..."), this, &Window3D::generateDrawingViews);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Add Sheet Metal Part..."), this, &Window3D::addSheetMetalPart);
    fileMenu->addAction(QStringLiteral("Export Flat Pattern..."), this, &Window3D::exportFlatPattern);
    fileMenu->addAction(QStringLiteral("Add Face Flange..."), this, &Window3D::addFaceFlange);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("BIM: Add Wall..."), this, &Window3D::addBimWall);
    fileMenu->addAction(QStringLiteral("BIM: Add Door/Window..."), this, &Window3D::addBimOpening);
    fileMenu->addAction(QStringLiteral("BIM: Add Slab..."), this, &Window3D::addBimSlab);
    fileMenu->addAction(QStringLiteral("BIM: Add Column..."), this, &Window3D::addBimColumn);
    fileMenu->addAction(QStringLiteral("BIM: Add Beam..."), this, &Window3D::addBimBeam);
    fileMenu->addAction(QStringLiteral("BIM: Add Room/Space..."), this, &Window3D::addBimSpace);
    fileMenu->addAction(QStringLiteral("BIM: Import IFC-lite..."), this, &Window3D::importIfcLite);
    fileMenu->addAction(QStringLiteral("BIM: Export IFC-lite..."), this, &Window3D::exportIfcLite);
    fileMenu->addAction(QStringLiteral("BIM: Export Opening Schedule..."), this, &Window3D::exportOpeningSchedule);
    fileMenu->addAction(QStringLiteral("BIM: Export Room Schedule..."), this, &Window3D::exportRoomSchedule);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Add Pipe Run..."), this, &Window3D::addPipeRun);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Generate 3D CAM Toolpath..."), this, &Window3D::generate3DCamToolpath);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Run FEM Analysis..."), this, &Window3D::runFemAnalysis);
    fileMenu->addAction(QStringLiteral("Run FEM Modal Analysis..."), this, &Window3D::runFemModalAnalysis);
    fileMenu->addAction(QStringLiteral("Run FEM Thermal Analysis..."), this, &Window3D::runFemThermalAnalysis);

    m_viewport = new Viewport3D(this);
    setCentralWidget(m_viewport);

    m_featureList = new QListWidget(this);
    auto* dock = new QDockWidget(QStringLiteral("Features"), this);
    dock->setWidget(m_featureList);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    QToolBar* toolbar = addToolBar(QStringLiteral("Features"));
    toolbar->addAction(QStringLiteral("Box"), this, [this] { addPrimitive(FeatureType::Box); });
    toolbar->addAction(QStringLiteral("Cylinder"), this, [this] { addPrimitive(FeatureType::Cylinder); });
    toolbar->addAction(QStringLiteral("Sphere"), this, [this] { addPrimitive(FeatureType::Sphere); });
    toolbar->addAction(QStringLiteral("Cone"), this, [this] { addPrimitive(FeatureType::Cone); });
    toolbar->addAction(QStringLiteral("Torus"), this, [this] { addPrimitive(FeatureType::Torus); });
    toolbar->addAction(QStringLiteral("Wedge"), this, [this] { addPrimitive(FeatureType::Wedge); });
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Union"), this, [this] { applyBoolean(FeatureType::Union); });
    toolbar->addAction(QStringLiteral("Cut"), this, [this] { applyBoolean(FeatureType::Cut); });
    toolbar->addAction(QStringLiteral("Intersect"), this, [this] { applyBoolean(FeatureType::Intersect); });
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Edit..."), this, &Window3D::editSelectedFeature);
    toolbar->addAction(QStringLiteral("List Edges..."), this, &Window3D::listSelectedFeatureEdges);
    toolbar->addAction(QStringLiteral("List Faces..."), this, &Window3D::listSelectedFeatureFaces);
    toolbar->addAction(QStringLiteral("New Sketch..."), this, &Window3D::openSketchEditor);
    toolbar->addAction(QStringLiteral("Add Sketch Feature..."), this, &Window3D::addSketchFeature);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Undo"), this, &Window3D::undo);
    toolbar->addAction(QStringLiteral("Redo"), this, &Window3D::redo);

    m_featureList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_featureList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { editSelectedFeature(); });

    if (m_viewport->isAvailable()) {
        statusBar()->showMessage(QStringLiteral("Add primitives, select two features and apply a boolean op, or "
                                                "double-click a feature to edit its dimensions — left-drag to "
                                                "orbit, wheel to zoom, middle-drag to pan"));
    } else {
        statusBar()->showMessage(QStringLiteral("3D viewport unavailable on this system (no usable display) — "
                                                "the feature tree and kernel still work, just not the preview"));
    }
}

void Window3D::addPrimitive(FeatureType type) {
    Feature3D feature;
    feature.type = type;
    feature.posX = m_nextOffsetX;
    m_nextOffsetX += 30.0;

    switch (type) {
    case FeatureType::Box:
        feature.p1 = feature.p2 = feature.p3 = 20.0;
        break;
    case FeatureType::Cylinder:
        feature.p1 = 10.0;
        feature.p2 = 20.0;
        break;
    case FeatureType::Sphere:
        feature.p1 = 10.0;
        break;
    case FeatureType::Cone:
        feature.p1 = 10.0;
        feature.p2 = 5.0;
        feature.p3 = 20.0;
        break;
    case FeatureType::Torus:
        feature.p1 = 15.0;
        feature.p2 = 5.0;
        break;
    case FeatureType::Wedge:
        feature.p1 = feature.p2 = feature.p3 = 20.0;
        feature.p4 = 10.0; // Ltx: top face narrower than the base by default
        break;
    default:
        break;
    }

    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, feature));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::applyBoolean(FeatureType type) {
    const auto selected = m_featureList->selectionModel()->selectedRows();
    if (selected.size() != 2) {
        statusBar()->showMessage(QStringLiteral("Select exactly two features first"), 3000);
        return;
    }
    Feature3D feature;
    feature.type = type;
    feature.inputA = selected[0].row();
    feature.inputB = selected[1].row();
    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, feature));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::editSelectedFeature() {
    const auto selected = m_featureList->selectionModel()->selectedRows();
    if (selected.size() != 1) {
        statusBar()->showMessage(QStringLiteral("Select exactly one feature to edit"), 3000);
        return;
    }
    const int index = selected[0].row();
    const lcad::Feature3D* existing = m_document.findFeature(index);
    if (!existing || existing->isBoolean()) {
        statusBar()->showMessage(QStringLiteral("Boolean features have no editable dimensions of their own"), 3000);
        return;
    }

    FeatureEditDialog dialog(*existing, this);
    if (dialog.exec() != QDialog::Accepted) return;

    m_document.commandStack().execute(std::make_unique<UpdateFeature3DCommand>(m_document, index, dialog.result()));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::listSelectedFeatureEdges() {
    const auto selected = m_featureList->selectionModel()->selectedRows();
    if (selected.size() != 1) {
        statusBar()->showMessage(QStringLiteral("Select exactly one feature first"), 3000);
        return;
    }
    const int index = selected[0].row();
    if (!m_document.isValid(index)) {
        statusBar()->showMessage(QStringLiteral("That feature isn't valid"), 3000);
        return;
    }

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(m_document.shapeAt(index), TopAbs_EDGE, edgeMap);

    QString report = QStringLiteral("%1 edge(s) -- index: midpoint (x, y, z)\n").arg(edgeMap.Extent());
    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(edgeMap(i));
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(edge, v1, v2);
        if (v1.IsNull() || v2.IsNull()) continue;
        const gp_Pnt p1 = BRep_Tool::Pnt(v1);
        const gp_Pnt p2 = BRep_Tool::Pnt(v2);
        const gp_Pnt mid((p1.X() + p2.X()) / 2.0, (p1.Y() + p2.Y()) / 2.0, (p1.Z() + p2.Z()) / 2.0);
        report += QStringLiteral("%1: (%2, %3, %4)\n").arg(i - 1).arg(mid.X(), 0, 'f', 2).arg(mid.Y(), 0, 'f', 2).arg(mid.Z(), 0, 'f', 2);
    }
    QMessageBox::information(this, QStringLiteral("Feature Edges"), report);
}

void Window3D::listSelectedFeatureFaces() {
    const auto selected = m_featureList->selectionModel()->selectedRows();
    if (selected.size() != 1) {
        statusBar()->showMessage(QStringLiteral("Select exactly one feature first"), 3000);
        return;
    }
    const int index = selected[0].row();
    if (!m_document.isValid(index)) {
        statusBar()->showMessage(QStringLiteral("That feature isn't valid"), 3000);
        return;
    }

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(m_document.shapeAt(index), TopAbs_FACE, faceMap);

    QString report = QStringLiteral("%1 face(s) -- index: centroid (x, y, z)\n").arg(faceMap.Extent());
    for (int i = 1; i <= faceMap.Extent(); ++i) {
        const TopoDS_Face face = TopoDS::Face(faceMap(i));
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        const gp_Pnt centroid = props.CentreOfMass();
        report += QStringLiteral("%1: (%2, %3, %4)\n")
                    .arg(i - 1)
                    .arg(centroid.X(), 0, 'f', 2)
                    .arg(centroid.Y(), 0, 'f', 2)
                    .arg(centroid.Z(), 0, 'f', 2);
    }
    QMessageBox::information(this, QStringLiteral("Feature Faces"), report);
}

void Window3D::openSketchEditor() {
    SketchEditorDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    const int index = m_document.addSketch(std::move(dialog.view()->sketch()));
    const auto& sketch = m_document.sketches()[static_cast<std::size_t>(index)];
    statusBar()->showMessage(QStringLiteral("Sketch %1 saved (%2 point(s), %3 line(s), %4 circle(s)) — "
                                            "Sprint 3 will consume it to drive Pad/Pocket/Revolve")
                                  .arg(index)
                                  .arg(sketch.points().size())
                                  .arg(sketch.lines().size())
                                  .arg(sketch.circles().size()),
                              6000);
    refreshFeatureList();
}

void Window3D::addSketchFeature() {
    SketchFeatureDialog dialog(m_document, this);
    if (dialog.exec() != QDialog::Accepted) return;

    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, dialog.result()));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::undo() {
    m_document.commandStack().undo();
    refreshFeatureList();
    refreshViewport();
}

void Window3D::redo() {
    m_document.commandStack().redo();
    refreshFeatureList();
    refreshViewport();
}

void Window3D::importStepFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import STEP"), QString(),
                                                        QStringLiteral("STEP Files (*.step *.stp)"));
    if (path.isEmpty()) return;
    const TopoDS_Shape shape = lcad::readStep(path.toStdString());
    if (shape.IsNull()) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"), QStringLiteral("Could not read that STEP file."));
        return;
    }
    const int importIdx = m_document.addImportedShape(shape);
    Feature3D feature;
    feature.type = FeatureType::Imported;
    feature.name = QFileInfo(path).baseName().toStdString();
    feature.importIndex = importIdx;
    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, feature));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::importIgesFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import IGES"), QString(),
                                                        QStringLiteral("IGES Files (*.igs *.iges)"));
    if (path.isEmpty()) return;
    const TopoDS_Shape shape = lcad::readIges(path.toStdString());
    if (shape.IsNull()) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"), QStringLiteral("Could not read that IGES file."));
        return;
    }
    const int importIdx = m_document.addImportedShape(shape);
    Feature3D feature;
    feature.type = FeatureType::Imported;
    feature.name = QFileInfo(path).baseName().toStdString();
    feature.importIndex = importIdx;
    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, feature));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::exportStepFile() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export STEP"), QString(),
                                                        QStringLiteral("STEP Files (*.step)"));
    if (path.isEmpty()) return;
    if (!lcad::writeStep(m_document, path.toStdString())) {
        statusBar()->showMessage(QStringLiteral("STEP export failed -- no valid tip solid in the document"), 4000);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported STEP to %1").arg(path), 3000);
}

void Window3D::exportIgesFile() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export IGES"), QString(),
                                                        QStringLiteral("IGES Files (*.igs)"));
    if (path.isEmpty()) return;
    if (!lcad::writeIges(m_document, path.toStdString())) {
        statusBar()->showMessage(QStringLiteral("IGES export failed -- no valid tip solid in the document"), 4000);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported IGES to %1").arg(path), 3000);
}

void Window3D::saveKcad3dFile() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save .kcad3d"), QString(),
                                                        QStringLiteral("KumCAD 3D Documents (*.kcad3d)"));
    if (path.isEmpty()) return;
    if (!lcad::saveDocument3D(m_document, path.toStdString())) {
        statusBar()->showMessage(QStringLiteral("Save failed"), 4000);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Saved to %1").arg(path), 3000);
}

void Window3D::openKcad3dFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open .kcad3d"), QString(),
                                                        QStringLiteral("KumCAD 3D Documents (*.kcad3d)"));
    if (path.isEmpty()) return;

    // Opens into a brand-new window rather than replacing this one --
    // loadDocument3D's contract is "load into a freshly-constructed
    // Document3D", and a new Window3D's document is exactly that, so this
    // sidesteps ever needing to reset/clear an in-use Document3D (which
    // isn't supported -- see Persistence3D.h's own disclosure).
    auto* window = new Window3D(nullptr);
    window->setAttribute(Qt::WA_DeleteOnClose);
    if (!lcad::loadDocument3D(window->m_document, path.toStdString())) {
        QMessageBox::warning(this, QStringLiteral("Open Failed"), QStringLiteral("Could not read that .kcad3d file."));
        delete window;
        return;
    }
    window->setWindowTitle(QStringLiteral("KumCAD — 3D Modeling — %1").arg(QFileInfo(path).fileName()));
    window->refreshFeatureList();
    window->refreshViewport();
    window->show();
}

void Window3D::openAssemblyWindow() {
    auto* window = new AssemblyWindow(nullptr);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void Window3D::generateDrawingViews() {
    // Same "tip feature" definition as StepIges.h: a valid feature nothing
    // else consumes as its own input.
    std::vector<bool> consumed(m_document.features().size(), false);
    for (const auto& f : m_document.features()) {
        if (f.inputA >= 0) consumed[static_cast<std::size_t>(f.inputA)] = true;
        if (f.inputB >= 0) consumed[static_cast<std::size_t>(f.inputB)] = true;
    }
    std::vector<TopoDS_Shape> tips;
    for (int i = 0; i < static_cast<int>(m_document.features().size()); ++i) {
        if (!consumed[static_cast<std::size_t>(i)] && m_document.isValid(i)) tips.push_back(m_document.shapeAt(i));
    }
    const lcad::BimShapes bimShapes = lcad::buildBimShapes(m_bimModel);
    for (const auto* shapes : {&bimShapes.wallShapes, &bimShapes.slabShapes, &bimShapes.columnShapes, &bimShapes.beamShapes}) {
        for (const TopoDS_Shape& shape : *shapes) {
            if (!shape.IsNull()) tips.push_back(shape);
        }
    }
    if (tips.empty()) {
        statusBar()->showMessage(QStringLiteral("No solids to draw yet"), 3000);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Generate Drawing Views"), QString(),
                                                        QStringLiteral("DXF Files (*.dxf)"));
    if (path.isEmpty()) return;

    Bnd_Box bounds;
    for (const TopoDS_Shape& shape : tips) BRepBndLib::Add(shape, bounds);
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double spacing = std::max({xmax - xmin, ymax - ymin, zmax - zmin, 10.0}) * 1.5;

    lcad::Document doc2d;
    struct ViewSpec {
        lcad::ViewDirection direction;
        double offsetX;
        double offsetY;
    };
    const ViewSpec specs[] = {
        {lcad::ViewDirection::Front, 0.0, 0.0},
        {lcad::ViewDirection::Top, 0.0, -spacing},
        {lcad::ViewDirection::Right, spacing, 0.0},
        {lcad::ViewDirection::Iso, spacing, -spacing},
    };
    for (const ViewSpec& spec : specs) {
        for (const TopoDS_Shape& shape : tips) {
            const lcad::TechDrawView view = lcad::projectView(shape, spec.direction);
            lcad::insertViewIntoDocument(doc2d, view, spec.offsetX, spec.offsetY);
            // Auto-dimension every orthographic view (not Iso -- see
            // autoDimensionView's own comment on why a true isometric
            // projection's lengths aren't the model's real lengths).
            if (spec.direction != lcad::ViewDirection::Iso) {
                lcad::AutoDimensionOptions dimOptions;
                dimOptions.offsetX = spec.offsetX;
                dimOptions.offsetY = spec.offsetY;
                dimOptions.dimensionEachAxisAlignedEdge = true;
                lcad::autoDimensionView(doc2d, view, dimOptions);
            }
        }
    }

    std::string error;
    if (!lcad::writeDxf(doc2d, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QString::fromStdString(error));
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("Drawing views (Front/Top/Right/Iso), auto-dimensioned, written to %1").arg(path), 4000);
}

void Window3D::addSheetMetalPart() {
    SheetMetalDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    const lcad::SheetMetalPart part = dialog.result();
    const TopoDS_Shape shape = lcad::buildSheetMetalSolid(part);
    if (shape.IsNull()) {
        QMessageBox::warning(this, QStringLiteral("Invalid Part"),
                              QStringLiteral("That combination of lengths/angles/thickness didn't produce a valid "
                                            "solid -- check flat-length count is one more than bend-angle count, "
                                            "and that no bend angle reaches 180 degrees."));
        return;
    }

    m_lastSheetMetalPart = part;
    m_hasSheetMetalPart = true;

    const int importIdx = m_document.addImportedShape(shape);
    Feature3D feature;
    feature.type = FeatureType::Imported;
    feature.name = "SheetMetal";
    feature.importIndex = importIdx;
    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, feature));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::exportFlatPattern() {
    if (!m_hasSheetMetalPart) {
        statusBar()->showMessage(QStringLiteral("Add a sheet metal part first"), 3000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Flat Pattern"), QString(),
                                                        QStringLiteral("DXF Files (*.dxf)"));
    if (path.isEmpty()) return;

    lcad::Document doc2d;
    lcad::insertFlatPatternIntoDocument(doc2d, m_lastSheetMetalPart, 0.0, 0.0);

    std::string error;
    if (!lcad::writeDxf(doc2d, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QString::fromStdString(error));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Flat pattern written to %1").arg(path), 3000);
}

void Window3D::addFaceFlange() {
    const auto selected = m_featureList->selectionModel()->selectedRows();
    if (selected.size() != 1) {
        statusBar()->showMessage(QStringLiteral("Select exactly one feature to flange"), 3000);
        return;
    }
    const int index = selected[0].row();
    if (!m_document.isValid(index)) {
        statusBar()->showMessage(QStringLiteral("That feature isn't valid"), 3000);
        return;
    }
    const TopoDS_Shape& target = m_document.shapeAt(index);

    FaceFlangeDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    const auto edgePick = lcad::pickEdge(target, dialog.edgeRay(), dialog.thickness() * 5.0);
    if (!edgePick) {
        QMessageBox::warning(this, QStringLiteral("Pick Failed"), QStringLiteral("Edge-pick ray didn't find an edge."));
        return;
    }
    const auto facePick = lcad::pickFace(target, dialog.faceRay());
    if (!facePick) {
        QMessageBox::warning(this, QStringLiteral("Pick Failed"), QStringLiteral("Face-pick ray didn't hit a face."));
        return;
    }

    const TopoDS_Shape flanged = lcad::buildFaceFlange(target, edgePick->edgeIndex, facePick->faceIndex,
                                                       dialog.length(), dialog.bendAngle(), dialog.thickness());
    if (flanged.IsNull()) {
        QMessageBox::warning(this, QStringLiteral("Flange Failed"),
                              QStringLiteral("Could not build a flange from that edge/face pair -- try different "
                                            "pick rays."));
        return;
    }

    const int importIdx = m_document.addImportedShape(flanged);
    Feature3D feature;
    feature.type = FeatureType::Imported;
    feature.name = "Flanged";
    feature.importIndex = importIdx;
    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, feature));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::addBimWall() {
    WallDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_bimModel.walls.push_back(dialog.result());
    refreshViewport();
    statusBar()->showMessage(QStringLiteral("Wall %1 added").arg(m_bimModel.walls.size() - 1), 2000);
}

void Window3D::addBimOpening() {
    if (m_bimModel.walls.empty()) {
        statusBar()->showMessage(QStringLiteral("Add a wall first"), 3000);
        return;
    }
    OpeningDialog dialog(static_cast<int>(m_bimModel.walls.size()), this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_bimModel.openings.push_back(dialog.result());
    refreshViewport();
}

void Window3D::addBimSlab() {
    SlabDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const lcad::Slab slab = dialog.result();
    if (slab.boundary.size() < 3) {
        statusBar()->showMessage(QStringLiteral("A slab needs at least 3 boundary points"), 3000);
        return;
    }
    m_bimModel.slabs.push_back(slab);
    refreshViewport();
}

void Window3D::addBimColumn() {
    ColumnDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_bimModel.columns.push_back(dialog.result());
    refreshViewport();
    statusBar()->showMessage(QStringLiteral("Column %1 added").arg(m_bimModel.columns.size() - 1), 2000);
}

void Window3D::addBimBeam() {
    BeamDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_bimModel.beams.push_back(dialog.result());
    refreshViewport();
    statusBar()->showMessage(QStringLiteral("Beam %1 added").arg(m_bimModel.beams.size() - 1), 2000);
}

void Window3D::addBimSpace() {
    SpaceDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const lcad::Space space = dialog.result();
    if (space.boundary.size() < 3) {
        statusBar()->showMessage(QStringLiteral("A room/space needs at least 3 boundary points"), 3000);
        return;
    }
    m_bimModel.spaces.push_back(space);
    statusBar()->showMessage(QStringLiteral("Room/space \"%1\" added (not drawn in 3D -- see Bim.h's Space comment)")
                                  .arg(QString::fromStdString(space.name)),
                              3000);
}

void Window3D::importIfcLite() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import IFC-lite"), QString(),
                                                        QStringLiteral("IFC-lite Files (*.ifc)"));
    if (path.isEmpty()) return;
    lcad::BimModel loaded;
    if (!lcad::readIfcLite(loaded, path.toStdString())) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"), QStringLiteral("Could not read that file."));
        return;
    }
    m_bimModel = loaded;
    refreshViewport();
    statusBar()->showMessage(QStringLiteral("Loaded %1 wall(s), %2 opening(s), %3 slab(s), %4 column(s), %5 beam(s), "
                                            "%6 space(s)")
                                  .arg(m_bimModel.walls.size())
                                  .arg(m_bimModel.openings.size())
                                  .arg(m_bimModel.slabs.size())
                                  .arg(m_bimModel.columns.size())
                                  .arg(m_bimModel.beams.size())
                                  .arg(m_bimModel.spaces.size()),
                              4000);
}

void Window3D::exportIfcLite() {
    if (m_bimModel.walls.empty() && m_bimModel.slabs.empty() && m_bimModel.columns.empty() && m_bimModel.beams.empty() &&
        m_bimModel.spaces.empty()) {
        statusBar()->showMessage(QStringLiteral("Add a wall, slab, column, beam, or room/space first"), 3000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export IFC-lite"), QString(),
                                                        QStringLiteral("IFC-lite Files (*.ifc)"));
    if (path.isEmpty()) return;
    if (!lcad::writeIfcLite(m_bimModel, path.toStdString())) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QStringLiteral("Could not write that file."));
        return;
    }
    statusBar()->showMessage(QStringLiteral("IFC-lite written to %1 (this codebase's own subset format, not "
                                            "real IFC -- see Bim.h)")
                                  .arg(path),
                              5000);
}

void Window3D::exportOpeningSchedule() {
    if (m_bimModel.openings.empty()) {
        statusBar()->showMessage(QStringLiteral("Add a door or window first"), 3000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Opening Schedule"), QString(),
                                                        QStringLiteral("DXF Files (*.dxf)"));
    if (path.isEmpty()) return;

    lcad::Document doc2d;
    lcad::buildOpeningScheduleTable(doc2d, m_bimModel, lcad::Point2D(0.0, 0.0));

    std::string error;
    if (!lcad::writeDxf(doc2d, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QString::fromStdString(error));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Opening schedule written to %1").arg(path), 3000);
}

void Window3D::exportRoomSchedule() {
    if (m_bimModel.spaces.empty()) {
        statusBar()->showMessage(QStringLiteral("Add a room/space first"), 3000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Room Schedule"), QString(),
                                                        QStringLiteral("DXF Files (*.dxf)"));
    if (path.isEmpty()) return;

    lcad::Document doc2d;
    lcad::buildRoomScheduleTable(doc2d, m_bimModel, lcad::Point2D(0.0, 0.0));

    std::string error;
    if (!lcad::writeDxf(doc2d, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QString::fromStdString(error));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Room schedule written to %1").arg(path), 3000);
}

void Window3D::generate3DCamToolpath() {
    // Same "tip feature" definition as StepIges.h/generateDrawingViews.
    std::vector<bool> consumed(m_document.features().size(), false);
    for (const auto& f : m_document.features()) {
        if (f.inputA >= 0) consumed[static_cast<std::size_t>(f.inputA)] = true;
        if (f.inputB >= 0) consumed[static_cast<std::size_t>(f.inputB)] = true;
    }
    TopoDS_Shape target;
    int tipCount = 0;
    for (int i = 0; i < static_cast<int>(m_document.features().size()); ++i) {
        if (consumed[static_cast<std::size_t>(i)] || !m_document.isValid(i)) continue;
        ++tipCount;
        if (target.IsNull()) target = m_document.shapeAt(i);
    }
    if (target.IsNull()) {
        statusBar()->showMessage(QStringLiteral("No solids to machine yet"), 3000);
        return;
    }

    Cam3DDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const lcad::Cam3DParams params = dialog.result();

    const std::vector<lcad::Cam3DLevel> levels = lcad::sliceIntoLevels(target, params);
    if (levels.empty()) {
        QMessageBox::warning(this, QStringLiteral("No Toolpath"),
                              QStringLiteral("Slicing produced no usable levels -- check step-down and tool "
                                            "diameter against the part's size."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Generate 3D CAM Toolpath"), QString(),
                                                        QStringLiteral("G-Code Files (*.gcode *.nc)"));
    if (path.isEmpty()) return;

    std::string error;
    if (!lcad::writeMultiLevelGCode(levels, params, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QString::fromStdString(error));
        return;
    }
    QString message = QStringLiteral("%1 level(s) written to %2").arg(levels.size()).arg(path);
    if (tipCount > 1) message += QStringLiteral(" (only the first of %1 solids was machined)").arg(tipCount);
    statusBar()->showMessage(message, 5000);
}

void Window3D::runFemAnalysis() {
    std::vector<bool> consumed(m_document.features().size(), false);
    for (const auto& f : m_document.features()) {
        if (f.inputA >= 0) consumed[static_cast<std::size_t>(f.inputA)] = true;
        if (f.inputB >= 0) consumed[static_cast<std::size_t>(f.inputB)] = true;
    }
    TopoDS_Shape target;
    for (int i = 0; i < static_cast<int>(m_document.features().size()); ++i) {
        if (consumed[static_cast<std::size_t>(i)] || !m_document.isValid(i)) continue;
        target = m_document.shapeAt(i);
        break;
    }
    if (target.IsNull()) {
        statusBar()->showMessage(QStringLiteral("No solids to analyze yet"), 3000);
        return;
    }

    FemDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    const lcad::FemMesh mesh = lcad::buildVoxelMesh(target, dialog.divisions());
    if (mesh.tets.empty()) {
        QMessageBox::warning(this, QStringLiteral("Meshing Failed"),
                              QStringLiteral("No tetrahedra were generated -- check the mesh divisions against "
                                            "the part's size."));
        return;
    }

    const lcad::FemResult result =
        lcad::solveLinearStatic(mesh, dialog.material(), dialog.boundaryCondition(), dialog.loads());
    if (!result.solved) {
        QMessageBox::warning(this, QStringLiteral("Analysis Failed"),
                              QStringLiteral("The system could not be solved -- check that enough nodes are "
                                            "fixed (an unconstrained model has rigid-body freedom)."));
        return;
    }

    double maxDisplacement = 0.0;
    for (const auto& d : result.displacements) {
        maxDisplacement = std::max(maxDisplacement, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    }
    double maxStress = 0.0;
    for (double vm : result.vonMisesStress) maxStress = std::max(maxStress, vm);

    // Auto-exaggerate displacement for visibility: real structural
    // displacements are usually tiny relative to the part itself, so a
    // 1:1 scale would show no visible deformation at all -- scale so the
    // single largest displacement maps to ~10% of the part's own bounding
    // diagonal, a common convention real FEM viewers use too.
    if (m_viewport->isAvailable() && maxDisplacement > 1e-12) {
        Bnd_Box bounds;
        BRepBndLib::Add(target, bounds);
        double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double diagonal = std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin) +
                                          (zmax - zmin) * (zmax - zmin));
        const double displacementScale = diagonal > 1e-9 ? (diagonal * 0.10) / maxDisplacement : 1.0;

        const lcad::FemVisualization viz = lcad::buildFemVisualization(mesh, result, displacementScale);
        m_viewport->clearShapes();
        for (std::size_t i = 0; i < viz.elementShapes.size(); ++i) {
            if (viz.elementShapes[i].IsNull()) continue;
            const auto& color = viz.elementColors[i];
            m_viewport->displayShape(viz.elementShapes[i], color[0], color[1], color[2]);
        }
        m_viewport->fitAll();
    }

    QMessageBox::information(this, QStringLiteral("FEM Analysis Complete"),
                              QStringLiteral("Mesh: %1 nodes, %2 tetrahedra\nMax displacement magnitude: %3\n"
                                            "Max von Mises stress: %4\n\n"
                                            "Displayed in the viewport as a blue (low stress) to red (high "
                                            "stress) heatmap, deformation exaggerated for visibility -- replaces "
                                            "the feature tree's own shapes until you edit/add a feature again.")
                                  .arg(mesh.nodes.size())
                                  .arg(mesh.tets.size())
                                  .arg(maxDisplacement)
                                  .arg(maxStress));
}

void Window3D::runFemModalAnalysis() {
    std::vector<bool> consumed(m_document.features().size(), false);
    for (const auto& f : m_document.features()) {
        if (f.inputA >= 0) consumed[static_cast<std::size_t>(f.inputA)] = true;
        if (f.inputB >= 0) consumed[static_cast<std::size_t>(f.inputB)] = true;
    }
    TopoDS_Shape target;
    for (int i = 0; i < static_cast<int>(m_document.features().size()); ++i) {
        if (consumed[static_cast<std::size_t>(i)] || !m_document.isValid(i)) continue;
        target = m_document.shapeAt(i);
        break;
    }
    if (target.IsNull()) {
        statusBar()->showMessage(QStringLiteral("No solids to analyze yet"), 3000);
        return;
    }

    FemModalDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    const lcad::FemMesh mesh = lcad::buildVoxelMesh(target, dialog.divisions());
    if (mesh.tets.empty()) {
        QMessageBox::warning(this, QStringLiteral("Meshing Failed"),
                              QStringLiteral("No tetrahedra were generated -- check the mesh divisions against "
                                            "the part's size."));
        return;
    }

    const lcad::FemModalResult result = lcad::solveModal(mesh, dialog.material(), dialog.boundaryCondition());
    if (!result.solved) {
        QMessageBox::warning(this, QStringLiteral("Analysis Failed"),
                              QStringLiteral("The mode could not be solved -- check that enough nodes are fixed "
                                            "(an unconstrained model has rigid-body freedom)."));
        return;
    }

    const double omega = std::sqrt(std::max(0.0, result.angularFrequencySquared));
    const double frequencyHz = omega / (2.0 * M_PI);

    double maxAmplitude = 0.0;
    for (const auto& d : result.modeShape) {
        maxAmplitude = std::max(maxAmplitude, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    }

    if (m_viewport->isAvailable() && maxAmplitude > 1e-12) {
        Bnd_Box bounds;
        BRepBndLib::Add(target, bounds);
        double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
        bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double diagonal = std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin) +
                                          (zmax - zmin) * (zmax - zmin));
        const double displacementScale = diagonal > 1e-9 ? (diagonal * 0.10) / maxAmplitude : 1.0;

        // Wrap the mode shape into a plain FemResult so buildFemVisualization
        // (written for a static result) can be reused as-is -- a uniform
        // color per element, since a mode shape carries no stress of its own.
        lcad::FemResult asStaticResult;
        asStaticResult.solved = true;
        asStaticResult.displacements = result.modeShape;
        asStaticResult.vonMisesStress.assign(mesh.tets.size(), 0.0);

        const lcad::FemVisualization viz = lcad::buildFemVisualization(mesh, asStaticResult, displacementScale);
        m_viewport->clearShapes();
        for (std::size_t i = 0; i < viz.elementShapes.size(); ++i) {
            if (viz.elementShapes[i].IsNull()) continue;
            const auto& color = viz.elementColors[i];
            m_viewport->displayShape(viz.elementShapes[i], color[0], color[1], color[2]);
        }
        m_viewport->fitAll();
    }

    QMessageBox::information(
        this, QStringLiteral("FEM Modal Analysis Complete"),
        QStringLiteral("Mesh: %1 nodes, %2 tetrahedra\nFundamental angular frequency^2 (omega^2): %3\n"
                      "Fundamental frequency: %4 (in consistent force/length/mass units, not necessarily Hz "
                      "unless your inputs already are)\n\n"
                      "Displayed in the viewport as the exaggerated mode shape (not a stress heatmap) -- "
                      "replaces the feature tree's own shapes until you edit/add a feature again.")
            .arg(mesh.nodes.size())
            .arg(mesh.tets.size())
            .arg(result.angularFrequencySquared)
            .arg(frequencyHz));
}

void Window3D::runFemThermalAnalysis() {
    std::vector<bool> consumed(m_document.features().size(), false);
    for (const auto& f : m_document.features()) {
        if (f.inputA >= 0) consumed[static_cast<std::size_t>(f.inputA)] = true;
        if (f.inputB >= 0) consumed[static_cast<std::size_t>(f.inputB)] = true;
    }
    TopoDS_Shape target;
    for (int i = 0; i < static_cast<int>(m_document.features().size()); ++i) {
        if (consumed[static_cast<std::size_t>(i)] || !m_document.isValid(i)) continue;
        target = m_document.shapeAt(i);
        break;
    }
    if (target.IsNull()) {
        statusBar()->showMessage(QStringLiteral("No solids to analyze yet"), 3000);
        return;
    }

    FemThermalDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    const lcad::FemMesh mesh = lcad::buildVoxelMesh(target, dialog.divisions());
    if (mesh.tets.empty()) {
        QMessageBox::warning(this, QStringLiteral("Meshing Failed"),
                              QStringLiteral("No tetrahedra were generated -- check the mesh divisions against "
                                            "the part's size."));
        return;
    }

    const lcad::FemThermalResult result =
        lcad::solveThermalSteadyState(mesh, dialog.material(), dialog.boundaryCondition(), dialog.loads());
    if (!result.solved) {
        QMessageBox::warning(this, QStringLiteral("Analysis Failed"),
                              QStringLiteral("The system could not be solved -- check that some part of the model "
                                            "actually has a fixed temperature."));
        return;
    }

    double minTemp = result.temperatures.empty() ? 0.0 : result.temperatures[0];
    double maxTemp = minTemp;
    for (double t : result.temperatures) {
        minTemp = std::min(minTemp, t);
        maxTemp = std::max(maxTemp, t);
    }

    if (m_viewport->isAvailable()) {
        // Wrap the temperature field into a plain FemResult so
        // buildFemVisualization (written for stress) can be reused as-is
        // -- no deformation (temperature alone doesn't displace anything
        // in this model), and the blue-to-red heatmap becomes cold-to-hot
        // instead of low-to-high stress.
        lcad::FemResult asStaticResult;
        asStaticResult.solved = true;
        asStaticResult.displacements.assign(mesh.nodes.size(), {0.0, 0.0, 0.0});
        asStaticResult.vonMisesStress.resize(mesh.tets.size());
        for (std::size_t t = 0; t < mesh.tets.size(); ++t) {
            double avg = 0.0;
            for (int node : mesh.tets[t]) avg += result.temperatures[static_cast<std::size_t>(node)];
            asStaticResult.vonMisesStress[t] = (avg / 4.0) - minTemp; // buildFemVisualization normalizes by its own max
        }

        const lcad::FemVisualization viz = lcad::buildFemVisualization(mesh, asStaticResult, 1.0);
        m_viewport->clearShapes();
        for (std::size_t i = 0; i < viz.elementShapes.size(); ++i) {
            if (viz.elementShapes[i].IsNull()) continue;
            const auto& color = viz.elementColors[i];
            m_viewport->displayShape(viz.elementShapes[i], color[0], color[1], color[2]);
        }
        m_viewport->fitAll();
    }

    QMessageBox::information(this, QStringLiteral("FEM Thermal Analysis Complete"),
                              QStringLiteral("Mesh: %1 nodes, %2 tetrahedra\nMin temperature: %3\nMax "
                                            "temperature: %4\n\nDisplayed in the viewport as a blue (cold) to "
                                            "red (hot) heatmap -- replaces the feature tree's own shapes until "
                                            "you edit/add a feature again.")
                                  .arg(mesh.nodes.size())
                                  .arg(mesh.tets.size())
                                  .arg(minTemp)
                                  .arg(maxTemp));
}

void Window3D::addPipeRun() {
    PipeRunDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    const lcad::PipeRun run = dialog.result();
    const TopoDS_Shape shape = lcad::buildPipeShape(run);
    if (shape.IsNull()) {
        QMessageBox::warning(this, QStringLiteral("Invalid Pipe Run"),
                              QStringLiteral("Needs at least 2 path points and a positive radius."));
        return;
    }

    const int importIdx = m_document.addImportedShape(shape);
    Feature3D feature;
    feature.type = FeatureType::Imported;
    feature.name = "PipeRun";
    feature.importIndex = importIdx;
    m_document.commandStack().execute(std::make_unique<AddFeature3DCommand>(m_document, feature));
    refreshFeatureList();
    refreshViewport();
}

void Window3D::refreshFeatureList() {
    m_featureList->clear();
    for (int i = 0; i < static_cast<int>(m_document.features().size()); ++i) {
        const auto& f = m_document.features()[static_cast<std::size_t>(i)];
        QString text = QStringLiteral("[%1] %2").arg(i).arg(typeName(f.type));
        if (f.isBoolean()) text += QStringLiteral(" (%1, %2)").arg(f.inputA).arg(f.inputB);
        if (!m_document.isValid(i)) text += QStringLiteral(" — invalid");
        m_featureList->addItem(text);
    }
    for (std::size_t i = 0; i < m_document.sketches().size(); ++i) {
        const auto& sketch = m_document.sketches()[i];
        auto* item = new QListWidgetItem(QStringLiteral("Sketch %1 (%2 pts, %3 lines, %4 circles)")
                                             .arg(i)
                                             .arg(sketch.points().size())
                                             .arg(sketch.lines().size())
                                             .arg(sketch.circles().size()));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable); // not a feature; not part of add-primitive/boolean selection
        m_featureList->addItem(item);
    }
}

void Window3D::refreshViewport() {
    if (!m_viewport->isAvailable()) return;

    // Only "leaf" features (nothing consumes them as a boolean input) are
    // shown, matching how a real parametric CAD viewport hides a feature
    // once something downstream has used it.
    std::vector<bool> consumed(m_document.features().size(), false);
    for (const auto& f : m_document.features()) {
        if (f.inputA >= 0) consumed[static_cast<std::size_t>(f.inputA)] = true;
        if (f.inputB >= 0) consumed[static_cast<std::size_t>(f.inputB)] = true;
    }

    m_viewport->clearShapes();
    for (int i = 0; i < static_cast<int>(m_document.features().size()); ++i) {
        if (consumed[static_cast<std::size_t>(i)] || !m_document.isValid(i)) continue;
        m_viewport->displayShape(m_document.shapeAt(i));
    }

    // The BIM model lives alongside the feature tree (see Window3D.h), so
    // its shapes are drawn into the same viewport here rather than through
    // a separate refresh path.
    const lcad::BimShapes bimShapes = lcad::buildBimShapes(m_bimModel);
    for (const auto* shapes : {&bimShapes.wallShapes, &bimShapes.slabShapes, &bimShapes.columnShapes, &bimShapes.beamShapes}) {
        for (const TopoDS_Shape& shape : *shapes) {
            if (!shape.IsNull()) m_viewport->displayShape(shape);
        }
    }

    m_viewport->fitAll();
}
