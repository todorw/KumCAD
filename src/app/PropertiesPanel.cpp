#include "PropertiesPanel.h"

#include "DrawingView.h"
#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"

#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>
#include <optional>

namespace {

QString formatNumber(double v) {
    return QString::number(v, 'f', 3);
}

QString formatDegrees(double radians) {
    return formatNumber(radians * 180.0 / M_PI) + QStringLiteral("\xC2\xB0"); // degree sign
}

} // namespace

PropertiesPanel::PropertiesPanel(lcad::Document& document, DrawingView& view, QWidget* parent)
    : QWidget(parent), m_document(document), m_view(view) {
    m_summaryLabel = new QLabel(QStringLiteral("No selection"), this);
    QFont boldFont = m_summaryLabel->font();
    boldFont.setBold(true);
    m_summaryLabel->setFont(boldFont);

    m_layerCombo = new QComboBox(this);
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertiesPanel::onLayerComboChanged);

    auto* topForm = new QFormLayout();
    topForm->addRow(QStringLiteral("Layer:"), m_layerCombo);

    m_fieldsForm = new QFormLayout();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summaryLabel);
    layout->addLayout(topForm);
    layout->addLayout(m_fieldsForm);
    layout->addStretch();

    refresh();
}

void PropertiesPanel::addRow(const QString& label, const QString& value) {
    auto* valueLabel = new QLabel(value, this);
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fieldsForm->addRow(label, valueLabel);
}

void PropertiesPanel::refresh() {
    m_updating = true;

    while (m_fieldsForm->rowCount() > 0) m_fieldsForm->removeRow(0);

    m_layerCombo->clear();
    for (const lcad::Layer& layer : m_document.layers()) {
        m_layerCombo->addItem(QString::fromStdString(layer.name), static_cast<qulonglong>(layer.id));
    }

    const auto ids = m_view.selectedIds();
    if (ids.empty()) {
        m_summaryLabel->setText(QStringLiteral("No selection"));
        m_layerCombo->setEnabled(false);
        m_updating = false;
        return;
    }
    m_layerCombo->setEnabled(true);

    std::optional<lcad::LayerId> commonLayer;
    bool uniformLayer = true;
    for (lcad::EntityId id : ids) {
        if (const lcad::Entity* e = m_document.findEntity(id)) {
            if (!commonLayer) {
                commonLayer = e->layer();
            } else if (*commonLayer != e->layer()) {
                uniformLayer = false;
            }
        }
    }
    if (uniformLayer && commonLayer) {
        for (int i = 0; i < m_layerCombo->count(); ++i) {
            if (static_cast<lcad::LayerId>(m_layerCombo->itemData(i).toULongLong()) == *commonLayer) {
                m_layerCombo->setCurrentIndex(i);
                break;
            }
        }
    } else {
        m_layerCombo->setCurrentIndex(-1);
    }

    if (ids.size() > 1) {
        m_summaryLabel->setText(QStringLiteral("%1 objects selected").arg(ids.size()));
        m_updating = false;
        return;
    }

    const lcad::Entity* e = m_document.findEntity(ids.front());
    if (!e) {
        m_summaryLabel->setText(QStringLiteral("No selection"));
        m_updating = false;
        return;
    }

    switch (e->type()) {
    case lcad::EntityType::Line: {
        const auto* line = static_cast<const lcad::LineEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Line"));
        addRow(QStringLiteral("Start X:"), formatNumber(line->start().x));
        addRow(QStringLiteral("Start Y:"), formatNumber(line->start().y));
        addRow(QStringLiteral("End X:"), formatNumber(line->end().x));
        addRow(QStringLiteral("End Y:"), formatNumber(line->end().y));
        addRow(QStringLiteral("Length:"), formatNumber(line->start().distanceTo(line->end())));
        break;
    }
    case lcad::EntityType::Circle: {
        const auto* circle = static_cast<const lcad::CircleEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Circle"));
        addRow(QStringLiteral("Center X:"), formatNumber(circle->center().x));
        addRow(QStringLiteral("Center Y:"), formatNumber(circle->center().y));
        addRow(QStringLiteral("Radius:"), formatNumber(circle->radius()));
        break;
    }
    case lcad::EntityType::Arc: {
        const auto* arc = static_cast<const lcad::ArcEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Arc"));
        addRow(QStringLiteral("Center X:"), formatNumber(arc->center().x));
        addRow(QStringLiteral("Center Y:"), formatNumber(arc->center().y));
        addRow(QStringLiteral("Radius:"), formatNumber(arc->radius()));
        addRow(QStringLiteral("Start Angle:"), formatDegrees(arc->startAngle()));
        addRow(QStringLiteral("End Angle:"), formatDegrees(arc->endAngle()));
        break;
    }
    case lcad::EntityType::Polyline: {
        const auto* pl = static_cast<const lcad::PolylineEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Polyline"));
        addRow(QStringLiteral("Vertices:"), QString::number(pl->vertices().size()));
        addRow(QStringLiteral("Closed:"), pl->closed() ? QStringLiteral("Yes") : QStringLiteral("No"));
        break;
    }
    case lcad::EntityType::Ellipse: {
        const auto* ellipse = static_cast<const lcad::EllipseEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Ellipse"));
        addRow(QStringLiteral("Center X:"), formatNumber(ellipse->center().x));
        addRow(QStringLiteral("Center Y:"), formatNumber(ellipse->center().y));
        addRow(QStringLiteral("Radius X:"), formatNumber(ellipse->radiusX()));
        addRow(QStringLiteral("Radius Y:"), formatNumber(ellipse->radiusY()));
        break;
    }
    case lcad::EntityType::Text: {
        const auto* text = static_cast<const lcad::TextEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Text"));
        addRow(QStringLiteral("Position X:"), formatNumber(text->position().x));
        addRow(QStringLiteral("Position Y:"), formatNumber(text->position().y));
        addRow(QStringLiteral("Height:"), formatNumber(text->height()));
        addRow(QStringLiteral("Rotation:"), formatDegrees(text->rotation()));
        addRow(QStringLiteral("Content:"), QString::fromStdString(text->text()));
        break;
    }
    }

    m_updating = false;
}

void PropertiesPanel::onLayerComboChanged(int index) {
    if (m_updating || index < 0) return;
    const auto ids = m_view.selectedIds();
    if (ids.empty()) return;

    const lcad::LayerId newLayer = static_cast<lcad::LayerId>(m_layerCombo->itemData(index).toULongLong());
    m_document.commandStack().execute(std::make_unique<lcad::SetEntityLayerCommand>(m_document, ids, newLayer));
    emit documentChanged();
}
