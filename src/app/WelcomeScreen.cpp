#include "WelcomeScreen.h"

#include "IconFactory.h"
#include "RecentFiles.h"
#include "core/io/DwgReader.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

namespace {

QToolButton* makeModeButton(const QIcon& icon, const QString& title, const QString& subtitle, bool enabled) {
    auto* button = new QToolButton;
    button->setIcon(icon);
    button->setIconSize(QSize(48, 48));
    button->setText(subtitle.isEmpty() ? title : QStringLiteral("%1\n%2").arg(title, subtitle));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setFixedSize(150, 110);
    button->setEnabled(enabled);
    button->setAutoRaise(true);
    return button;
}

} // namespace

WelcomeScreen::WelcomeScreen(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Welcome to KumCAD"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* heading = new QLabel(QStringLiteral("<h2>Welcome to KumCAD</h2>"));
    layout->addWidget(heading);
    auto* sub = new QLabel(QStringLiteral("What would you like to create?"));
    sub->setStyleSheet(QStringLiteral("color: #aaa;"));
    layout->addWidget(sub);

    auto* modeGrid = new QGridLayout;
    modeGrid->setSpacing(12);

    auto* mode2D = makeModeButton(IconFactory::mode2DIcon(), QStringLiteral("2D Drafting"), QString(), true);
    connect(mode2D, &QToolButton::clicked, this, &WelcomeScreen::pickNewDrawing);
    modeGrid->addWidget(mode2D, 0, 0);

#ifdef LCAD_HAS_OCCT
    // The 3D kernel core is real (see core/core3d/Document3D.h) as of Phase
    // 2 foundations, but the viewport itself is unverified in whatever
    // environment built this -- see Viewport3D.h's own disclosure.
    auto* mode3D = makeModeButton(IconFactory::mode3DIcon(), QStringLiteral("3D Modeling"),
                                  QStringLiteral("Early preview"), true);
    connect(mode3D, &QToolButton::clicked, this, [this] {
        m_choice = Choice::New3D;
        accept();
    });
#else
    auto* mode3D =
        makeModeButton(IconFactory::mode3DIcon(), QStringLiteral("3D Modeling"), QStringLiteral("Coming soon"), true);
    connect(mode3D, &QToolButton::clicked, this, [this] { showComingSoon(QStringLiteral("3D Modeling")); });
#endif
    modeGrid->addWidget(mode3D, 0, 1);

    // Schematic capture and PCB layout/routing/Gerber export (see
    // core/schematic/Netlist.h, core/pcb/GerberWriter.h) are both real, so
    // this opens the same drawing window as 2D Drafting rather than a
    // "coming soon" notice.
    auto* modePcb =
        makeModeButton(IconFactory::modePcbIcon(), QStringLiteral("PCB Design"), QString(), true);
    connect(modePcb, &QToolButton::clicked, this, &WelcomeScreen::pickNewDrawing);
    modeGrid->addWidget(modePcb, 0, 2);

    auto* modeOther =
        makeModeButton(IconFactory::modeOtherIcon(), QStringLiteral("Other"), QStringLiteral("Coming soon"), true);
    connect(modeOther, &QToolButton::clicked, this, [this] { showComingSoon(QStringLiteral("Other modes")); });
    modeGrid->addWidget(modeOther, 0, 3);

    // A genuinely distinct entry point from plain schematic capture (see
    // MainWindow::setupElectricalPanelMode) -- pre-registers the panel
    // symbol library and starts a first sheet, rather than silently
    // opening the same blank drawing 2D Drafting would.
    auto* modeElectrical = makeModeButton(IconFactory::modeElectricalIcon(), QStringLiteral("Electrical Panel"),
                                          QString(), true);
    connect(modeElectrical, &QToolButton::clicked, this, [this] {
        m_choice = Choice::NewElectricalPanel;
        accept();
    });
    modeGrid->addWidget(modeElectrical, 1, 0);

    layout->addLayout(modeGrid);

    auto* rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    layout->addWidget(rule);

    auto* openRow = new QHBoxLayout;
    auto* openLabel = new QLabel(QStringLiteral("Or open an existing drawing:"));
    openRow->addWidget(openLabel);
    openRow->addStretch();
    auto* openButton = new QPushButton(QStringLiteral("Browse..."));
    connect(openButton, &QPushButton::clicked, this, &WelcomeScreen::pickOpenExisting);
    openRow->addWidget(openButton);
    layout->addLayout(openRow);

    m_recentList = new QListWidget;
    m_recentList->setMaximumHeight(140);
    connect(m_recentList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        pickRecentFile(item->data(Qt::UserRole).toString());
    });
    layout->addWidget(m_recentList);
    refreshRecentList();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &WelcomeScreen::reject);
    layout->addWidget(buttons);

    resize(560, 420);
}

void WelcomeScreen::refreshRecentList() {
    m_recentList->clear();
    const QStringList recent = RecentFiles::list();
    if (recent.isEmpty()) {
        auto* placeholder = new QListWidgetItem(QStringLiteral("(No recent drawings)"));
        placeholder->setFlags(Qt::NoItemFlags);
        m_recentList->addItem(placeholder);
        return;
    }
    for (const QString& path : recent) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName() + QStringLiteral("  —  ") + path);
        item->setData(Qt::UserRole, path);
        m_recentList->addItem(item);
    }
}

void WelcomeScreen::pickNewDrawing() {
    m_choice = Choice::NewDrawing;
    accept();
}

void WelcomeScreen::pickOpenExisting() {
    const QString filter = lcad::dwgSupportAvailable()
                               ? QStringLiteral("Drawings (*.dxf *.dwg);;DXF Files (*.dxf);;DWG Files (*.dwg)")
                               : QStringLiteral("DXF Files (*.dxf)");
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Drawing"), QString(), filter);
    if (path.isEmpty()) return;
    pickRecentFile(path);
}

void WelcomeScreen::pickRecentFile(const QString& path) {
    if (path.isEmpty()) return;
    m_choice = Choice::OpenExisting;
    m_selectedPath = path;
    accept();
}

void WelcomeScreen::showComingSoon(const QString& modeName) {
    QMessageBox::information(this, modeName,
                              QStringLiteral("%1 isn't available yet -- KumCAD is currently a 2D drafting "
                                              "application. This mode is planned for a future release.")
                                  .arg(modeName));
}
