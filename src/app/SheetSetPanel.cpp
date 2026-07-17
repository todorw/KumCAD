#include "SheetSetPanel.h"

#include "PrintRenderer.h"

#include "core/document/Document.h"
#include "core/io/DwgReader.h"
#include "core/io/DxfReader.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPrinter>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

// Loads path into a scratch document just far enough to list its layout
// names, for the "which layout is this sheet" picker.
QStringList peekLayoutNames(const QString& path) {
    lcad::Document scratch;
    const bool isDwg = path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive);
    const bool ok = isDwg ? lcad::readDwg(scratch, path.toStdString()) : lcad::readDxf(scratch, path.toStdString());
    QStringList names;
    if (!ok) return names;
    for (const lcad::Layout& layout : scratch.layouts()) names << QString::fromStdString(layout.name);
    return names;
}

} // namespace

SheetSetPanel::SheetSetPanel(QWidget* parent) : QWidget(parent) {
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("Sheet"), QStringLiteral("Drawing")});
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &SheetSetPanel::onItemDoubleClicked);

    auto* hint = new QLabel(QStringLiteral("Double-click a sheet to open it (replaces the current drawing)."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888;"));

    auto* addButton = new QPushButton(QStringLiteral("Add Sheet..."), this);
    connect(addButton, &QPushButton::clicked, this, &SheetSetPanel::onAddSheet);
    auto* removeButton = new QPushButton(QStringLiteral("Remove Sheet"), this);
    connect(removeButton, &QPushButton::clicked, this, &SheetSetPanel::onRemoveSheet);
    auto* newButton = new QPushButton(QStringLiteral("New Set"), this);
    connect(newButton, &QPushButton::clicked, this, &SheetSetPanel::onNewSet);
    auto* saveButton = new QPushButton(QStringLiteral("Save Set..."), this);
    connect(saveButton, &QPushButton::clicked, this, &SheetSetPanel::onSaveSet);
    auto* loadButton = new QPushButton(QStringLiteral("Load Set..."), this);
    connect(loadButton, &QPushButton::clicked, this, &SheetSetPanel::onLoadSet);
    auto* publishButton = new QPushButton(QStringLiteral("Publish..."), this);
    connect(publishButton, &QPushButton::clicked, this, &SheetSetPanel::onPublish);

    auto* buttonRow1 = new QHBoxLayout();
    buttonRow1->addWidget(addButton);
    buttonRow1->addWidget(removeButton);
    auto* buttonRow2 = new QHBoxLayout();
    buttonRow2->addWidget(newButton);
    buttonRow2->addWidget(saveButton);
    buttonRow2->addWidget(loadButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_tree);
    layout->addLayout(buttonRow1);
    layout->addLayout(buttonRow2);
    layout->addWidget(publishButton);
}

void SheetSetPanel::refreshTree() {
    m_tree->clear();
    for (const lcad::SheetSubset& subset : m_set.subsets) {
        auto* subsetItem = new QTreeWidgetItem(m_tree, {QString::fromStdString(subset.name)});
        QFont font = subsetItem->font(0);
        font.setBold(true);
        subsetItem->setFont(0, font);
        subsetItem->setFlags(subsetItem->flags() & ~Qt::ItemIsSelectable);
        for (const lcad::SheetSetEntry& sheet : subset.sheets) {
            const QString layoutPart =
                sheet.layoutName.empty() ? QStringLiteral("Model") : QString::fromStdString(sheet.layoutName);
            const QString label = QString::fromStdString(sheet.sheetNumber) + QStringLiteral(" - ") +
                                  QString::fromStdString(sheet.sheetTitle);
            const QString drawingPart =
                QFileInfo(QString::fromStdString(sheet.drawingPath)).fileName() + QStringLiteral(" — ") + layoutPart;
            auto* sheetItem = new QTreeWidgetItem(subsetItem, {label, drawingPart});
            sheetItem->setData(0, Qt::UserRole, QString::fromStdString(sheet.sheetNumber));
        }
    }
    m_tree->expandAll();
}

void SheetSetPanel::onAddSheet() {
    const QString filter = QStringLiteral("Drawings (*.dxf *.dwg);;DXF Files (*.dxf);;DWG Files (*.dwg)");
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Add Sheet"), QString(), filter);
    if (path.isEmpty()) return;

    const QStringList layouts = peekLayoutNames(path);
    QStringList options{QStringLiteral("(Model space)")};
    options << layouts;
    bool ok = false;
    const QString chosenLayout =
        QInputDialog::getItem(this, QStringLiteral("Add Sheet"), QStringLiteral("Layout:"), options, 0, false, &ok);
    if (!ok) return;
    const QString layoutName = chosenLayout == QStringLiteral("(Model space)") ? QString() : chosenLayout;

    QStringList subsetNames;
    for (const lcad::SheetSubset& subset : m_set.subsets) subsetNames << QString::fromStdString(subset.name);
    if (subsetNames.isEmpty()) subsetNames << QStringLiteral("Sheets");
    const QString subsetName = QInputDialog::getItem(this, QStringLiteral("Add Sheet"),
                                                      QStringLiteral("Subset (existing or new):"), subsetNames, 0,
                                                      true, &ok);
    if (!ok || subsetName.isEmpty()) return;

    const QString number =
        QInputDialog::getText(this, QStringLiteral("Add Sheet"), QStringLiteral("Sheet number:"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok || number.isEmpty()) return;
    if (m_set.findSheet(number.toStdString()) != nullptr) {
        QMessageBox::warning(this, QStringLiteral("Add Sheet"),
                             QStringLiteral("A sheet numbered \"%1\" already exists in this set.").arg(number));
        return;
    }

    const QString defaultTitle =
        layoutName.isEmpty() ? QFileInfo(path).baseName() : QFileInfo(path).baseName() + QStringLiteral(" - ") + layoutName;
    const QString title =
        QInputDialog::getText(this, QStringLiteral("Add Sheet"), QStringLiteral("Sheet title:"), QLineEdit::Normal,
                              defaultTitle, &ok);
    if (!ok || title.isEmpty()) return;

    lcad::addSheet(m_set, subsetName.toStdString(),
                   {number.toStdString(), title.toStdString(), path.toStdString(), layoutName.toStdString()});
    refreshTree();
}

void SheetSetPanel::onRemoveSheet() {
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || !item->parent()) return; // no selection, or a subset header (not a sheet)
    const std::string number = item->data(0, Qt::UserRole).toString().toStdString();
    lcad::removeSheet(m_set, number);
    refreshTree();
}

void SheetSetPanel::onNewSet() {
    m_set = lcad::SheetSet();
    m_setPath.clear();
    refreshTree();
}

void SheetSetPanel::onSaveSet() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Sheet Set"), m_setPath,
                                                       QStringLiteral("KumCAD Sheet Set (*.kss)"));
    if (path.isEmpty()) return;
    std::string error;
    if (!lcad::saveSheetSet(m_set, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Save Sheet Set"), QString::fromStdString(error));
        return;
    }
    m_setPath = path;
}

void SheetSetPanel::onLoadSet() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load Sheet Set"), m_setPath,
                                                       QStringLiteral("KumCAD Sheet Set (*.kss)"));
    if (path.isEmpty()) return;
    lcad::SheetSet loaded;
    std::string error;
    if (!lcad::loadSheetSet(loaded, path.toStdString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Load Sheet Set"), QString::fromStdString(error));
        return;
    }
    m_set = std::move(loaded);
    m_setPath = path;
    refreshTree();
}

void SheetSetPanel::onPublish() {
    if (m_set.sheetCount() == 0) {
        QMessageBox::information(this, QStringLiteral("Publish"), QStringLiteral("The sheet set is empty."));
        return;
    }
    QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Publish Sheet Set"), QString(), QStringLiteral("PDF Files (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);

    QPainter painter(&printer);
    int published = 0;
    QStringList failed;
    bool firstPage = true;
    for (const lcad::SheetSubset& subset : m_set.subsets) {
        for (const lcad::SheetSetEntry& sheet : subset.sheets) {
            lcad::Document scratch;
            const QString filePath = QString::fromStdString(sheet.drawingPath);
            const bool isDwg = filePath.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive);
            const bool ok = isDwg ? lcad::readDwg(scratch, sheet.drawingPath) : lcad::readDxf(scratch, sheet.drawingPath);
            if (!ok) {
                failed << QString::fromStdString(sheet.sheetNumber);
                continue;
            }
            const lcad::Layout* layout = nullptr;
            if (!sheet.layoutName.empty()) {
                for (const lcad::Layout& l : scratch.layouts()) {
                    if (l.name == sheet.layoutName) {
                        layout = &l;
                        break;
                    }
                }
            }
            if (!firstPage) printer.newPage();
            firstPage = false;
            renderDocumentPage(painter, printer.resolution(), scratch, layout);
            ++published;
        }
    }
    painter.end();

    if (published == 0) {
        QFile::remove(path); // nothing rendered; don't leave a blank/broken PDF behind
        QMessageBox::warning(this, QStringLiteral("Publish"), QStringLiteral("No sheets could be published."));
        return;
    }

    QString message = QStringLiteral("Published %1 sheet(s) to %2").arg(published).arg(QFileInfo(path).fileName());
    if (!failed.isEmpty()) {
        message += QStringLiteral("\n%1 sheet(s) could not be read: %2")
                       .arg(failed.size())
                       .arg(failed.join(QStringLiteral(", ")));
    }
    QMessageBox::information(this, QStringLiteral("Publish"), message);
}

void SheetSetPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    (void)column;
    if (!item || !item->parent()) return; // a subset header, not a sheet
    const std::string number = item->data(0, Qt::UserRole).toString().toStdString();
    const lcad::SheetSetEntry* sheet = m_set.findSheet(number);
    if (!sheet) return;
    emit sheetOpenRequested(QString::fromStdString(sheet->drawingPath), QString::fromStdString(sheet->layoutName));
}
