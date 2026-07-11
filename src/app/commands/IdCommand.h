#pragma once

#include "commands/DrawCommand.h"

// AutoCAD ID: reports the coordinates of a picked point.
class IdCommand : public DrawCommand {
public:
    QString start() override { return QStringLiteral("ID  Specify point:"); }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        m_finished = true;
        return QStringLiteral("X = %1   Y = %2").arg(pt.x, 0, 'f', 4).arg(pt.y, 0, 'f', 4);
    }

    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    bool m_finished = false;
};
