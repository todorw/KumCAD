#pragma once

#include "core/geometry/Point2D.h"

#include <QString>

#include <optional>
#include <utility>
#include <vector>

// Interactive drawing command state machine, modeled on AutoCAD's prompt-driven
// command line: CommandDispatcher feeds picked points (from clicks or typed
// coordinates) and mouse-move previews; the command reports the next prompt
// text until it reports itself finished.
class DrawCommand {
public:
    virtual ~DrawCommand() = default;

    virtual QString start() = 0;
    virtual std::optional<QString> onPoint(const lcad::Point2D& pt) = 0;

    // Bare-number typed input (e.g. a rotation angle in degrees, or a scale
    // factor), for commands that support it as an alternative to picking a
    // point. Default: not supported. Same finish convention as onPoint: check
    // isFinished() after calling to tell "handled and done" apart from
    // "rejected" (both return nullopt).
    virtual std::optional<QString> onScalar(double value) {
        (void)value;
        return std::nullopt;
    }

    virtual void onPreviewPoint(const lcad::Point2D& pt) { (void)pt; }
    virtual std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const { return {}; }

    // True once the command has reached a stage where it wants raw text
    // content (e.g. TEXT's "Enter text:" prompt) rather than a point/number --
    // CommandDispatcher routes ALL typed input to onText() while this is true,
    // including things that would otherwise look like a coordinate or a bare
    // number, and including an empty string for a bare Enter.
    virtual bool wantsTextInput() const { return false; }
    virtual std::optional<QString> onText(const QString& text) {
        (void)text;
        return std::nullopt;
    }

    // The command's current reference point (base/center/last vertex), if it
    // has one yet, used by DrawingView's ortho mode to constrain the next
    // point to be exactly horizontal or vertical relative to it.
    virtual std::optional<lcad::Point2D> anchorPoint() const { return std::nullopt; }

    // Enter/right-click with no point typed: try to finish with the points
    // collected so far. Returns true if the command finished successfully.
    virtual bool requestFinish() { return false; }

    virtual bool isFinished() const = 0;
    virtual void cancel() = 0;
};
