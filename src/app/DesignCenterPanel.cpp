#include "DesignCenterPanel.h"

#include "core/io/DwgReader.h"
#include "core/io/DxfReader.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

DesignCenterPanel::DesignCenterPanel(lcad::Document& document, QWidget* parent) : QWidget(parent), m_document(document) {
    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &DesignCenterPanel::onItemDoubleClicked);

    auto* hint = new QLabel(
        QStringLiteral("Browse a drawing, then double-click an item to copy it into this drawing."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888;"));

    auto* browseButton = new QPushButton(QStringLiteral("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &DesignCenterPanel::onBrowse);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(browseButton);
    layout->addWidget(m_list);
}

void DesignCenterPanel::onBrowse() {
    const QString filter = QStringLiteral("Drawings (*.dxf *.dwg);;DXF Files (*.dxf);;DWG Files (*.dwg)");
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Design Center: Browse"), QString(), filter);
    if (path.isEmpty()) return;

    lcad::Document loaded;
    const bool isDwg = path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive);
    std::string error;
    const bool ok = isDwg ? lcad::readDwg(loaded, path.toStdString(), &error)
                          : lcad::readDxf(loaded, path.toStdString(), &error);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Design Center"), QString::fromStdString(error));
        return;
    }
    m_source = std::move(loaded);
    m_sourcePath = path;
    refreshList();
}

void DesignCenterPanel::refreshList() {
    m_list->clear();
    if (m_sourcePath.isEmpty()) return;

    auto* header = new QListWidgetItem(QStringLiteral("— %1 —").arg(QFileInfo(m_sourcePath).fileName()), m_list);
    header->setFlags(Qt::NoItemFlags);

    for (const auto& block : m_source.blocks()) {
        auto* item = new QListWidgetItem(QStringLiteral("[Block] %1").arg(QString::fromStdString(block->name)), m_list);
        item->setData(Qt::UserRole, QStringLiteral("block"));
        item->setData(Qt::UserRole + 1, QString::fromStdString(block->name));
    }
    for (const lcad::Layer& layer : m_source.layers()) {
        if (layer.id == 0) continue; // layer "0" always exists in the target already
        auto* item = new QListWidgetItem(QStringLiteral("[Layer] %1").arg(QString::fromStdString(layer.name)), m_list);
        item->setData(Qt::UserRole, QStringLiteral("layer"));
        item->setData(Qt::UserRole + 1, QString::fromStdString(layer.name));
    }
    for (const lcad::NamedDimStyle& style : m_source.dimStyles()) {
        auto* item =
            new QListWidgetItem(QStringLiteral("[Dim Style] %1").arg(QString::fromStdString(style.name)), m_list);
        item->setData(Qt::UserRole, QStringLiteral("dimstyle"));
        item->setData(Qt::UserRole + 1, QString::fromStdString(style.name));
    }
    for (const lcad::TextStyle& style : m_source.textStyles()) {
        auto* item =
            new QListWidgetItem(QStringLiteral("[Text Style] %1").arg(QString::fromStdString(style.name)), m_list);
        item->setData(Qt::UserRole, QStringLiteral("textstyle"));
        item->setData(Qt::UserRole + 1, QString::fromStdString(style.name));
    }
    for (const lcad::NamedMLeaderStyle& style : m_source.mleaderStyles()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("[MLeader Style] %1").arg(QString::fromStdString(style.name)), m_list);
        item->setData(Qt::UserRole, QStringLiteral("mleaderstyle"));
        item->setData(Qt::UserRole + 1, QString::fromStdString(style.name));
    }
}

void DesignCenterPanel::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    const QString kind = item->data(Qt::UserRole).toString();
    const std::string name = item->data(Qt::UserRole + 1).toString().toStdString();
    if (kind == QStringLiteral("block")) importBlock(name);
    else if (kind == QStringLiteral("layer")) importLayer(name);
    else if (kind == QStringLiteral("dimstyle")) importDimStyle(name);
    else if (kind == QStringLiteral("textstyle")) importTextStyle(name);
    else if (kind == QStringLiteral("mleaderstyle")) importMLeaderStyle(name);
}

void DesignCenterPanel::importBlock(const std::string& name) {
    const lcad::BlockDefinition* source = m_source.findBlock(name);
    if (!source) return;

    std::string targetName = name;
    int suffix = 2;
    while (m_document.findBlock(targetName)) {
        targetName = name + "(" + std::to_string(suffix) + ")";
        ++suffix;
    }

    std::vector<std::unique_ptr<lcad::Entity>> copies;
    copies.reserve(source->entities.size());
    for (const auto& e : source->entities) {
        auto copy = e->clone();
        copy->setId(m_document.reserveEntityId());
        copies.push_back(std::move(copy));
    }
    m_document.addBlock(targetName, std::move(copies));
    emit documentImported();
}

void DesignCenterPanel::importLayer(const std::string& name) {
    for (const lcad::Layer& existing : m_document.layers()) {
        if (existing.name == name) return; // already present, leave it as-is
    }
    for (const lcad::Layer& source : m_source.layers()) {
        if (source.name != name) continue;
        const lcad::LayerId id = m_document.addLayer(name, source.color);
        if (lcad::Layer* target = m_document.findLayer(id)) {
            target->linetype = source.linetype;
            target->lineweight = source.lineweight;
        }
        break;
    }
    emit documentImported();
}

void DesignCenterPanel::importDimStyle(const std::string& name) {
    if (m_document.findDimStyle(name)) return; // already present, leave it as-is
    for (const lcad::NamedDimStyle& source : m_source.dimStyles()) {
        if (source.name != name) continue;
        m_document.addOrUpdateDimStyle(source.name, source.style);
        break;
    }
    emit documentImported();
}

void DesignCenterPanel::importTextStyle(const std::string& name) {
    if (m_document.findTextStyle(name)) return; // already present, leave it as-is
    for (const lcad::TextStyle& source : m_source.textStyles()) {
        if (source.name != name) continue;
        m_document.addOrUpdateTextStyle(source);
        break;
    }
    emit documentImported();
}

void DesignCenterPanel::importMLeaderStyle(const std::string& name) {
    if (m_document.findMLeaderStyle(name)) return; // already present, leave it as-is
    for (const lcad::NamedMLeaderStyle& source : m_source.mleaderStyles()) {
        if (source.name != name) continue;
        m_document.addOrUpdateMLeaderStyle(source.name, source.style);
        break;
    }
    emit documentImported();
}
