#include "AssemblyWindow.h"
#include "Viewport3D.h"

#include "core/core3d/Pick3D.h"
#include "core/core3d/StepIges.h"

#include <BRepBuilderAPI_Transform.hxx>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>

using lcad::AssemblyComponent;
using lcad::Mate;
using lcad::MateType;

namespace {

QString mateTypeName(MateType type) {
    switch (type) {
    case MateType::Coincident: return QStringLiteral("Coincident");
    case MateType::Concentric: return QStringLiteral("Concentric");
    case MateType::Distance: return QStringLiteral("Distance");
    case MateType::Angle: return QStringLiteral("Angle");
    }
    return QStringLiteral("Mate");
}

// A ray (origin + direction) to pick a face with, since there's no live
// interactive viewport picking wired up yet (mouse-to-ray conversion in
// Viewport3D is the same unverified-in-this-environment territory the
// rest of the viewport carries) -- this is the same "typed-in, not
// live-clicked" precedent Window3D's own "List Edges..." action set for
// Fillet/Chamfer edge selection. A face's exact point+normal is still
// real, picked geometry (see Pick3D.h) once the ray is given; only the
// ray's own origin/direction has to be estimated by the user.
class PickRayDialog : public QDialog {
public:
    explicit PickRayDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(QStringLiteral("Pick Face (by ray)"));
        auto* form = new QFormLayout(this);

        m_ox = makeSpin(0.0); m_oy = makeSpin(0.0); m_oz = makeSpin(100.0);
        form->addRow(QStringLiteral("Ray Origin X/Y/Z:"), rowOf(this, {m_ox, m_oy, m_oz}));
        m_dx = makeSpin(0.0); m_dy = makeSpin(0.0); m_dz = makeSpin(-1.0);
        form->addRow(QStringLiteral("Ray Direction X/Y/Z:"), rowOf(this, {m_dx, m_dy, m_dz}));

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    lcad::PickRay ray() const {
        lcad::PickRay r;
        r.origin = {m_ox->value(), m_oy->value(), m_oz->value()};
        r.direction = {m_dx->value(), m_dy->value(), m_dz->value()};
        return r;
    }

private:
    static QDoubleSpinBox* makeSpin(double value) {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(-1e6, 1e6);
        spin->setDecimals(3);
        spin->setValue(value);
        return spin;
    }
    static QWidget* rowOf(QWidget* parent, std::initializer_list<QWidget*> widgets) {
        auto* container = new QWidget(parent);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        for (QWidget* w : widgets) layout->addWidget(w);
        return container;
    }

    QDoubleSpinBox *m_ox, *m_oy, *m_oz, *m_dx, *m_dy, *m_dz;
};

// A mate's reference point/direction on each component can be typed in
// directly, or resolved from a real face pick (see PickRayDialog above and
// Pick3D.h) -- picking happens against the component's own LOCAL-frame
// shape (matching Mate's own "point/direction in ITS OWN local frame"
// contract), not its current world placement.
class AddMateDialog : public QDialog {
public:
    AddMateDialog(const std::vector<AssemblyComponent>& components, QWidget* parent = nullptr)
        : QDialog(parent), m_components(components) {
        setWindowTitle(QStringLiteral("Add Mate"));
        auto* form = new QFormLayout(this);

        m_typeCombo = new QComboBox(this);
        for (MateType type : {MateType::Coincident, MateType::Concentric, MateType::Distance, MateType::Angle}) {
            m_typeCombo->addItem(mateTypeName(type), static_cast<int>(type));
        }
        form->addRow(QStringLiteral("Type:"), m_typeCombo);

        m_compA = new QComboBox(this);
        m_compB = new QComboBox(this);
        for (std::size_t i = 0; i < components.size(); ++i) {
            const QString label = QStringLiteral("[%1] %2").arg(i).arg(QString::fromStdString(components[i].name));
            m_compA->addItem(label, static_cast<int>(i));
            m_compB->addItem(label, static_cast<int>(i));
        }
        if (components.size() > 1) m_compB->setCurrentIndex(1);
        form->addRow(QStringLiteral("Component A (already placed):"), m_compA);
        form->addRow(QStringLiteral("Component B (gets placed):"), m_compB);

        m_ax = makeSpin(0.0); m_ay = makeSpin(0.0); m_az = makeSpin(0.0);
        form->addRow(QStringLiteral("A point X/Y/Z:"), rowOf({m_ax, m_ay, m_az}));
        m_adx = makeSpin(0.0); m_ady = makeSpin(0.0); m_adz = makeSpin(1.0);
        form->addRow(QStringLiteral("A direction X/Y/Z:"), rowOf({m_adx, m_ady, m_adz}));
        auto* pickA = new QPushButton(QStringLiteral("Pick Face on A..."), this);
        connect(pickA, &QPushButton::clicked, this, [this] { pickFaceInto(m_compA, m_ax, m_ay, m_az, m_adx, m_ady, m_adz); });
        form->addRow(QString(), pickA);

        m_bx = makeSpin(0.0); m_by = makeSpin(0.0); m_bz = makeSpin(0.0);
        form->addRow(QStringLiteral("B point X/Y/Z:"), rowOf({m_bx, m_by, m_bz}));
        m_bdx = makeSpin(0.0); m_bdy = makeSpin(0.0); m_bdz = makeSpin(1.0);
        form->addRow(QStringLiteral("B direction X/Y/Z:"), rowOf({m_bdx, m_bdy, m_bdz}));
        auto* pickB = new QPushButton(QStringLiteral("Pick Face on B..."), this);
        connect(pickB, &QPushButton::clicked, this, [this] { pickFaceInto(m_compB, m_bx, m_by, m_bz, m_bdx, m_bdy, m_bdz); });
        form->addRow(QString(), pickB);

        m_value = makeSpin(0.0);
        form->addRow(QStringLiteral("Distance offset / Angle degrees:"), m_value);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    Mate result() const {
        Mate m;
        m.type = static_cast<MateType>(m_typeCombo->currentData().toInt());
        m.componentA = m_compA->currentData().toInt();
        m.componentB = m_compB->currentData().toInt();
        m.ax = m_ax->value(); m.ay = m_ay->value(); m.az = m_az->value();
        m.adx = m_adx->value(); m.ady = m_ady->value(); m.adz = m_adz->value();
        m.bx = m_bx->value(); m.by = m_by->value(); m.bz = m_bz->value();
        m.bdx = m_bdx->value(); m.bdy = m_bdy->value(); m.bdz = m_bdz->value();
        m.value = m_value->value();
        return m;
    }

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

    // Runs PickRayDialog against componentCombo's currently-selected
    // component's own (local-frame) shape, and on a real hit fills the
    // given point/direction spinboxes with the picked face's exact
    // point/outward normal.
    void pickFaceInto(QComboBox* componentCombo, QDoubleSpinBox* px, QDoubleSpinBox* py, QDoubleSpinBox* pz,
                      QDoubleSpinBox* dx, QDoubleSpinBox* dy, QDoubleSpinBox* dz) {
        const int compIndex = componentCombo->currentData().toInt();
        if (compIndex < 0 || compIndex >= static_cast<int>(m_components.size())) return;

        PickRayDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) return;

        const auto hit = lcad::pickFace(m_components[static_cast<std::size_t>(compIndex)].shape, dialog.ray());
        if (!hit) {
            QMessageBox::warning(this, QStringLiteral("Pick Failed"), QStringLiteral("That ray didn't hit any face."));
            return;
        }
        px->setValue(hit->point[0]);
        py->setValue(hit->point[1]);
        pz->setValue(hit->point[2]);
        dx->setValue(hit->normal[0]);
        dy->setValue(hit->normal[1]);
        dz->setValue(hit->normal[2]);
    }

    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_compA = nullptr;
    QComboBox* m_compB = nullptr;
    QDoubleSpinBox *m_ax, *m_ay, *m_az, *m_adx, *m_ady, *m_adz;
    QDoubleSpinBox *m_bx, *m_by, *m_bz, *m_bdx, *m_bdy, *m_bdz;
    QDoubleSpinBox* m_value = nullptr;
    const std::vector<AssemblyComponent>& m_components;
};

} // namespace

AssemblyWindow::AssemblyWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("KumCAD — Assembly (early preview)"));
    resize(1200, 800);

    m_viewport = new Viewport3D(this);
    setCentralWidget(m_viewport);

    m_componentList = new QListWidget(this);
    auto* componentDock = new QDockWidget(QStringLiteral("Components"), this);
    componentDock->setWidget(m_componentList);
    addDockWidget(Qt::RightDockWidgetArea, componentDock);

    m_mateList = new QListWidget(this);
    auto* mateDock = new QDockWidget(QStringLiteral("Mates"), this);
    mateDock->setWidget(m_mateList);
    addDockWidget(Qt::RightDockWidgetArea, mateDock);

    QToolBar* toolbar = addToolBar(QStringLiteral("Assembly"));
    toolbar->addAction(QStringLiteral("Add Component from STEP..."), this, &AssemblyWindow::addComponentFromStep);
    toolbar->addAction(QStringLiteral("Add Mate..."), this, &AssemblyWindow::addMate);
    toolbar->addAction(QStringLiteral("Solve"), this, &AssemblyWindow::solve);
    toolbar->addAction(QStringLiteral("Check DOF..."), this, &AssemblyWindow::checkDof);

    if (m_viewport->isAvailable()) {
        statusBar()->showMessage(QStringLiteral("Add components from STEP files, define mates between them, then "
                                                "Solve to place everything"));
    } else {
        statusBar()->showMessage(QStringLiteral("3D viewport unavailable on this system (no usable display) -- the "
                                                "assembly solver still works, just not the preview"));
    }
}

void AssemblyWindow::addComponentFromStep() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Add Component from STEP"), QString(),
                                                        QStringLiteral("STEP Files (*.step *.stp)"));
    if (path.isEmpty()) return;
    const TopoDS_Shape shape = lcad::readStep(path.toStdString());
    if (shape.IsNull()) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"), QStringLiteral("Could not read that STEP file."));
        return;
    }

    AssemblyComponent component;
    component.name = QFileInfo(path).baseName().toStdString();
    component.shape = shape;
    // The first component added has nothing to mate against yet, so it's
    // fixed by default -- every assembly needs at least one anchor for the
    // rest of the mate chain to solve against (see Assembly.h).
    component.fixed = m_assembly.components().empty();
    m_assembly.addComponent(component);
    refreshComponentList();
    refreshViewport();
}

void AssemblyWindow::addMate() {
    if (m_assembly.components().size() < 2) {
        statusBar()->showMessage(QStringLiteral("Add at least two components first"), 3000);
        return;
    }
    AddMateDialog dialog(m_assembly.components(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_assembly.addMate(dialog.result());
    refreshMateList();
}

void AssemblyWindow::solve() {
    m_assembly.solve();
    refreshViewport();
    statusBar()->showMessage(QStringLiteral("Solved"), 2000);
}

void AssemblyWindow::checkDof() {
    const lcad::AssemblyDofReport report = lcad::analyzeAssemblyDof(m_assembly);
    QString message;
    if (report.unplacedComponentIndices.empty() && report.multiplyMatedComponentIndices.empty()) {
        message = QStringLiteral("Every component is either fixed or placed by exactly one mate.");
    } else {
        if (!report.unplacedComponentIndices.empty()) {
            QStringList names;
            for (int idx : report.unplacedComponentIndices) {
                names << QStringLiteral("[%1] %2").arg(idx).arg(
                    QString::fromStdString(m_assembly.components()[static_cast<std::size_t>(idx)].name));
            }
            message += QStringLiteral("Unplaced (neither fixed nor mated): %1\n").arg(names.join(QStringLiteral(", ")));
        }
        if (!report.multiplyMatedComponentIndices.empty()) {
            QStringList names;
            for (int idx : report.multiplyMatedComponentIndices) {
                names << QStringLiteral("[%1] %2").arg(idx).arg(
                    QString::fromStdString(m_assembly.components()[static_cast<std::size_t>(idx)].name));
            }
            message += QStringLiteral("Mated more than once (later mate silently wins): %1").arg(names.join(QStringLiteral(", ")));
        }
    }
    QMessageBox::information(this, QStringLiteral("Assembly DOF Check"), message);
}

void AssemblyWindow::refreshComponentList() {
    m_componentList->clear();
    for (std::size_t i = 0; i < m_assembly.components().size(); ++i) {
        const auto& component = m_assembly.components()[i];
        QString text = QStringLiteral("[%1] %2").arg(i).arg(QString::fromStdString(component.name));
        if (component.fixed) text += QStringLiteral(" (fixed)");
        m_componentList->addItem(text);
    }
}

void AssemblyWindow::refreshMateList() {
    m_mateList->clear();
    for (const Mate& mate : m_assembly.mates()) {
        m_mateList->addItem(QStringLiteral("%1: [%2] -> [%3]")
                                 .arg(mateTypeName(mate.type))
                                 .arg(mate.componentA)
                                 .arg(mate.componentB));
    }
}

void AssemblyWindow::refreshViewport() {
    if (!m_viewport->isAvailable()) return;
    m_viewport->clearShapes();
    for (const auto& component : m_assembly.components()) {
        if (component.shape.IsNull()) continue;
        m_viewport->displayShape(BRepBuilderAPI_Transform(component.shape, component.placement, true).Shape());
    }
    m_viewport->fitAll();
}
