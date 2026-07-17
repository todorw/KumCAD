#pragma once

#include <QDialog>
#include <QString>

class QListWidget;

// Startup dialog: lets the user pick what kind of document to start, or
// open an existing/recent drawing. Every mode card here opens a REAL,
// working entry point -- 2D Drafting/PCB/Electrical Panel/P&ID/Civil/CAM
// all open the same 2D engine (pre-registered with the right symbol
// library and, where useful, a mode label + status-bar hint, same
// pattern as Electrical Panel's own setup), 3D Modeling and its "Other"
// submenu's 3D Extensions entry open the OCCT-backed 3D engine.
class WelcomeScreen : public QDialog {
    Q_OBJECT
public:
    // New3D is only ever produced when the app was built with OCCT (see
    // LCAD_HAS_OCCT); the 3D card falls back to a "coming soon" notice
    // otherwise, same as PCB did before schematic capture was real.
    enum class Choice {
        NewDrawing,
        OpenExisting,
        New3D,
        NewElectricalPanel,
        NewPid,
        NewCivil,
        NewCam,
    };

    explicit WelcomeScreen(QWidget* parent = nullptr);

    Choice choice() const { return m_choice; }
    // Valid only when choice() == OpenExisting.
    QString selectedPath() const { return m_selectedPath; }

private:
    void refreshRecentList();
    void pickNewDrawing();
    void pickOpenExisting();
    void pickRecentFile(const QString& path);
    void showComingSoon(const QString& modeName);
    // "Other": a small submenu of domains that ride on the existing 2D/3D
    // engines but had no discoverable front door before (P&ID/Civil/CAM
    // were reachable only by typing an exact, undocumented-in-the-UI
    // command name; the 3D extension domains -- Sheet Metal/BIM/FEM/
    // Piping/Assembly -- already have real toolbar buttons once inside
    // 3D Modeling, so that entry just points there).
    void showOtherMenu();

    QListWidget* m_recentList = nullptr;
    Choice m_choice = Choice::NewDrawing;
    QString m_selectedPath;
};
