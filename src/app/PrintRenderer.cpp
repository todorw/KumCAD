#include "PrintRenderer.h"

#include "EntityPainter.h"

#include "core/document/Document.h"

#include <QPainter>
#include <QPrinter>

#include <algorithm>

namespace {

void renderLayoutPage(QPainter& painter, double resolutionDpi, const lcad::Document& document,
                      const lcad::Layout& layout) {
    const QRect viewport = painter.viewport();
    painter.fillRect(viewport, Qt::white);

    // Fit the sheet to the page (the sheet itself prints without a border).
    const double margin = 1.02;
    const double scale =
        std::min(viewport.width() / (layout.paperWidth * margin), viewport.height() / (layout.paperHeight * margin));
    const QPointF pageCenter = QRectF(viewport).center();
    const double cx = layout.paperWidth / 2.0;
    const double cy = layout.paperHeight / 2.0;
    const auto paperToPage = [scale, cx, cy, pageCenter](const lcad::Point2D& p) {
        return QPointF((p.x - cx) * scale + pageCenter.x(), pageCenter.y() - (p.y - cy) * scale);
    };

    // A plotted lineweight of 0mm still needs to show up as a hairline.
    const auto penWidthFor = [resolutionDpi](double lineweightMm) {
        return std::max(1.0, lineweightMm * resolutionDpi / 25.4);
    };
    for (const lcad::Viewport& vp : layout.viewports) {
        const QPointF tl = paperToPage(
            lcad::Point2D(vp.paperCenter.x - vp.paperWidth / 2.0, vp.paperCenter.y + vp.paperHeight / 2.0));
        const QPointF br = paperToPage(
            lcad::Point2D(vp.paperCenter.x + vp.paperWidth / 2.0, vp.paperCenter.y - vp.paperHeight / 2.0));
        painter.save();
        painter.setClipRect(QRectF(tl, br).normalized());
        const auto toScreen = [&](const lcad::Point2D& p) {
            return paperToPage(vp.paperCenter + (p - vp.modelCenter) * vp.viewScale);
        };
        const double effScale = vp.viewScale * scale;
        for (const lcad::Entity* e : document.entities()) {
            const lcad::Layer* layer = document.findLayer(e->layer());
            if (layer && !layer->visible) continue;
            const lcad::PlotAppearance appearance = document.plotAppearance(*e);
            QColor color(appearance.color.r, appearance.color.g, appearance.color.b);
            if (appearance.color.r > 200 && appearance.color.g > 200 && appearance.color.b > 200) color = Qt::black;
            // Each viewport gets its own annotation-scale representation
            // (1/viewScale) instead of the document-wide "current" scale,
            // so the same annotative object reads correctly sized on every
            // viewport's printed sheet simultaneously, whatever its own
            // plot scale is.
            const double annoOverride = vp.viewScale > 1e-9 ? 1.0 / vp.viewScale : 0.0;
            EntityPainter::paint(painter, *e, toScreen, effScale, color, penWidthFor(appearance.lineweight),
                                 appearance.linetype, document.lineTypeScale(), &document, annoOverride);
        }
        painter.restore();
    }

    // Entities drawn directly on the sheet (title blocks, notes).
    const int layoutIndex = static_cast<int>(&layout - document.layouts().data());
    for (const lcad::Entity* e : document.paperEntities(layoutIndex)) {
        const lcad::Layer* layer = document.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        const lcad::PlotAppearance appearance = document.plotAppearance(*e);
        QColor color(appearance.color.r, appearance.color.g, appearance.color.b);
        if (appearance.color.r > 200 && appearance.color.g > 200 && appearance.color.b > 200) color = Qt::black;
        EntityPainter::paint(painter, *e, paperToPage, scale, color, penWidthFor(appearance.lineweight),
                             appearance.linetype, document.lineTypeScale(), &document);
    }
}

void renderModelPage(QPainter& painter, double resolutionDpi, const lcad::Document& document) {
    lcad::BoundingBox box;
    const auto entities = document.entities();
    for (const lcad::Entity* e : entities) {
        const lcad::Layer* layer = document.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        box.expand(e->boundingBox());
    }

    const QRect viewport = painter.viewport();
    painter.fillRect(viewport, Qt::white);

    if (!box.isValid()) {
        painter.drawText(viewport, Qt::AlignCenter, QStringLiteral("(empty drawing)"));
        return;
    }

    const double w = std::max(box.max.x - box.min.x, 1e-6);
    const double h = std::max(box.max.y - box.min.y, 1e-6);
    const double margin = 1.1;
    const double scale = std::min(viewport.width() / (w * margin), viewport.height() / (h * margin));
    const double cx = (box.min.x + box.max.x) / 2.0;
    const double cy = (box.min.y + box.max.y) / 2.0;
    const QPointF pageCenter = QRectF(viewport).center();
    const auto toScreen = [scale, cx, cy, pageCenter](const lcad::Point2D& p) {
        return QPointF((p.x - cx) * scale + pageCenter.x(), pageCenter.y() - (p.y - cy) * scale);
    };

    for (const lcad::Entity* e : entities) {
        const lcad::Layer* layer = document.findLayer(e->layer());
        if (layer && !layer->visible) continue;

        const lcad::PlotAppearance appearance = document.plotAppearance(*e);
        // Colors that read well on the dark canvas vanish on paper.
        QColor color(appearance.color.r, appearance.color.g, appearance.color.b);
        if (appearance.color.r > 200 && appearance.color.g > 200 && appearance.color.b > 200) color = Qt::black;
        const double penWidth = std::max(1.0, appearance.lineweight * resolutionDpi / 25.4);

        EntityPainter::paint(painter, *e, toScreen, scale, color, penWidth, appearance.linetype,
                             document.lineTypeScale(), &document);
    }
}

} // namespace

void renderDocumentPage(QPainter& painter, double resolutionDpi, const lcad::Document& document,
                        const lcad::Layout* layout) {
    painter.setRenderHint(QPainter::Antialiasing);
    if (layout) {
        renderLayoutPage(painter, resolutionDpi, document, *layout);
    } else {
        renderModelPage(painter, resolutionDpi, document);
    }
}

void renderDocumentPage(QPrinter& printer, const lcad::Document& document, const lcad::Layout* layout) {
    QPainter painter(&printer);
    renderDocumentPage(painter, printer.resolution(), document, layout);
}
