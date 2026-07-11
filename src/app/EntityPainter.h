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
void paint(QPainter& painter, const lcad::Entity& entity, const WorldToScreen& toScreen, double scale,
           const QColor& color, double penWidth, lcad::LineType linetype = lcad::LineType::Continuous,
           double ltScale = 1.0, const lcad::Document* document = nullptr);

} // namespace EntityPainter
