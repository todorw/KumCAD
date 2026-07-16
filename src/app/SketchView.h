#pragma once

#include "core/sketch/SketchGeometry.h"

#include <QWidget>

#include <optional>
#include <vector>

// A basic interactive 2D sketch editor canvas: click to place lines/
// circles (snapping onto existing points so shared endpoints get real
// structural coincidence, see SketchGeometry.h), click to select geometry
// for applying a constraint. Plain QWidget + QPainter (unlike DrawingView/
// Viewport3D, there's no OpenGL or native-window integration needed here at
// all -- it's simple 2D line art), so it doesn't carry either of those
// widgets' platform-dependent risk.
class SketchView : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Line, Circle };

    struct Selection {
        enum class Kind { Point, Line, Circle };
        Kind kind;
        int index;
    };

    explicit SketchView(QWidget* parent = nullptr);

    lcad::Sketch& sketch() { return m_sketch; }
    const lcad::Sketch& sketch() const { return m_sketch; }

    void setTool(Tool tool);
    const std::vector<Selection>& selection() const { return m_selection; }
    void clearSelection();

    // Re-runs the constraint solver and repaints.
    void resolve();

signals:
    void statusMessage(const QString& text);
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QPointF toScreen(const lcad::Point2D& p) const;
    lcad::Point2D toSketch(const QPointF& p) const;

    // Returns the index of an existing point within snapping distance of
    // sketchPos, or creates and returns a new free point there.
    int findOrCreatePoint(const lcad::Point2D& sketchPos);

    // Nearest selectable entity to sketchPos within pick tolerance, or
    // nullopt.
    std::optional<Selection> pickEntity(const lcad::Point2D& sketchPos) const;

    lcad::Sketch m_sketch;
    Tool m_tool = Tool::Select;
    std::vector<Selection> m_selection;
    std::optional<int> m_pendingLineStart;
    double m_scale = 15.0;
    QPointF m_panOffset{0.0, 0.0};
};
