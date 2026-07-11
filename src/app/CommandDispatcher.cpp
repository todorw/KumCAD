#include "CommandDispatcher.h"

#include "CommandLine.h"
#include "DrawingView.h"
#include "commands/AlignCommand.h"
#include "commands/AttDefCommand.h"
#include "commands/ArcCommand.h"
#include "commands/AreaCommand.h"
#include "commands/ArrayCommand.h"
#include "commands/BlockCommand.h"
#include "commands/BreakCommand.h"
#include "commands/CircleCommand.h"
#include "commands/CopyCommand.h"
#include "commands/DimAngularCommand.h"
#include "commands/DimCommand.h"
#include "commands/DimRadialCommand.h"
#include "commands/DimStyleCommand.h"
#include "commands/DistCommand.h"
#include "commands/EllipseCommand.h"
#include "commands/ExtendCommand.h"
#include "commands/FilletCommand.h"
#include "commands/GroupCommand.h"
#include "commands/IdCommand.h"
#include "commands/HatchCommand.h"
#include "commands/InsertCommand.h"
#include "commands/LayoutCommand.h"
#include "commands/LeaderCommand.h"
#include "commands/LengthenCommand.h"
#include "commands/MLeaderCommand.h"
#include "commands/MLeaderStyleCommand.h"
#include "commands/LineCommand.h"
#include "commands/LtScaleCommand.h"
#include "commands/LweightCommand.h"
#include "commands/MatchPropCommand.h"
#include "commands/OsnapCommand.h"
#include "commands/PageSetupCommand.h"
#include "commands/PolarAngCommand.h"
#include "commands/PointCommands.h"
#include "commands/MTextCommand.h"
#include "commands/MirrorCommand.h"
#include "commands/MviewCommand.h"
#include "commands/MoveCommand.h"
#include "commands/OffsetCommand.h"
#include "commands/PeditCommand.h"
#include "commands/PolylineCommand.h"
#include "commands/RectangCommand.h"
#include "commands/RotateCommand.h"
#include "commands/ScaleCommand.h"
#include "commands/SplineCommand.h"
#include "commands/StretchCommand.h"
#include "commands/StyleCommand.h"
#include "commands/TableCommand.h"
#include "commands/TableEditCommand.h"
#include "commands/TextCommand.h"
#include "commands/TrimCommand.h"
#include "commands/XlineCommand.h"
#include "commands/XrefCommand.h"
#include "commands/VpScaleCommand.h"
#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <QStringList>

CommandDispatcher::CommandDispatcher(lcad::Document& document, CommandLine& commandLine, QObject* parent)
    : QObject(parent), m_document(document), m_commandLine(commandLine) {
    connect(&m_commandLine, &CommandLine::commandEntered, this, &CommandDispatcher::handleCommandText);
}

void CommandDispatcher::startCommand(std::unique_ptr<DrawCommand> command, const QString& name) {
    m_activeCommand = std::move(command);
    m_commandLine.appendLine(name);
    m_commandLine.appendLine(m_activeCommand->start());
}

void CommandDispatcher::finishCommand() {
    m_activeCommand.reset();
    m_commandLine.appendLine(QStringLiteral("Command:"));
    emit documentChanged();
    emit previewChanged();
}

void CommandDispatcher::handleCommandText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_activeCommand) {
        if (m_activeCommand->wantsTextInput()) {
            // Free-text stage (e.g. TEXT's "Enter text:"): route everything here,
            // including an empty Enter, rather than trying to parse it as a point/number.
            const std::optional<QString> prompt = m_activeCommand->onText(trimmed);
            if (m_activeCommand->isFinished()) {
                if (prompt) m_commandLine.appendLine(*prompt); // final result message (e.g. DIST)
                finishCommand();
                return;
            }
            if (prompt) {
                m_commandLine.appendLine(*prompt);
                emit documentChanged();
                return;
            }
            m_commandLine.appendLine(QStringLiteral("*Invalid input*"));
            return;
        }
        if (trimmed.isEmpty()) {
            handleFinishRequested();
            return;
        }
        lcad::Point2D pt;
        if (tryParsePoint(trimmed, pt)) {
            handlePointPicked(pt);
            return;
        }
        bool isNumber = false;
        const double scalar = trimmed.toDouble(&isNumber);
        if (isNumber) {
            const std::optional<QString> prompt = m_activeCommand->onScalar(scalar);
            if (m_activeCommand->isFinished()) {
                if (prompt) m_commandLine.appendLine(*prompt);
                finishCommand();
                return;
            }
            if (prompt) {
                m_commandLine.appendLine(*prompt);
                emit documentChanged();
                return;
            }
        } else {
            // Keyword option like PLINE's "C" (close).
            const std::optional<QString> prompt = m_activeCommand->onOption(trimmed);
            if (m_activeCommand->isFinished()) {
                if (prompt) m_commandLine.appendLine(*prompt);
                finishCommand();
                return;
            }
            if (prompt) {
                m_commandLine.appendLine(*prompt);
                emit documentChanged();
                return;
            }
        }
        m_commandLine.appendLine(QStringLiteral("*Invalid input, expected x,y or a number*"));
        return;
    }

    const QString cmd = trimmed.toUpper();
    if (cmd == QLatin1String("LINE") || cmd == QLatin1String("L")) {
        startCommand(std::make_unique<LineCommand>(m_document), QStringLiteral("LINE"));
    } else if (cmd == QLatin1String("CIRCLE") || cmd == QLatin1String("C")) {
        startCommand(std::make_unique<CircleCommand>(m_document), QStringLiteral("CIRCLE"));
    } else if (cmd == QLatin1String("ARC") || cmd == QLatin1String("A")) {
        startCommand(std::make_unique<ArcCommand>(m_document), QStringLiteral("ARC"));
    } else if (cmd == QLatin1String("PLINE") || cmd == QLatin1String("PL")) {
        startCommand(std::make_unique<PolylineCommand>(m_document), QStringLiteral("PLINE"));
    } else if (cmd == QLatin1String("SPLINE") || cmd == QLatin1String("SPL")) {
        startCommand(std::make_unique<SplineCommand>(m_document), QStringLiteral("SPLINE"));
    } else if (cmd == QLatin1String("ELLIPSE") || cmd == QLatin1String("EL")) {
        startCommand(std::make_unique<EllipseCommand>(m_document), QStringLiteral("ELLIPSE"));
    } else if (cmd == QLatin1String("RECTANG") || cmd == QLatin1String("RECTANGLE") || cmd == QLatin1String("REC")) {
        startCommand(std::make_unique<RectangCommand>(m_document), QStringLiteral("RECTANG"));
    } else if (cmd == QLatin1String("TEXT") || cmd == QLatin1String("DT")) {
        startCommand(std::make_unique<TextCommand>(m_document), QStringLiteral("TEXT"));
    } else if (cmd == QLatin1String("MTEXT") || cmd == QLatin1String("MT") || cmd == QLatin1String("T")) {
        startCommand(std::make_unique<MTextCommand>(m_document), QStringLiteral("MTEXT"));
    } else if (cmd == QLatin1String("MOVE") || cmd == QLatin1String("M")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<MoveCommand>(m_document, ids), QStringLiteral("MOVE"));
    } else if (cmd == QLatin1String("COPY") || cmd == QLatin1String("CO") || cmd == QLatin1String("CP")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<CopyCommand>(m_document, ids), QStringLiteral("COPY"));
    } else if (cmd == QLatin1String("ROTATE") || cmd == QLatin1String("RO")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<RotateCommand>(m_document, ids), QStringLiteral("ROTATE"));
    } else if (cmd == QLatin1String("SCALE") || cmd == QLatin1String("SC")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<ScaleCommand>(m_document, ids), QStringLiteral("SCALE"));
    } else if (cmd == QLatin1String("MIRROR") || cmd == QLatin1String("MI")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<MirrorCommand>(m_document, ids), QStringLiteral("MIRROR"));
    } else if (cmd == QLatin1String("OFFSET") || cmd == QLatin1String("O")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<OffsetCommand>(m_document, ids), QStringLiteral("OFFSET"));
    } else if (cmd == QLatin1String("PEDIT") || cmd == QLatin1String("PE")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<PeditCommand>(m_document, ids), QStringLiteral("PEDIT"));
    } else if (cmd == QLatin1String("STRETCH") || cmd == QLatin1String("S")) {
        startCommand(std::make_unique<StretchCommand>(m_document), QStringLiteral("STRETCH"));
    } else if (cmd == QLatin1String("LENGTHEN") || cmd == QLatin1String("LEN")) {
        startCommand(std::make_unique<LengthenCommand>(m_document, pickTolerance()), QStringLiteral("LENGTHEN"));
    } else if (cmd == QLatin1String("BREAK") || cmd == QLatin1String("BR")) {
        startCommand(std::make_unique<BreakCommand>(m_document, pickTolerance()), QStringLiteral("BREAK"));
    } else if (cmd == QLatin1String("ALIGN") || cmd == QLatin1String("AL")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<AlignCommand>(m_document, ids), QStringLiteral("ALIGN"));
    } else if (cmd == QLatin1String("OSNAP") || cmd == QLatin1String("OS")) {
        if (m_view) startCommand(std::make_unique<OsnapCommand>(*m_view), QStringLiteral("OSNAP"));
    } else if (cmd == QLatin1String("POLARANG")) {
        if (m_view) startCommand(std::make_unique<PolarAngCommand>(*m_view), QStringLiteral("POLARANG"));
    } else if (cmd == QLatin1String("TRIM") || cmd == QLatin1String("TR")) {
        // Empty selection = every entity is a cutting edge (quick-trim style).
        const std::vector<lcad::EntityId> ids = m_view ? m_view->selectedIds() : std::vector<lcad::EntityId>{};
        startCommand(std::make_unique<TrimCommand>(m_document, ids, pickTolerance()), QStringLiteral("TRIM"));
    } else if (cmd == QLatin1String("EXTEND") || cmd == QLatin1String("EX")) {
        const std::vector<lcad::EntityId> ids = m_view ? m_view->selectedIds() : std::vector<lcad::EntityId>{};
        startCommand(std::make_unique<ExtendCommand>(m_document, ids, pickTolerance()), QStringLiteral("EXTEND"));
    } else if (cmd == QLatin1String("FILLET") || cmd == QLatin1String("F")) {
        std::vector<lcad::EntityId> lineIds;
        if (m_view) {
            for (lcad::EntityId id : m_view->selectedIds()) {
                const lcad::Entity* e = m_document.findEntity(id);
                if (e && e->type() == lcad::EntityType::Line) lineIds.push_back(id);
            }
        }
        if (lineIds.size() == 2) {
            startCommand(std::make_unique<FilletCommand>(m_document, lineIds[0], lineIds[1]), QStringLiteral("FILLET"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*Select exactly two lines first, then run FILLET*"));
        }
    } else if (cmd == QLatin1String("DIMLINEAR") || cmd == QLatin1String("DLI")) {
        startCommand(std::make_unique<DimCommand>(m_document, false), QStringLiteral("DIMLINEAR"));
    } else if (cmd == QLatin1String("DIMALIGNED") || cmd == QLatin1String("DAL")) {
        startCommand(std::make_unique<DimCommand>(m_document, true), QStringLiteral("DIMALIGNED"));
    } else if (cmd == QLatin1String("DIMRADIUS") || cmd == QLatin1String("DRA")) {
        startCommand(std::make_unique<DimRadialCommand>(m_document, false, pickTolerance()),
                     QStringLiteral("DIMRADIUS"));
    } else if (cmd == QLatin1String("DIMDIAMETER") || cmd == QLatin1String("DDI")) {
        startCommand(std::make_unique<DimRadialCommand>(m_document, true, pickTolerance()),
                     QStringLiteral("DIMDIAMETER"));
    } else if (cmd == QLatin1String("DIMANGULAR") || cmd == QLatin1String("DAN")) {
        startCommand(std::make_unique<DimAngularCommand>(m_document), QStringLiteral("DIMANGULAR"));
    } else if (cmd == QLatin1String("DIMSTYLE") || cmd == QLatin1String("D")) {
        startCommand(std::make_unique<DimStyleCommand>(m_document), QStringLiteral("DIMSTYLE"));
    } else if (cmd == QLatin1String("STYLE") || cmd == QLatin1String("ST")) {
        startCommand(std::make_unique<StyleCommand>(m_document), QStringLiteral("STYLE"));
    } else if (cmd == QLatin1String("LAYOUT") || cmd == QLatin1String("LO")) {
        const int active = m_view ? m_view->activeLayoutIndex() : -1;
        startCommand(std::make_unique<LayoutCommand>(m_document, active), QStringLiteral("LAYOUT"));
    } else if (cmd == QLatin1String("PAGESETUP")) {
        if (m_view && m_view->inLayoutMode()) {
            startCommand(std::make_unique<PageSetupCommand>(m_document, m_view->activeLayoutIndex()),
                         QStringLiteral("PAGESETUP"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*PAGESETUP works on a layout tab*"));
        }
    } else if (cmd == QLatin1String("LEADER") || cmd == QLatin1String("LEAD") || cmd == QLatin1String("LE")) {
        startCommand(std::make_unique<LeaderCommand>(m_document), QStringLiteral("LEADER"));
    } else if (cmd == QLatin1String("MLEADER") || cmd == QLatin1String("MLD")) {
        startCommand(std::make_unique<MLeaderCommand>(m_document), QStringLiteral("MLEADER"));
    } else if (cmd == QLatin1String("MLEADERSTYLE") || cmd == QLatin1String("MLS")) {
        startCommand(std::make_unique<MLeaderStyleCommand>(m_document), QStringLiteral("MLEADERSTYLE"));
    } else if (cmd == QLatin1String("TABLE") || cmd == QLatin1String("TB")) {
        startCommand(std::make_unique<TableCommand>(m_document), QStringLiteral("TABLE"));
    } else if (cmd == QLatin1String("TABLEDIT") || cmd == QLatin1String("TED")) {
        startCommand(std::make_unique<TableEditCommand>(m_document), QStringLiteral("TABLEDIT"));
    } else if (cmd == QLatin1String("HATCH") || cmd == QLatin1String("H")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<HatchCommand>(m_document, ids), QStringLiteral("HATCH"));
    } else if (cmd == QLatin1String("BLOCK") || cmd == QLatin1String("B")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<BlockCommand>(m_document, ids), QStringLiteral("BLOCK"));
    } else if (cmd == QLatin1String("INSERT") || cmd == QLatin1String("I")) {
        startCommand(std::make_unique<InsertCommand>(m_document), QStringLiteral("INSERT"));
    } else if (cmd == QLatin1String("ARRAY") || cmd == QLatin1String("AR")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<ArrayCommand>(m_document, ids), QStringLiteral("ARRAY"));
    } else if (cmd == QLatin1String("POINT") || cmd == QLatin1String("PO")) {
        startCommand(std::make_unique<PointCommand>(m_document), QStringLiteral("POINT"));
    } else if (cmd == QLatin1String("PDMODE") || cmd == QLatin1String("PTYPE")) {
        startCommand(std::make_unique<PdModeCommand>(m_document), QStringLiteral("PDMODE"));
    } else if (cmd == QLatin1String("DIVIDE") || cmd == QLatin1String("DIV")) {
        startCommand(std::make_unique<DivideCommand>(m_document, pickTolerance(), false), QStringLiteral("DIVIDE"));
    } else if (cmd == QLatin1String("MEASURE") || cmd == QLatin1String("ME")) {
        startCommand(std::make_unique<DivideCommand>(m_document, pickTolerance(), true), QStringLiteral("MEASURE"));
    } else if (cmd == QLatin1String("XLINE") || cmd == QLatin1String("XL")) {
        startCommand(std::make_unique<XlineCommand>(m_document, false), QStringLiteral("XLINE"));
    } else if (cmd == QLatin1String("RAY")) {
        startCommand(std::make_unique<XlineCommand>(m_document, true), QStringLiteral("RAY"));
    } else if (cmd == QLatin1String("GROUP") || cmd == QLatin1String("G")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<GroupCommand>(m_document, ids, false), QStringLiteral("GROUP"));
    } else if (cmd == QLatin1String("UNGROUP")) {
        startCommand(std::make_unique<GroupCommand>(m_document, std::vector<lcad::EntityId>{}, true),
                     QStringLiteral("UNGROUP"));
    } else if (cmd == QLatin1String("MATCHPROP") || cmd == QLatin1String("MA")) {
        startCommand(std::make_unique<MatchPropCommand>(m_document, pickTolerance()), QStringLiteral("MATCHPROP"));
    } else if (cmd == QLatin1String("LWEIGHT") || cmd == QLatin1String("LW")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<LweightCommand>(m_document, ids), QStringLiteral("LWEIGHT"));
    } else if (cmd == QLatin1String("LWDISPLAY")) {
        if (m_view) {
            m_view->setLineweightDisplay(!m_view->lineweightDisplay());
            m_commandLine.appendLine(m_view->lineweightDisplay() ? QStringLiteral("*Lineweight display on*")
                                                                 : QStringLiteral("*Lineweight display off*"));
        }
    } else if (cmd == QLatin1String("ATTDEF") || cmd == QLatin1String("ATT")) {
        startCommand(std::make_unique<AttDefCommand>(m_document), QStringLiteral("ATTDEF"));
    } else if (cmd == QLatin1String("ID")) {
        startCommand(std::make_unique<IdCommand>(), QStringLiteral("ID"));
    } else if (cmd == QLatin1String("PURGE") || cmd == QLatin1String("PU")) {
        const lcad::Document::PurgeResult purged = m_document.purge();
        m_commandLine.appendLine(QStringLiteral("*Purged %1 block(s) and %2 layer(s)*")
                                     .arg(purged.blocks)
                                     .arg(purged.layers));
        emit documentChanged();
    } else if (cmd == QLatin1String("REGEN") || cmd == QLatin1String("RE")) {
        emit documentChanged();
        m_commandLine.appendLine(QStringLiteral("*Regenerated*"));
    } else if (cmd == QLatin1String("XREF") || cmd == QLatin1String("XR")) {
        startCommand(std::make_unique<XrefCommand>(m_document), QStringLiteral("XREF"));
    } else if (cmd == QLatin1String("EXPLODE") || cmd == QLatin1String("X")) {
        explodeSelection();
    } else if (cmd == QLatin1String("AREA") || cmd == QLatin1String("AA")) {
        startCommand(std::make_unique<AreaCommand>(), QStringLiteral("AREA"));
    } else if (cmd == QLatin1String("DIST") || cmd == QLatin1String("DI")) {
        startCommand(std::make_unique<DistCommand>(), QStringLiteral("DIST"));
    } else if (cmd == QLatin1String("MVIEW") || cmd == QLatin1String("MV")) {
        if (m_view && m_view->inLayoutMode()) {
            startCommand(std::make_unique<MviewCommand>(m_document, m_view->activeLayoutIndex()),
                         QStringLiteral("MVIEW"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*MVIEW only works in a layout tab*"));
        }
    } else if (cmd == QLatin1String("VPSCALE") || cmd == QLatin1String("VPS")) {
        if (m_view && m_view->inLayoutMode() && m_view->selectedViewportIndex() >= 0) {
            startCommand(std::make_unique<VpScaleCommand>(m_document, m_view->activeLayoutIndex(),
                                                          m_view->selectedViewportIndex()),
                         QStringLiteral("VPSCALE"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*Select a viewport in a layout tab first*"));
        }
    } else if (cmd == QLatin1String("LTSCALE") || cmd == QLatin1String("LTS")) {
        startCommand(std::make_unique<LtScaleCommand>(m_document), QStringLiteral("LTSCALE"));
    } else if (cmd == QLatin1String("ZOOM") || cmd == QLatin1String("Z")) {
        if (m_view) {
            m_view->zoomExtents();
            m_commandLine.appendLine(QStringLiteral("*Zoom extents*"));
        }
    } else if (cmd == QLatin1String("ERASE") || cmd == QLatin1String("E")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) {
            m_view->eraseSelection();
            m_commandLine.appendLine(QStringLiteral("*%1 erased*").arg(ids.size()));
            emit documentChanged();
        }
    } else if (cmd == QLatin1String("UNDO") || cmd == QLatin1String("U")) {
        undo();
    } else if (cmd == QLatin1String("REDO")) {
        redo();
    } else if (!cmd.isEmpty()) {
        m_commandLine.appendLine(QStringLiteral("*Unknown command \"%1\"*").arg(trimmed));
    }
}

void CommandDispatcher::handlePointPicked(const lcad::Point2D& pt, const std::optional<lcad::SnapRef>& snapRef) {
    if (!m_activeCommand) return;
    m_activeCommand->onSnapContext(snapRef);
    const std::optional<QString> prompt = m_activeCommand->onPoint(pt);
    if (m_activeCommand->isFinished()) {
        if (prompt) m_commandLine.appendLine(*prompt);
        finishCommand();
        return;
    }
    if (prompt) m_commandLine.appendLine(*prompt);
    emit documentChanged();
}

void CommandDispatcher::handleMouseMoved(const lcad::Point2D& pt) {
    if (!m_activeCommand) return;
    m_activeCommand->onPreviewPoint(pt);
    emit previewChanged();
}

void CommandDispatcher::handleFinishRequested() {
    if (!m_activeCommand) return;
    const bool accepted = m_activeCommand->requestFinish();
    if (accepted && !m_activeCommand->isFinished()) {
        // Multi-phase command moving to its next phase (e.g. LEADER's Enter
        // ending the points and starting the annotation) rather than ending.
        if (const auto message = m_activeCommand->resultMessage()) m_commandLine.appendLine(*message);
        emit previewChanged();
        return;
    }
    if (const auto message = m_activeCommand->resultMessage()) m_commandLine.appendLine(*message);
    finishCommand();
}

void CommandDispatcher::handleEscape() {
    if (!m_activeCommand) return;
    m_activeCommand->cancel();
    finishCommand();
}

void CommandDispatcher::undo() {
    m_document.commandStack().undo();
    m_commandLine.appendLine(QStringLiteral("*Undo*"));
    emit documentChanged();
}

void CommandDispatcher::redo() {
    m_document.commandStack().redo();
    m_commandLine.appendLine(QStringLiteral("*Redo*"));
    emit documentChanged();
}

double CommandDispatcher::pickTolerance() const {
    return m_view ? m_view->pickToleranceWorld() : 0.5;
}

void CommandDispatcher::explodeSelection() {
    const std::vector<lcad::EntityId> ids = selectionForModify();
    if (ids.empty()) return;

    auto batch = std::make_unique<lcad::BatchCommand>("Explode");
    int made = 0;
    int skipped = 0;
    for (lcad::EntityId id : ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;
        if (e->type() == lcad::EntityType::Insert) {
            const auto& insert = static_cast<const lcad::InsertEntity&>(*e);
            batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
            for (auto& child : insert.instantiate()) {
                child->setId(m_document.reserveEntityId());
                batch->add(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(child)));
            }
            ++made;
        } else if (e->type() == lcad::EntityType::Polyline) {
            const auto& pl = static_cast<const lcad::PolylineEntity&>(*e);
            batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
            pl.forEachSegment([&](const lcad::Point2D& a, const lcad::Point2D& b, double bulge) {
                if (const auto arc = lcad::bulgeToArc(a, b, bulge)) {
                    // Our ArcEntity always sweeps CCW, so a clockwise bulge
                    // becomes the same arc traversed from the other end.
                    const double lo = arc->sweep >= 0 ? arc->startAngle : arc->startAngle + arc->sweep;
                    batch->add(std::make_unique<lcad::AddEntityCommand>(
                        m_document,
                        std::make_unique<lcad::ArcEntity>(m_document.reserveEntityId(), e->layer(), arc->center,
                                                          arc->radius, lo, lo + std::abs(arc->sweep))));
                } else {
                    batch->add(std::make_unique<lcad::AddEntityCommand>(
                        m_document,
                        std::make_unique<lcad::LineEntity>(m_document.reserveEntityId(), e->layer(), a, b)));
                }
            });
            ++made;
        } else {
            ++skipped;
        }
    }
    if (!batch->empty()) {
        m_document.commandStack().execute(std::move(batch));
        emit documentChanged();
    }
    if (skipped > 0) {
        m_commandLine.appendLine(
            QStringLiteral("*%1 exploded, %2 skipped (blocks and polylines only)*").arg(made).arg(skipped));
    } else {
        m_commandLine.appendLine(QStringLiteral("*%1 exploded*").arg(made));
    }
}

std::vector<lcad::EntityId> CommandDispatcher::selectionForModify() const {
    if (!m_view || !m_view->hasSelection()) {
        m_commandLine.appendLine(QStringLiteral("*Select objects first, then run this command*"));
        return {};
    }
    return m_view->selectedIds();
}

bool CommandDispatcher::tryParsePoint(const QString& text, lcad::Point2D& out) {
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 2) return false;
    bool okX = false;
    bool okY = false;
    const double x = parts[0].trimmed().toDouble(&okX);
    const double y = parts[1].trimmed().toDouble(&okY);
    if (!okX || !okY) return false;
    out = lcad::Point2D(x, y);
    return true;
}
