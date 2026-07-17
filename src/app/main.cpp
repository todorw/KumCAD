#include "IconFactory.h"
#include "MainWindow.h"
#include "WelcomeScreen.h"
#ifdef LCAD_HAS_OCCT
#include "Window3D.h"
#endif

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace {

void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(43, 43, 43));
    palette.setColor(QPalette::WindowText, QColor(224, 224, 224));
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::AlternateBase, QColor(47, 47, 47));
    palette.setColor(QPalette::ToolTipBase, QColor(58, 58, 58));
    palette.setColor(QPalette::ToolTipText, QColor(224, 224, 224));
    palette.setColor(QPalette::Text, QColor(224, 224, 224));
    palette.setColor(QPalette::Button, QColor(58, 58, 58));
    palette.setColor(QPalette::ButtonText, QColor(224, 224, 224));
    palette.setColor(QPalette::BrightText, QColor(255, 80, 80));
    palette.setColor(QPalette::Link, QColor(90, 170, 255));
    palette.setColor(QPalette::Highlight, QColor(61, 142, 201));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(140, 140, 140));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));

    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral("QToolTip { color: #e0e0e0; background-color: #3a3a3a; border: 1px solid #555; }"
                                      "QMenu { border: 1px solid #444; }"
                                      "QDockWidget::title { padding: 4px; background: #333; }"
                                      "QToolBar { border: none; spacing: 3px; }"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KumCAD"));
    QApplication::setApplicationName(QStringLiteral("KumCAD"));
    app.setWindowIcon(IconFactory::appIcon());
    applyDarkTheme(app);

    WelcomeScreen welcome;
    if (welcome.exec() != QDialog::Accepted) return 0;

#ifdef LCAD_HAS_OCCT
    if (welcome.choice() == WelcomeScreen::Choice::New3D) {
        Window3D window3d;
        window3d.show();
        return app.exec();
    }
#endif

    MainWindow window;
    if (welcome.choice() == WelcomeScreen::Choice::OpenExisting) {
        window.openSheet(welcome.selectedPath(), QString());
    } else if (welcome.choice() == WelcomeScreen::Choice::NewElectricalPanel) {
        window.setupElectricalPanelMode();
    } else if (welcome.choice() == WelcomeScreen::Choice::NewPid) {
        window.setupPidMode();
    } else if (welcome.choice() == WelcomeScreen::Choice::NewCivil) {
        window.setupCivilMode();
    } else if (welcome.choice() == WelcomeScreen::Choice::NewCam) {
        window.setupCamMode();
    }
    window.show();

    return app.exec();
}
