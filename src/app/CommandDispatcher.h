#pragma once

#include "CommandAliases.h"
#include "commands/DrawCommand.h"
#include "core/document/Document.h"
#include "core/geometry/Point2D.h"
#include "core/lisp/LispInterpreter.h"

#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
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

    // The document's own path lives on MainWindow (core Document doesn't
    // track it), so FIELD/UPDATEFIELD's {{FILENAME}} needs it mirrored here.
    void setDocumentFileName(QString fileName) { m_documentFileName = std::move(fileName); }

    // Exposed so a "Customize Aliases..." dialog can edit the live table
    // (edits apply to the next command typed, no restart needed).
    CommandAliases& commandAliases() { return m_aliases; }

    bool hasActiveCommand() const { return m_activeCommand != nullptr; }
    DrawCommand* activeDrawCommand() const { return m_activeCommand.get(); }
    // True when a click on the canvas should be forwarded to
    // handlePointPicked -- either a real DrawCommand is active, or AutoLISP
    // is paused in (getpoint) waiting for one. DrawingView's mouse-press
    // handler uses this instead of hasActiveCommand() so clicking answers a
    // getpoint prompt the same way typing "x,y" does.
    bool wantsPointInput() const { return hasActiveCommand() || (m_lispLoop && m_lispWaitIsPoint); }

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
    // SCRIPT: plays a .scr command script (see core/util/Script.h) by
    // feeding its entries back through handleCommandText. Public so a
    // future command-line "-s script.scr" launch flag can reuse it.
    void runScriptFile(const QString& path);
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
    // OVERKILL (Express Tools): deletes exact geometric duplicates within
    // the current selection, keeping the first occurrence of each.
    void overkillSelection();
    // More Express Tools (see core/document/ExpressTools.h): BURST explodes
    // blocks keeping attribute VALUES as text (EXPLODE reverts them to tag
    // placeholders); TXT2MTXT combines selected TEXT into one MTEXT;
    // TORIENT flips selected text right-side-up.
    void burstSelection();
    void txt2mtxtSelection();
    void torientSelection();
    // Layer tools. LAYISO hides every layer not represented in the
    // selection and remembers what it hid; LAYUNISO restores that.
    // LAYOFF/LAYFRZ act on the selection's layers, LAYON/LAYTHW on all.
    void layerIsolate();
    void layerUnisolate();
    void layerToolOnSelection(bool freeze); // LAYOFF (freeze=false) / LAYFRZ (true)
    void layerAllVisible(bool thaw);        // LAYON (thaw=false) / LAYTHW (true)

    lcad::Document& m_document;
    CommandLine& m_commandLine;
    DrawingView* m_view = nullptr;
    QString m_documentFileName;
    std::unique_ptr<DrawCommand> m_activeCommand;
    CommandAliases m_aliases;

    // ACTRECORD/ACTSTOP/PLAY: a simplified action recorder -- one
    // last-recorded macro (not AutoCAD's named, saved-to-disk .actm files),
    // replayed by feeding its lines back through handleCommandText().
    bool m_recording = false;
    QStringList m_recordingBuffer;
    QStringList m_lastMacro;

    // Layers LAYISO turned off (they were visible before), for LAYUNISO.
    std::vector<lcad::LayerId> m_layIsoHidden;

    // SCRIPT: the next typed line is the script path; depth guards a
    // script SCRIPTing itself forever.
    bool m_awaitingScriptPath = false;
    int m_scriptDepth = 0;

    // A scoped-down AutoLISP interpreter (see LispInterpreter.h for what's
    // not implemented): input starting with '(' at the top-level command
    // prompt is evaluated as AutoLISP, matching real AutoCAD's command line.
    // Its (command ...) sink re-enters handleCommandText.
    lcad::LispInterpreter m_lisp;

    // Backs (getpoint)/(getreal)/(getstring)/(getkword): called from deep in
    // m_lisp.run()'s call stack, it prints the prompt and blocks on a nested
    // QEventLoop so the UI keeps responding (repaints, the pending click or
    // typed line) until handlePointPicked/handleCommandText see m_lispLoop
    // is set and feed the answer back in instead of routing it as normal
    // command input. Escape sets m_lispWaitCancelled and quits the loop too.
    std::optional<std::string> waitForLispInput(const std::string& prompt, const std::vector<std::string>& keywords,
                                                bool isPoint);

    QEventLoop* m_lispLoop = nullptr;
    bool m_lispWaitIsPoint = false;   // getpoint: a click resolves the wait too, not just typed text
    bool m_lispWaitIsKeyword = false; // getkword: typed text must match m_lispKeywords
    QStringList m_lispKeywords;
    std::optional<std::string> m_lispInputResult;
    bool m_lispWaitCancelled = false;
};
