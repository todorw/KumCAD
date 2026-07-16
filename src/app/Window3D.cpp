#include "Window3D.h"
#include "AssemblyWindow.h"
#include "SketchEditorDialog.h"
#include "SketchFeatureDialog.h"
#include "SketchView.h"
#include "Viewport3D.h"

#include "core/core3d/Commands3D.h"
#include "core/core3d/Persistence3D.h"
#include "core/core3d/StepIges.h"
#include "core/core3d/TechDraw.h"
#include "core/document/Document.h"
#include "core/io/DxfWriter.h"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

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
        return f;
    }

private:
    Feature3D m_result;
    std::vector<QDoubleSpinBox*> m_paramSpins;
    std::vector<QDoubleSpinBox*> m_posSpins;
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
        }
    }

    std::string error;
    if (!lcad::writeDxf(doc2d, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QString::fromStdString(error));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Drawing views (Front/Top/Right/Iso) written to %1").arg(path), 4000);
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
    m_viewport->fitAll();
}
