#pragma once

#include "core/document/LineType.h"
#include "core/geometry/Entity.h"

#include <QColor>
#include <QPointF>

#include <functional>

class QPainter;

namespace lcad {
class Document;
}

// Entity drawing shared by the interactive canvas (DrawingView) and the
// print/PDF path. The world-to-screen mapping is injected so each caller
// brings its own viewport (pan/zoom vs. fit-to-page).
namespace EntityPainter {

using WorldToScreen = std::function<QPointF(const lcad::Point2D&)>;

// scale = device pixels per world unit (for radii and text sizes).
// linetype is the entity's resolved linetype (override or layer), with
// patterns in drawing units scaled by ltScale (the document's LTSCALE).
// document, when given, resolves TEXT/MTEXT named text styles (font family,
// width factor, oblique) — without it text renders in the default face.
//
// annotationScaleOverride, when positive, is the multiplier Annotative text
// renders at instead of document->annotationScale() -- how a paper-space
// viewport gets its own correct annotation representation (1/viewScale)
// independent of whatever scale the user is currently editing model space
// at, so the same annotative object reads at its true plotted size in every
// viewport simultaneously, at whatever scale each one plots. 0 (the
// default) means "use document->annotationScale()", matching the single
// current representation model space itself is edited at.
//
// backgroundColor is what a WIPEOUT entity actually fills with (see
// core/geometry/Wipeout.h) -- injected rather than assumed, since the two
// callers have genuinely different backgrounds (DrawingView's dark canvas
// vs. PrintRenderer's white paper), the same reason toScreen itself is
// injected instead of computed here.
void paint(QPainter& painter, const lcad::Entity& entity, const WorldToScreen& toScreen, double scale,
           const QColor& color, double penWidth, lcad::LineType linetype = lcad::LineType::Continuous,
           double ltScale = 1.0, const lcad::Document* document = nullptr, double annotationScaleOverride = 0.0,
           const QColor& backgroundColor = QColor(33, 33, 33));

} // namespace EntityPainter
