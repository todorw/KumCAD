#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class CommandLine;
class DrawingView;

// Owns the currently-active DrawCommand (if any) and routes input from the
// CommandLine widget and DrawingView (clicks, mouse-move, Escape) to it,
// mirroring AutoCAD's command-line-driven interaction model.
class CommandDispatcher : public QObject {
    Q_OBJECT
public:
    CommandDispatcher(lcad::Document& document, CommandLine& commandLine, QObject* parent = nullptr);

    // Needed so MOVE/COPY/ROTATE/SCALE/ERASE can act on the current selection.
    void setView(DrawingView* view) { m_view = view; }

    bool hasActiveCommand() const { return m_activeCommand != nullptr; }
    DrawCommand* activeDrawCommand() const { return m_activeCommand.get(); }

    // snapRef carries the osnap hit the point came from (nullopt for typed
    // coordinates or free clicks), for commands that record associativity.
    // recordClick is false for the internal re-dispatch from a typed "x,y"
    // coordinate (handleCommandText already records the raw text), true for
    // a genuine mouse click, so ACTRECORD doesn't double up on typed points.
    void handlePointPicked(const lcad::Point2D& pt, const std::optional<lcad::SnapRef>& snapRef = std::nullopt,
                           bool recordClick = true);
    void handleMouseMoved(const lcad::Point2D& pt);
    void handleFinishRequested();
    void handleEscape();

public slots:
    void handleCommandText(const QString& text);
    void undo();
    void redo();

signals:
    void documentChanged();
    void previewChanged();

private:
    void startCommand(std::unique_ptr<DrawCommand> command, const QString& name);
    void finishCommand();
    static bool tryParsePoint(const QString& text, lcad::Point2D& out);

    // Returns the current DrawingView selection, or an empty vector (with a
    // command-line message) if there isn't one. Used by MOVE/COPY/ROTATE/SCALE/ERASE.
    std::vector<lcad::EntityId> selectionForModify() const;

    // World-space click tolerance from the view's current zoom, for commands
    // that pick entities themselves (TRIM/EXTEND).
    double pickTolerance() const;

    // Instant selection-based action (no prompt loop): EXPLODE breaks block
    // references and polylines apart.
    void explodeSelection();

    lcad::Document& m_document;
    CommandLine& m_commandLine;
    DrawingView* m_view = nullptr;
    std::unique_ptr<DrawCommand> m_activeCommand;

    // ACTRECORD/ACTSTOP/PLAY: a simplified action recorder -- one
    // last-recorded macro (not AutoCAD's named, saved-to-disk .actm files),
    // replayed by feeding its lines back through handleCommandText().
    bool m_recording = false;
    QStringList m_recordingBuffer;
    QStringList m_lastMacro;
};
