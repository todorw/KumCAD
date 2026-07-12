#include "PropertiesPanel.h"

#include "DrawingView.h"
#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/MText.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Text.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
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

struct NamedColor {
    const char* name;
    lcad::Color color;
};

// The classic ACI 1-9 palette, the colors drafters actually reach for.
const NamedColor kNamedColors[] = {
    {"Red", {255, 0, 0}},     {"Yellow", {255, 255, 0}}, {"Green", {0, 255, 0}},
    {"Cyan", {0, 255, 255}},  {"Blue", {0, 0, 255}},     {"Magenta", {255, 0, 255}},
    {"White", {255, 255, 255}}, {"Gray", {128, 128, 128}},
};

QIcon colorSwatch(const lcad::Color& color) {
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QColor(0, 0, 0, 80));
    painter.setBrush(QColor(color.r, color.g, color.b));
    painter.drawRect(0, 0, 11, 11);
    return QIcon(pixmap);
}

// Combo item data: -1 = ByLayer, -2 = Custom..., otherwise 0xRRGGBB.
constexpr int kByLayerData = -1;
constexpr int kCustomData = -2;

int packColor(const lcad::Color& c) {
    return (static_cast<int>(c.r) << 16) | (static_cast<int>(c.g) << 8) | static_cast<int>(c.b);
}

lcad::Color unpackColor(int v) {
    return lcad::Color{static_cast<std::uint8_t>((v >> 16) & 0xFF), static_cast<std::uint8_t>((v >> 8) & 0xFF),
                       static_cast<std::uint8_t>(v & 0xFF)};
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

    m_colorCombo = new QComboBox(this);
    populateColorCombo();
    connect(m_colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertiesPanel::onColorComboChanged);

    m_linetypeCombo = new QComboBox(this);
    populateLinetypeCombo();
    connect(m_linetypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertiesPanel::onLinetypeComboChanged);

    auto* topForm = new QFormLayout();
    topForm->addRow(QStringLiteral("Layer:"), m_layerCombo);
    topForm->addRow(QStringLiteral("Color:"), m_colorCombo);
    topForm->addRow(QStringLiteral("Linetype:"), m_linetypeCombo);

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
        m_colorCombo->setEnabled(false);
        m_linetypeCombo->setEnabled(false);
        m_updating = false;
        return;
    }
    m_layerCombo->setEnabled(true);
    m_colorCombo->setEnabled(true);
    m_linetypeCombo->setEnabled(true);

    // Color combo: ByLayer when nothing is overridden, the matching swatch
    // when every entity carries the same override, Custom for mixed states.
    int totalCount = 0;
    int overrideCount = 0;
    bool sameOverride = true;
    std::optional<lcad::Color> commonColor;
    for (lcad::EntityId id : ids) {
        if (const lcad::Entity* e = m_document.findEntity(id)) {
            ++totalCount;
            if (const auto& o = e->colorOverride()) {
                ++overrideCount;
                if (!commonColor) commonColor = o;
                else if (!(*o == *commonColor)) sameOverride = false;
            }
        }
    }
    int colorIndex = 0; // ByLayer
    if (overrideCount > 0) {
        colorIndex = m_colorCombo->count() - 1; // Custom, unless a swatch matches below
        if (overrideCount == totalCount && sameOverride && commonColor) {
            for (int i = 1; i < m_colorCombo->count() - 1; ++i) {
                if (unpackColor(m_colorCombo->itemData(i).toInt()) == *commonColor) {
                    colorIndex = i;
                    break;
                }
            }
        }
    }
    m_colorCombo->setCurrentIndex(colorIndex);

    // Linetype combo: index 0 is ByLayer; a specific linetype is shown only
    // when every selected entity carries that same override.
    int linetypeIndex = 0;
    {
        bool first = true;
        bool uniform = true;
        std::optional<lcad::LineType> common;
        for (lcad::EntityId id : ids) {
            if (const lcad::Entity* e = m_document.findEntity(id)) {
                if (first) {
                    common = e->linetypeOverride();
                    first = false;
                } else if (e->linetypeOverride() != common) {
                    uniform = false;
                }
            }
        }
        if (uniform && common) {
            for (int i = 1; i < m_linetypeCombo->count(); ++i) {
                if (static_cast<lcad::LineType>(m_linetypeCombo->itemData(i).toInt()) == *common) {
                    linetypeIndex = i;
                    break;
                }
            }
        }
    }
    m_linetypeCombo->setCurrentIndex(linetypeIndex);

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
        addRow(QStringLiteral("Rotation:"), formatDegrees(ellipse->rotation()));
        break;
    }
    case lcad::EntityType::Spline: {
        const auto* spline = static_cast<const lcad::SplineEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Spline"));
        addRow(QStringLiteral("Degree:"), QString::number(spline->degree()));
        addRow(QStringLiteral("Control points:"), QString::number(spline->controlPoints().size()));
        addRow(QStringLiteral("Fit points:"), QString::number(spline->fitPoints().size()));
        break;
    }
    case lcad::EntityType::Dimension: {
        const auto* dim = static_cast<const lcad::DimensionEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Dimension"));
        QString kind;
        switch (dim->kind()) {
        case lcad::DimensionKind::Linear: kind = QStringLiteral("Linear"); break;
        case lcad::DimensionKind::Aligned: kind = QStringLiteral("Aligned"); break;
        case lcad::DimensionKind::Radius: kind = QStringLiteral("Radius"); break;
        case lcad::DimensionKind::Diameter: kind = QStringLiteral("Diameter"); break;
        case lcad::DimensionKind::Angular: kind = QStringLiteral("Angular"); break;
        }
        addRow(QStringLiteral("Type:"), kind);
        addRow(QStringLiteral("Value:"), QString::fromUtf8(dim->geometry().label.c_str()));
        addRow(QStringLiteral("Point 1:"),
               QStringLiteral("%1, %2").arg(formatNumber(dim->point1().x), formatNumber(dim->point1().y)));
        addRow(QStringLiteral("Point 2:"),
               QStringLiteral("%1, %2").arg(formatNumber(dim->point2().x), formatNumber(dim->point2().y)));
        break;
    }
    case lcad::EntityType::Leader: {
        const auto* leader = static_cast<const lcad::LeaderEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Leader"));
        addRow(QStringLiteral("Points:"), QString::number(leader->points().size()));
        addRow(QStringLiteral("Arrow size:"), formatNumber(leader->arrowSize()));
        break;
    }
    case lcad::EntityType::Hatch: {
        const auto* hatch = static_cast<const lcad::HatchEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Hatch"));
        addRow(QStringLiteral("Pattern:"), QLatin1String(lcad::hatchPatternName(hatch->pattern())));
        if (hatch->pattern() != lcad::HatchPattern::Solid) {
            addRow(QStringLiteral("Scale:"), formatNumber(hatch->patternScale()));
            addRow(QStringLiteral("Angle:"), formatDegrees(hatch->patternAngle()));
        }
        addRow(QStringLiteral("Vertices:"), QString::number(hatch->vertices().size()));
        break;
    }
    case lcad::EntityType::Insert: {
        const auto* insert = static_cast<const lcad::InsertEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Block Reference"));
        addRow(QStringLiteral("Block:"), QString::fromStdString(insert->blockName()));
        addRow(QStringLiteral("Position X:"), formatNumber(insert->position().x));
        addRow(QStringLiteral("Position Y:"), formatNumber(insert->position().y));
        addRow(QStringLiteral("Scale:"), formatNumber(insert->scaleFactor()));
        addRow(QStringLiteral("Rotation:"), formatDegrees(insert->rotation()));
        if (const lcad::BlockDefinition* block = insert->block()) {
            const lcad::EntityId insertId = insert->id();
            if (block->dynamicVisibility) {
                auto* combo = new QComboBox(this);
                for (const std::string& state : block->dynamicVisibility->states) {
                    combo->addItem(QString::fromStdString(state));
                }
                const QString current = QString::fromStdString(insert->visibilityState());
                const int idx = combo->findText(current);
                combo->setCurrentIndex(idx >= 0 ? idx : 0);
                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this, combo, insertId](int index) {
                            if (m_updating || index < 0) return;
                            m_document.commandStack().execute(std::make_unique<lcad::SetInsertVisibilityStateCommand>(
                                m_document, insertId, combo->currentText().toStdString()));
                            emit documentChanged();
                        });
                m_fieldsForm->addRow(QStringLiteral("Visibility:"), combo);
            }
            if (block->dynamicLookup) {
                auto* combo = new QComboBox(this);
                for (const auto& [label, factor] : block->dynamicLookup->presets) {
                    (void)factor;
                    combo->addItem(QString::fromStdString(label));
                }
                const QString current = QString::fromStdString(insert->lookupValue());
                const int idx = combo->findText(current);
                combo->setCurrentIndex(idx >= 0 ? idx : 0);
                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this, combo, insertId](int index) {
                            if (m_updating || index < 0) return;
                            m_document.commandStack().execute(std::make_unique<lcad::SetInsertLookupCommand>(
                                m_document, insertId, combo->currentText().toStdString()));
                            emit documentChanged();
                        });
                m_fieldsForm->addRow(QStringLiteral("Lookup:"), combo);
            }
        }
        break;
    }
    case lcad::EntityType::MText: {
        const auto* mtext = static_cast<const lcad::MTextEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("MText"));
        addRow(QStringLiteral("Position X:"), formatNumber(mtext->position().x));
        addRow(QStringLiteral("Position Y:"), formatNumber(mtext->position().y));
        addRow(QStringLiteral("Height:"), formatNumber(mtext->height()));
        addRow(QStringLiteral("Width:"), formatNumber(mtext->width()));
        addRow(QStringLiteral("Lines:"), QString::number(mtext->wrappedLines().size()));
        addRow(QStringLiteral("Content:"), QString::fromStdString(mtext->text()));
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
    case lcad::EntityType::Point: {
        const auto* point = static_cast<const lcad::PointEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Point"));
        addRow(QStringLiteral("X:"), formatNumber(point->position().x));
        addRow(QStringLiteral("Y:"), formatNumber(point->position().y));
        break;
    }
    case lcad::EntityType::ConstructionLine: {
        const auto* cl = static_cast<const lcad::ConstructionLineEntity*>(e);
        m_summaryLabel->setText(cl->isRay() ? QStringLiteral("Ray") : QStringLiteral("Construction Line"));
        addRow(QStringLiteral("Base X:"), formatNumber(cl->basePoint().x));
        addRow(QStringLiteral("Base Y:"), formatNumber(cl->basePoint().y));
        addRow(QStringLiteral("Angle:"), formatDegrees(std::atan2(cl->direction().y, cl->direction().x)));
        break;
    }
    case lcad::EntityType::AttDef: {
        const auto* attdef = static_cast<const lcad::AttDefEntity*>(e);
        m_summaryLabel->setText(QStringLiteral("Attribute Definition"));
        addRow(QStringLiteral("Tag:"), QString::fromStdString(attdef->tag()));
        addRow(QStringLiteral("Prompt:"), QString::fromStdString(attdef->prompt()));
        addRow(QStringLiteral("Default:"), QString::fromStdString(attdef->defaultValue()));
        break;
    }
    }

    m_updating = false;
}

void PropertiesPanel::populateColorCombo() {
    m_colorCombo->addItem(QStringLiteral("ByLayer"), kByLayerData);
    for (const NamedColor& nc : kNamedColors) {
        m_colorCombo->addItem(colorSwatch(nc.color), QString::fromLatin1(nc.name), packColor(nc.color));
    }
    m_colorCombo->addItem(QStringLiteral("Custom..."), kCustomData);
}

void PropertiesPanel::populateLinetypeCombo() {
    m_linetypeCombo->addItem(QStringLiteral("ByLayer"), kByLayerData);
    for (lcad::LineType type : lcad::allLineTypes()) {
        m_linetypeCombo->addItem(QLatin1String(lcad::lineTypeName(type)), static_cast<int>(type));
    }
}

void PropertiesPanel::onLinetypeComboChanged(int index) {
    if (m_updating || index < 0) return;
    const auto ids = m_view.selectedIds();
    if (ids.empty()) return;

    std::optional<lcad::LineType> linetype;
    if (m_linetypeCombo->itemData(index).toInt() != kByLayerData) {
        linetype = static_cast<lcad::LineType>(m_linetypeCombo->itemData(index).toInt());
    }
    m_document.commandStack().execute(std::make_unique<lcad::SetEntityLinetypeCommand>(m_document, ids, linetype));
    emit documentChanged();
}

void PropertiesPanel::applyColor(std::optional<lcad::Color> color) {
    const auto ids = m_view.selectedIds();
    if (ids.empty()) return;
    m_document.commandStack().execute(std::make_unique<lcad::SetEntityColorCommand>(m_document, ids, color));
    emit documentChanged();
}

void PropertiesPanel::onColorComboChanged(int index) {
    if (m_updating || index < 0) return;
    const int data = m_colorCombo->itemData(index).toInt();
    if (data == kByLayerData) {
        applyColor(std::nullopt);
    } else if (data == kCustomData) {
        const QColor picked = QColorDialog::getColor(Qt::white, this, QStringLiteral("Entity Color"));
        if (picked.isValid()) {
            applyColor(lcad::Color{static_cast<std::uint8_t>(picked.red()), static_cast<std::uint8_t>(picked.green()),
                                   static_cast<std::uint8_t>(picked.blue())});
        } else {
            refresh(); // dialog cancelled: restore the combo to reality
        }
    } else {
        applyColor(unpackColor(data));
    }
}

void PropertiesPanel::onLayerComboChanged(int index) {
    if (m_updating || index < 0) return;
    const auto ids = m_view.selectedIds();
    if (ids.empty()) return;

    const lcad::LayerId newLayer = static_cast<lcad::LayerId>(m_layerCombo->itemData(index).toULongLong());
    m_document.commandStack().execute(std::make_unique<lcad::SetEntityLayerCommand>(m_document, ids, newLayer));
    emit documentChanged();
}
