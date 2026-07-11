#pragma once

#include "commands/DrawCommand.h"

class DrawingView;

// OSNAP settings via the command line: shows each snap kind's on/off state;
// typing a keyword (END/MID/CEN/QUA/NOD/INT/PER/TAN/NEA) toggles that kind,
// ALL/NONE set everything at once, Enter finishes.
class OsnapCommand : public DrawCommand {
public:
    explicit OsnapCommand(DrawingView& view) : m_view(view) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    std::optional<QString> onOption(const QString& option) override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    QString statusLine() const;

    DrawingView& m_view;
    bool m_finished = false;
};
