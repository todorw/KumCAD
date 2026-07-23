#include "CommandDispatcher.h"

#include "CommandLine.h"
#include "DrawingView.h"
#ifdef LCAD_HAS_OCCT
#include "Board3DWindow.h"
#endif
#include "commands/AlignCommand.h"
#include "commands/AnnoScaleCommand.h"
#include "commands/AttDefCommand.h"
#include "commands/ArcCommand.h"
#include "commands/AreaCommand.h"
#include "commands/ArrayCommand.h"
#include "commands/BlockCommand.h"
#include "commands/BlockParamCommand.h"
#include "commands/BreakCommand.h"
#include "commands/CircleCommand.h"
#include "commands/CopyCommand.h"
#include "commands/ClipboardPasteCommand.h"
#include "commands/DataLinkCommand.h"
#include "commands/LayerStateCommand.h"
#include "commands/TCaseCommand.h"
#include "commands/TranCommand.h"
#include "commands/LengthTuneCommand.h"
#include "commands/DiffPairCommand.h"
#include "commands/WipeoutCommand.h"
#include "commands/LayTransCommand.h"
#include "commands/AuditCommand.h"
#include "commands/NCopyCommand.h"
#include "commands/TextFitCommand.h"
#include "commands/RevcloudCommand.h"
#include "commands/LibraryCommands.h"
#include "commands/ExpressToolCommands.h"
#include "commands/DimAngularCommand.h"
#include "commands/DimCommand.h"
#include "commands/DimRadialCommand.h"
#include "commands/DimStyleCommand.h"
#include "commands/DistCommand.h"
#include "commands/EllipseCommand.h"
#include "commands/ExtendCommand.h"
#include "commands/FilletCommand.h"
#include "commands/GeoLocationCommand.h"
#include "commands/FindCommand.h"
#include "commands/GradientCommand.h"
#include "commands/GroupCommand.h"
#include "commands/IdCommand.h"
#include "commands/ImageAttachCommand.h"
#include "commands/BoundaryCommand.h"
#include "commands/HatchCommand.h"
#include "commands/InsertCommand.h"
#include "commands/LayoutCommand.h"
#include "commands/LeaderCommand.h"
#include "commands/LengthenCommand.h"
#include "commands/MLeaderCommand.h"
#include "commands/MLeaderStyleCommand.h"
#include "commands/CamCommands.h"
#include "commands/CivilCommands.h"
#include "commands/PcbCommands.h"
#include "commands/ParametricCommands.h"
#include "commands/SchematicCommands.h"
#include "commands/WireCommand.h"
#include "commands/LineCommand.h"
#include "commands/LtScaleCommand.h"
#include "commands/LweightCommand.h"
#include "commands/MatchPropCommand.h"
#include "commands/OsnapCommand.h"
#include "commands/PageSetupCommand.h"
#include "commands/PolarAngCommand.h"
#include "commands/PointCloudAttachCommand.h"
#include "commands/PointCommands.h"
#include "commands/MTextCommand.h"
#include "commands/QSelectCommand.h"
#include "commands/QuickCalcCommand.h"
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
#include "commands/DataExtractionCommand.h"
#include "commands/FieldCommand.h"
#include "commands/TextCommand.h"
#include "commands/TrimCommand.h"
#include "commands/XlineCommand.h"
#include "commands/XrefCommand.h"
#include "commands/VpScaleCommand.h"
#include "core/document/Commands.h"
#include "core/document/Fields.h"
#include "core/document/ExpressTools.h"
#include "core/util/Script.h"
#include "core/geometry/MText.h"
#include <set>
#include "core/geometry/Text.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/ModifyOps.h"
#include "core/geometry/Polyline.h"
#include "core/electrical/WireNumbering.h"
#include "core/pcb/Drc.h"
#include "core/pcb/Teardrop.h"
#include "core/pid/InstrumentTagging.h"
#include "core/schematic/Erc.h"
#include "core/schematic/Netlist.h"
#include "core/schematic/Sheets.h"
#include "core/schematic/Spice.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QIODevice>
#include <QImage>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>

CommandDispatcher::CommandDispatcher(lcad::Document& document, CommandLine& commandLine, QObject* parent)
    : QObject(parent), m_document(document), m_commandLine(commandLine),
      m_lisp(
          [this](const std::string& s) { handleCommandText(QString::fromStdString(s)); }, &m_document,
          [this](const std::string& prompt, const std::vector<std::string>& keywords, bool isPoint) {
              return waitForLispInput(prompt, keywords, isPoint);
          }) {
    m_aliases.load();
    connect(&m_commandLine, &CommandLine::commandEntered, this, &CommandDispatcher::handleCommandText);
}

std::optional<std::string> CommandDispatcher::waitForLispInput(const std::string& prompt,
                                                                const std::vector<std::string>& keywords,
                                                                bool isPoint) {
    m_commandLine.appendLine(QString::fromStdString(prompt));
    m_lispWaitIsPoint = isPoint;
    m_lispWaitIsKeyword = !keywords.empty();
    m_lispKeywords.clear();
    for (const std::string& k : keywords) m_lispKeywords.push_back(QString::fromStdString(k));
    m_lispInputResult.reset();
    m_lispWaitCancelled = false;

    QEventLoop loop;
    m_lispLoop = &loop;
    loop.exec();
    m_lispLoop = nullptr;
    m_lispWaitIsPoint = false;
    m_lispWaitIsKeyword = false;
    m_lispKeywords.clear();

    if (m_lispWaitCancelled) return std::nullopt;
    return m_lispInputResult;
}

void CommandDispatcher::startCommand(std::unique_ptr<DrawCommand> command, const QString& name) {
    m_activeCommand = std::move(command);
    m_commandLine.appendLine(name);
    m_commandLine.appendLine(m_activeCommand->start());
}

void CommandDispatcher::finishCommand() {
    if (m_view) {
        if (auto selection = m_activeCommand->resultSelection()) m_view->setSelection(*selection);
    }
    m_activeCommand.reset();
    m_commandLine.appendLine(QStringLiteral("Command:"));
    emit documentChanged();
    emit previewChanged();
}

void CommandDispatcher::handleCommandText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_recording) {
        // ACTSTOP itself (typed at the top-level prompt, not mid-command)
        // ends the recording rather than becoming part of it.
        const bool isActstop =
            !m_activeCommand && trimmed.compare(QLatin1String("ACTSTOP"), Qt::CaseInsensitive) == 0;
        if (!isActstop) m_recordingBuffer << text;
    }

    if (m_lispLoop) {
        if (m_lispWaitIsKeyword) {
            const QString* match = nullptr;
            for (const QString& kw : m_lispKeywords) {
                if (kw.compare(trimmed, Qt::CaseInsensitive) == 0) {
                    match = &kw;
                    break;
                }
            }
            if (!match) {
                m_commandLine.appendLine(
                    QStringLiteral("*Invalid option, expected one of: %1*").arg(m_lispKeywords.join(QStringLiteral("/"))));
                return;
            }
            m_lispInputResult = match->toStdString();
        } else {
            m_lispInputResult = trimmed.toStdString();
        }
        m_lispLoop->quit();
        return;
    }

    if (m_awaitingScriptPath) {
        m_awaitingScriptPath = false;
        if (trimmed.isEmpty()) {
            m_commandLine.appendLine(QStringLiteral("*Cancelled*"));
        } else {
            runScriptFile(trimmed);
        }
        return;
    }

    if (m_awaitingActSavePath) {
        m_awaitingActSavePath = false;
        if (trimmed.isEmpty()) {
            m_commandLine.appendLine(QStringLiteral("*Cancelled*"));
        } else if (m_lastMacro.isEmpty()) {
            m_commandLine.appendLine(QStringLiteral("*No recorded macro yet -- ACTRECORD/ACTSTOP first*"));
        } else {
            QFile file(trimmed);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                m_commandLine.appendLine(QStringLiteral("*Cannot write \"%1\"*").arg(trimmed));
            } else {
                file.write(m_lastMacro.join(QStringLiteral("\n")).toUtf8());
                file.write("\n");
                m_commandLine.appendLine(
                    QStringLiteral("*Saved %1 step(s) to %2*").arg(m_lastMacro.size()).arg(trimmed));
            }
        }
        return;
    }

    if (m_awaitingActLoadPath) {
        m_awaitingActLoadPath = false;
        if (trimmed.isEmpty()) {
            m_commandLine.appendLine(QStringLiteral("*Cancelled*"));
        } else {
            QFile file(trimmed);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_commandLine.appendLine(QStringLiteral("*Cannot open \"%1\"*").arg(trimmed));
            } else {
                QStringList loaded;
                for (const QByteArray& lineBytes : file.readAll().split('\n')) {
                    const QString line = QString::fromUtf8(lineBytes);
                    if (!line.trimmed().isEmpty()) loaded << line;
                }
                m_lastMacro = loaded;
                m_commandLine.appendLine(
                    QStringLiteral("*Loaded %1 step(s) from %2 -- PLAY to replay*").arg(loaded.size()).arg(trimmed));
            }
        }
        return;
    }

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
            handlePointPicked(pt, std::nullopt, false); // raw text already recorded above
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

    if (trimmed.startsWith(QLatin1Char('('))) {
        const auto report = [this](const lcad::LispInterpreter::RunResult& result) {
            if (!result.output.empty()) m_commandLine.appendLine(QString::fromStdString(result.output));
            if (result.ok) {
                m_commandLine.appendLine(QStringLiteral("*= %1*").arg(QString::fromStdString(result.resultText)));
            } else {
                m_commandLine.appendLine(
                    QStringLiteral("*AutoLISP error: %1*").arg(QString::fromStdString(result.error)));
            }
            emit documentChanged();
        };

        // (load "path") is the idiomatic way to bring in a script; read the
        // file and run its contents instead of evaluating the call itself.
        static const QRegularExpression loadPattern(QStringLiteral(R"RX(^\(\s*load\s+"([^"]*)"\s*\)$)RX"),
                                                     QRegularExpression::CaseInsensitiveOption);
        if (const auto match = loadPattern.match(trimmed); match.hasMatch()) {
            QFile file(match.captured(1));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_commandLine.appendLine(QStringLiteral("*Cannot open \"%1\"*").arg(match.captured(1)));
                return;
            }
            report(m_lisp.run(QString::fromUtf8(file.readAll()).toStdString()));
            return;
        }

        report(m_lisp.run(trimmed.toStdString()));
        return;
    }

    QString cmd = trimmed.toUpper();
    if (const auto custom = m_aliases.resolve(cmd)) cmd = *custom;
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
    } else if (cmd == QLatin1String("FIELD")) {
        startCommand(std::make_unique<FieldCommand>(m_document, m_documentFileName), QStringLiteral("FIELD"));
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
    } else if (cmd == QLatin1String("GCHORIZONTAL") || cmd == QLatin1String("GCVERTICAL") ||
             cmd == QLatin1String("GCPARALLEL") || cmd == QLatin1String("GCPERPENDICULAR") ||
             cmd == QLatin1String("GCEQUAL") || cmd == QLatin1String("GCTANGENT") ||
             cmd == QLatin1String("GCMIDPOINT") || cmd == QLatin1String("GCSYMMETRIC")) {
        // Geometric constraints (core/document/DocumentConstraints.h) --
        // no dimension value, so unlike DCRADIUS/DCANGULAR below these
        // apply immediately once the right selection is already made,
        // the same "select first, run command" convention FILLET uses.
        std::vector<lcad::EntityId> lineIds, circleIds, pointIds;
        for (lcad::EntityId id : (m_view ? m_view->selectedIds() : std::vector<lcad::EntityId>{})) {
            const lcad::Entity* e = m_document.findEntity(id);
            if (!e) continue;
            if (e->type() == lcad::EntityType::Line) lineIds.push_back(id);
            else if (e->type() == lcad::EntityType::Circle) circleIds.push_back(id);
            else if (e->type() == lcad::EntityType::Point) pointIds.push_back(id);
        }

        std::optional<lcad::DocumentConstraint> constraint;
        QString errorMessage = QStringLiteral("*Selection doesn't match %1*").arg(cmd);
        if (cmd == QLatin1String("GCHORIZONTAL") || cmd == QLatin1String("GCVERTICAL")) {
            if (lineIds.size() == 1) {
                lcad::DocumentConstraint c;
                c.type = cmd == QLatin1String("GCHORIZONTAL") ? lcad::SketchConstraintType::Horizontal
                                                              : lcad::SketchConstraintType::Vertical;
                c.geomA = lineIds[0];
                constraint = c;
            } else {
                errorMessage = QStringLiteral("*Select exactly one line first*");
            }
        } else if (cmd == QLatin1String("GCPARALLEL") || cmd == QLatin1String("GCPERPENDICULAR") ||
                  cmd == QLatin1String("GCEQUAL")) {
            if (lineIds.size() == 2) {
                lcad::DocumentConstraint c;
                c.type = cmd == QLatin1String("GCPARALLEL")   ? lcad::SketchConstraintType::Parallel
                        : cmd == QLatin1String("GCPERPENDICULAR") ? lcad::SketchConstraintType::Perpendicular
                                                                  : lcad::SketchConstraintType::Equal;
                c.geomA = lineIds[0];
                c.geomB = lineIds[1];
                constraint = c;
            } else {
                errorMessage = QStringLiteral("*Select exactly two lines first*");
            }
        } else if (cmd == QLatin1String("GCTANGENT")) {
            if (lineIds.size() == 1 && circleIds.size() == 1) {
                lcad::DocumentConstraint c;
                c.type = lcad::SketchConstraintType::Tangent;
                c.geomA = lineIds[0];
                c.geomB = circleIds[0];
                constraint = c;
            } else if (circleIds.size() == 2) {
                lcad::DocumentConstraint c;
                c.type = lcad::SketchConstraintType::TangentCircleCircle;
                c.geomA = circleIds[0];
                c.geomB = circleIds[1];
                constraint = c;
            } else {
                errorMessage = QStringLiteral("*Select one line and one circle, or two circles, first*");
            }
        } else if (cmd == QLatin1String("GCMIDPOINT")) {
            if (lineIds.size() == 1 && pointIds.size() == 1) {
                lcad::DocumentConstraint c;
                c.type = lcad::SketchConstraintType::Midpoint;
                c.geomA = lineIds[0];
                c.pointA = {pointIds[0], 0};
                constraint = c;
            } else {
                errorMessage = QStringLiteral("*Select exactly one line and one POINT entity first*");
            }
        } else if (cmd == QLatin1String("GCSYMMETRIC")) {
            if (lineIds.size() == 1 && pointIds.size() == 2) {
                lcad::DocumentConstraint c;
                c.type = lcad::SketchConstraintType::Symmetric;
                c.geomA = lineIds[0];
                c.pointA = {pointIds[0], 0};
                c.pointB = {pointIds[1], 0};
                constraint = c;
            } else {
                errorMessage = QStringLiteral("*Select exactly two POINT entities and one line (the symmetry axis) first*");
            }
        }

        if (constraint) {
            const lcad::DocumentConstraintResult result = lcad::solveDocumentConstraints(m_document, {*constraint});
            m_commandLine.appendLine(result.converged
                                        ? QStringLiteral("*%1 applied*").arg(cmd)
                                        : QStringLiteral("*%1 solved with residual %2 -- may not be fully satisfied*")
                                              .arg(cmd)
                                              .arg(result.finalResidualNorm));
            emit documentChanged();
        } else {
            m_commandLine.appendLine(errorMessage);
        }
    } else if (cmd == QLatin1String("DCRADIUS")) {
        std::vector<lcad::EntityId> circleIds;
        for (lcad::EntityId id : (m_view ? m_view->selectedIds() : std::vector<lcad::EntityId>{})) {
            const lcad::Entity* e = m_document.findEntity(id);
            if (e && e->type() == lcad::EntityType::Circle) circleIds.push_back(id);
        }
        if (circleIds.size() == 1) {
            startCommand(std::make_unique<lcad::DcRadiusCommand>(m_document, circleIds[0]), QStringLiteral("DCRADIUS"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*Select exactly one circle first*"));
        }
    } else if (cmd == QLatin1String("DCANGULAR")) {
        std::vector<lcad::EntityId> lineIds;
        for (lcad::EntityId id : (m_view ? m_view->selectedIds() : std::vector<lcad::EntityId>{})) {
            const lcad::Entity* e = m_document.findEntity(id);
            if (e && e->type() == lcad::EntityType::Line) lineIds.push_back(id);
        }
        if (lineIds.size() == 2) {
            startCommand(std::make_unique<lcad::DcAngularCommand>(m_document, lineIds[0], lineIds[1]),
                        QStringLiteral("DCANGULAR"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*Select exactly two lines first*"));
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
    } else if (cmd == QLatin1String("MLEADEREDIT")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (ids.size() != 1 || !m_document.findEntity(ids[0]) ||
            m_document.findEntity(ids[0])->type() != lcad::EntityType::MLeader) {
            if (!ids.empty()) m_commandLine.appendLine(QStringLiteral("*Select exactly one multileader*"));
        } else {
            startCommand(std::make_unique<MLeaderAddLeaderCommand>(m_document, ids[0]), QStringLiteral("MLEADEREDIT"));
        }
    } else if (cmd == QLatin1String("MLEADERSTYLE") || cmd == QLatin1String("MLS")) {
        startCommand(std::make_unique<MLeaderStyleCommand>(m_document), QStringLiteral("MLEADERSTYLE"));
    } else if (cmd == QLatin1String("DATALINK") || cmd == QLatin1String("DL")) {
        startCommand(std::make_unique<DataLinkCommand>(m_document), QStringLiteral("DATALINK"));
    } else if (cmd == QLatin1String("PASTECLIP") || cmd == QLatin1String("PASTEIMAGE")) {
        const QImage image = QGuiApplication::clipboard()->image();
        if (image.isNull()) {
            m_commandLine.appendLine(QStringLiteral("*Clipboard has no image*"));
        } else {
            startCommand(std::make_unique<ClipboardPasteCommand>(m_document, image), QStringLiteral("PASTECLIP"));
        }
    } else if (cmd == QLatin1String("LAYERSTATE") || cmd == QLatin1String("LAS")) {
        startCommand(std::make_unique<LayerStateCommand>(m_document), QStringLiteral("LAYERSTATE"));
    } else if (cmd == QLatin1String("TABLE") || cmd == QLatin1String("TB")) {
        startCommand(std::make_unique<TableCommand>(m_document), QStringLiteral("TABLE"));
    } else if (cmd == QLatin1String("TABLEDIT") || cmd == QLatin1String("TED")) {
        startCommand(std::make_unique<TableEditCommand>(m_document), QStringLiteral("TABLEDIT"));
    } else if (cmd == QLatin1String("HATCH") || cmd == QLatin1String("H")) {
        // With a selection: hatch it directly (existing closed polylines).
        // With none: HatchCommand switches to pick-internal-point mode, so an
        // empty selection isn't an error here the way it is for MOVE/COPY/etc.
        const std::vector<lcad::EntityId> ids =
            m_view && m_view->hasSelection() ? m_view->selectedIds() : std::vector<lcad::EntityId>{};
        startCommand(std::make_unique<HatchCommand>(m_document, ids), QStringLiteral("HATCH"));
    } else if (cmd == QLatin1String("BOUNDARY") || cmd == QLatin1String("BPOLY") || cmd == QLatin1String("BO")) {
        startCommand(std::make_unique<BoundaryCommand>(m_document), QStringLiteral("BOUNDARY"));
    } else if (cmd == QLatin1String("GRADIENT") || cmd == QLatin1String("GD")) {
        const std::vector<lcad::EntityId> ids =
            m_view && m_view->hasSelection() ? m_view->selectedIds() : std::vector<lcad::EntityId>{};
        startCommand(std::make_unique<GradientCommand>(m_document, ids), QStringLiteral("GRADIENT"));
    } else if (cmd == QLatin1String("BLOCK") || cmd == QLatin1String("B")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<BlockCommand>(m_document, ids), QStringLiteral("BLOCK"));
    } else if (cmd == QLatin1String("BPARAMETER") || cmd == QLatin1String("PAR")) {
        startCommand(std::make_unique<BlockParamCommand>(m_document), QStringLiteral("BPARAMETER"));
    } else if (cmd == QLatin1String("BFLIP")) {
        startCommand(std::make_unique<BlockFlipCommand>(m_document), QStringLiteral("BFLIP"));
    } else if (cmd == QLatin1String("BROTATION")) {
        startCommand(std::make_unique<BlockRotationCommand>(m_document), QStringLiteral("BROTATION"));
    } else if (cmd == QLatin1String("BARRAY")) {
        startCommand(std::make_unique<BlockArrayCommand>(m_document), QStringLiteral("BARRAY"));
    } else if (cmd == QLatin1String("BVISIBILITY")) {
        startCommand(std::make_unique<BlockVisibilityCommand>(m_document), QStringLiteral("BVISIBILITY"));
    } else if (cmd == QLatin1String("BLOOKUP")) {
        startCommand(std::make_unique<BlockLookupCommand>(m_document), QStringLiteral("BLOOKUP"));
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
    } else if (cmd == QLatin1String("IMAGEATTACH") || cmd == QLatin1String("IAT")) {
        startCommand(std::make_unique<ImageAttachCommand>(m_document), QStringLiteral("IMAGEATTACH"));
    } else if (cmd == QLatin1String("GEOGRAPHICLOCATION") || cmd == QLatin1String("GEO")) {
        startCommand(std::make_unique<GeoLocationCommand>(m_document), QStringLiteral("GEOGRAPHICLOCATION"));
    } else if (cmd == QLatin1String("POINTCLOUDATTACH") || cmd == QLatin1String("PCATTACH")) {
        startCommand(std::make_unique<PointCloudAttachCommand>(m_document), QStringLiteral("POINTCLOUDATTACH"));
    } else if (cmd == QLatin1String("PURGE") || cmd == QLatin1String("PU")) {
        const lcad::Document::PurgeResult purged = m_document.purge();
        m_commandLine.appendLine(QStringLiteral("*Purged %1 block(s) and %2 layer(s)*")
                                     .arg(purged.blocks)
                                     .arg(purged.layers));
        emit documentChanged();
    } else if (cmd == QLatin1String("REGEN") || cmd == QLatin1String("RE")) {
        emit documentChanged();
        m_commandLine.appendLine(QStringLiteral("*Regenerated*"));
    } else if (cmd == QLatin1String("UPDATEFIELD")) {
        int updated = 0;
        const std::string fileName = m_documentFileName.toStdString();
        for (lcad::Entity* e : m_document.entities()) {
            if (e->type() == lcad::EntityType::Text) {
                auto* text = static_cast<lcad::TextEntity*>(e);
                if (text->fieldTemplate().empty()) continue;
                text->setText(lcad::evaluateFieldTemplate(m_document, text->fieldTemplate(), fileName));
                ++updated;
            } else if (e->type() == lcad::EntityType::MText) {
                auto* mtext = static_cast<lcad::MTextEntity*>(e);
                if (mtext->fieldTemplate().empty()) continue;
                mtext->setText(lcad::evaluateFieldTemplate(m_document, mtext->fieldTemplate(), fileName));
                ++updated;
            }
        }
        m_commandLine.appendLine(QStringLiteral("*Updated %1 field(s)*").arg(updated));
        emit documentChanged();
    } else if (cmd == QLatin1String("ACTRECORD")) {
        if (m_activeCommand) {
            m_commandLine.appendLine(QStringLiteral("*Finish the active command first*"));
        } else if (m_recording) {
            m_commandLine.appendLine(QStringLiteral("*Already recording -- ACTSTOP to finish*"));
        } else {
            m_recording = true;
            m_recordingBuffer.clear();
            m_commandLine.appendLine(QStringLiteral("*Recording started -- type commands, then ACTSTOP*"));
        }
    } else if (cmd == QLatin1String("ACTSTOP")) {
        if (!m_recording) {
            m_commandLine.appendLine(QStringLiteral("*Not recording*"));
        } else {
            m_recording = false;
            m_lastMacro = m_recordingBuffer;
            m_recordingBuffer.clear();
            m_commandLine.appendLine(
                QStringLiteral("*Recording stopped: %1 step(s). PLAY to replay.*").arg(m_lastMacro.size()));
        }
    } else if (cmd == QLatin1String("PLAY")) {
        if (m_activeCommand) {
            m_commandLine.appendLine(QStringLiteral("*Finish the active command first*"));
        } else if (m_lastMacro.isEmpty()) {
            m_commandLine.appendLine(QStringLiteral("*No recorded macro yet -- ACTRECORD/ACTSTOP first*"));
        } else {
            const QStringList steps = m_lastMacro;
            m_commandLine.appendLine(QStringLiteral("*Playing %1 step(s)*").arg(steps.size()));
            for (const QString& step : steps) handleCommandText(step);
        }
    } else if (cmd == QLatin1String("ACTSAVE")) {
        m_awaitingActSavePath = true;
        m_commandLine.appendLine(QStringLiteral("ACTSAVE  Enter macro file path:"));
    } else if (cmd == QLatin1String("ACTLOAD")) {
        m_awaitingActLoadPath = true;
        m_commandLine.appendLine(QStringLiteral("ACTLOAD  Enter macro file path:"));
    } else if (cmd == QLatin1String("XREF") || cmd == QLatin1String("XR")) {
        startCommand(std::make_unique<XrefCommand>(m_document), QStringLiteral("XREF"));
    } else if (cmd == QLatin1String("EXPLODE") || cmd == QLatin1String("X")) {
        explodeSelection();
    } else if (cmd == QLatin1String("OVERKILL") || cmd == QLatin1String("OV")) {
        overkillSelection();
    } else if (cmd == QLatin1String("TCASE")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<TCaseCommand>(m_document, ids), QStringLiteral("TCASE"));
    } else if (cmd == QLatin1String("SCRIPT") || cmd == QLatin1String("SCR")) {
        m_awaitingScriptPath = true;
        m_commandLine.appendLine(QStringLiteral("SCRIPT  Enter script file path:"));
    } else if (cmd == QLatin1String("BURST")) {
        burstSelection();
    } else if (cmd == QLatin1String("TXT2MTXT")) {
        txt2mtxtSelection();
    } else if (cmd == QLatin1String("TORIENT")) {
        torientSelection();
    } else if (cmd == QLatin1String("TCOUNT")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) startCommand(std::make_unique<TCountCommand>(m_document, ids), QStringLiteral("TCOUNT"));
    } else if (cmd == QLatin1String("BREAKLINE") || cmd == QLatin1String("BL")) {
        startCommand(std::make_unique<BreaklineCommand>(m_document), QStringLiteral("BREAKLINE"));
    } else if (cmd == QLatin1String("LAYISO")) {
        layerIsolate();
    } else if (cmd == QLatin1String("LAYUNISO")) {
        layerUnisolate();
    } else if (cmd == QLatin1String("LAYOFF")) {
        layerToolOnSelection(false);
    } else if (cmd == QLatin1String("LAYFRZ")) {
        layerToolOnSelection(true);
    } else if (cmd == QLatin1String("LAYON")) {
        layerAllVisible(false);
    } else if (cmd == QLatin1String("LAYTHW")) {
        layerAllVisible(true);
    } else if (cmd == QLatin1String("LAYMRG")) {
        startCommand(std::make_unique<LayMrgCommand>(m_document), QStringLiteral("LAYMRG"));
    } else if (cmd == QLatin1String("LAYDEL")) {
        startCommand(std::make_unique<LayDelCommand>(m_document), QStringLiteral("LAYDEL"));
    } else if (cmd == QLatin1String("COPYTOLAYER")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (!ids.empty()) {
            startCommand(std::make_unique<CopyToLayerCommand>(m_document, ids), QStringLiteral("COPYTOLAYER"));
        }
    } else if (cmd == QLatin1String("QSELECT") || cmd == QLatin1String("QSE")) {
        startCommand(std::make_unique<QSelectCommand>(m_document), QStringLiteral("QSELECT"));
    } else if (cmd == QLatin1String("FIND")) {
        startCommand(std::make_unique<FindCommand>(m_document), QStringLiteral("FIND"));
    } else if (cmd == QLatin1String("QUICKCALC") || cmd == QLatin1String("CAL") || cmd == QLatin1String("QC")) {
        startCommand(std::make_unique<QuickCalcCommand>(), QStringLiteral("QUICKCALC"));
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
    } else if (cmd == QLatin1String("ANNOSCALE") || cmd == QLatin1String("ANNO")) {
        startCommand(std::make_unique<AnnoScaleCommand>(m_document), QStringLiteral("ANNOSCALE"));
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
    } else if (cmd == QLatin1String("WIRE") || cmd == QLatin1String("W")) {
        startCommand(std::make_unique<WireCommand>(m_document), QStringLiteral("WIRE"));
    } else if (cmd == QLatin1String("JUNCTION") || cmd == QLatin1String("J")) {
        startCommand(std::make_unique<JunctionCommand>(m_document), QStringLiteral("JUNCTION"));
    } else if (cmd == QLatin1String("NOCONNECT") || cmd == QLatin1String("NC")) {
        startCommand(std::make_unique<NoConnectCommand>(m_document), QStringLiteral("NOCONNECT"));
    } else if (cmd == QLatin1String("NETLABEL") || cmd == QLatin1String("LABEL")) {
        startCommand(std::make_unique<NetLabelCommand>(m_document), QStringLiteral("NETLABEL"));
    } else if (cmd == QLatin1String("PINADD")) {
        startCommand(std::make_unique<PinAddCommand>(m_document), QStringLiteral("PINADD"));
    } else if (cmd == QLatin1String("TRACK")) {
        startCommand(std::make_unique<TrackCommand>(m_document), QStringLiteral("TRACK"));
    } else if (cmd == QLatin1String("VIA") || cmd == QLatin1String("V")) {
        startCommand(std::make_unique<ViaCommand>(m_document), QStringLiteral("VIA"));
    } else if (cmd == QLatin1String("RATSNEST")) {
        startCommand(std::make_unique<RatsnestCommand>(m_document), QStringLiteral("RATSNEST"));
    } else if (cmd == QLatin1String("GERBER")) {
        startCommand(std::make_unique<GerberExportCommand>(m_document), QStringLiteral("GERBER"));
    } else if (cmd == QLatin1String("DRILL")) {
        startCommand(std::make_unique<DrillExportCommand>(m_document), QStringLiteral("DRILL"));
    } else if (cmd == QLatin1String("PNP")) {
        startCommand(std::make_unique<PickAndPlaceCommand>(m_document), QStringLiteral("PNP"));
    } else if (cmd == QLatin1String("DSNEXPORT")) {
        startCommand(std::make_unique<DsnExportCommand>(m_document), QStringLiteral("DSNEXPORT"));
    } else if (cmd == QLatin1String("AUTOROUTE")) {
        startCommand(std::make_unique<AutorouteCommand>(m_document), QStringLiteral("AUTOROUTE"));
    } else if (cmd == QLatin1String("FOOTPRINTGEN")) {
        startCommand(std::make_unique<FootprintGenCommand>(m_document), QStringLiteral("FOOTPRINTGEN"));
    } else if (cmd == QLatin1String("KICADSCHEXPORT")) {
        startCommand(std::make_unique<KiCadSchExportCommand>(m_document), QStringLiteral("KICADSCHEXPORT"));
    } else if (cmd == QLatin1String("KICADSCHIMPORT")) {
        startCommand(std::make_unique<KiCadSchImportCommand>(m_document), QStringLiteral("KICADSCHIMPORT"));
    } else if (cmd == QLatin1String("KICADPCBEXPORT")) {
        startCommand(std::make_unique<KiCadPcbExportCommand>(m_document), QStringLiteral("KICADPCBEXPORT"));
    } else if (cmd == QLatin1String("KICADPCBIMPORT")) {
        startCommand(std::make_unique<KiCadPcbImportCommand>(m_document), QStringLiteral("KICADPCBIMPORT"));
    } else if (cmd == QLatin1String("PCB3D")) {
#ifdef LCAD_HAS_OCCT
        auto* window = new Board3DWindow(m_document, lcad::CopperStackup{}, nullptr);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
#else
        m_commandLine.appendLine(QStringLiteral("*3D board view requires this build's OpenCASCADE support, "
                                                "not available here*"));
#endif
    } else if (cmd == QLatin1String("COPPERPOUR")) {
        startCommand(std::make_unique<CopperPourCommand>(m_document, pickTolerance()), QStringLiteral("COPPERPOUR"));
    } else if (cmd == QLatin1String("VIASTITCH")) {
        startCommand(std::make_unique<ViaStitchCommand>(m_document, pickTolerance()), QStringLiteral("VIASTITCH"));
    } else if (cmd == QLatin1String("PANELIZE")) {
        startCommand(std::make_unique<PanelizeCommand>(m_document, pickTolerance()), QStringLiteral("PANELIZE"));
    } else if (cmd == QLatin1String("LIBSAVE")) {
        startCommand(std::make_unique<LibSaveCommand>(m_document), QStringLiteral("LIBSAVE"));
    } else if (cmd == QLatin1String("LIBLOAD")) {
        startCommand(std::make_unique<LibLoadCommand>(m_document), QStringLiteral("LIBLOAD"));
    } else if (cmd == QLatin1String("LIBLIST")) {
        if (m_document.blocks().empty()) {
            m_commandLine.appendLine(QStringLiteral("*No blocks in this document*"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*%1 block(s):*").arg(m_document.blocks().size()));
            for (const auto& block : m_document.blocks()) {
                QString tag;
                if (block->isSymbol()) tag = QStringLiteral(" [symbol]");
                else if (block->isFootprint()) tag = QStringLiteral(" [footprint]");
                m_commandLine.appendLine(QStringLiteral("  %1%2").arg(QString::fromStdString(block->name), tag));
            }
        }
    } else if (cmd == QLatin1String("TEARDROP")) {
        const auto ids = lcad::addTeardropsToDocument(m_document, m_document.currentLayer());
        m_commandLine.appendLine(QStringLiteral("*Teardrops: %1 added*").arg(ids.size()));
        if (!ids.empty()) emit documentChanged();
    } else if (cmd == QLatin1String("GCODE")) {
        startCommand(std::make_unique<GCodeExportCommand>(m_document, pickTolerance()), QStringLiteral("GCODE"));
    } else if (cmd == QLatin1String("TIN")) {
        startCommand(std::make_unique<TinInfoCommand>(m_document), QStringLiteral("TIN"));
    } else if (cmd == QLatin1String("CONTOUR")) {
        startCommand(std::make_unique<ContourCommand>(m_document), QStringLiteral("CONTOUR"));
    } else if (cmd == QLatin1String("CUTFILL")) {
        startCommand(std::make_unique<CutFillCommand>(m_document), QStringLiteral("CUTFILL"));
    } else if (cmd == QLatin1String("PROFILE")) {
        startCommand(std::make_unique<ProfileCommand>(m_document), QStringLiteral("PROFILE"));
    } else if (cmd == QLatin1String("DRC")) {
        // The interactive DRC command turns on every check this library
        // supports, including the two opt-in ones (courtyard overlap,
        // silkscreen-over-pad) -- runDrc's own default keeps them off
        // for API/test backward compatibility, not because a real user
        // running DRC here wouldn't want them.
        lcad::DrcRules rules;
        rules.checkCourtyards = true;
        rules.checkSilkscreenOverPad = true;
        const std::vector<lcad::DrcViolation> violations = lcad::runDrc(m_document, rules);
        m_commandLine.appendLine(QStringLiteral("*DRC: %1 violation(s)*").arg(violations.size()));
        for (const lcad::DrcViolation& v : violations) {
            m_commandLine.appendLine(QStringLiteral("  %1").arg(QString::fromStdString(v.message)));
        }
    } else if (cmd == QLatin1String("ERC")) {
        const std::vector<lcad::Net> nets = lcad::computeNets(m_document);
        const std::vector<lcad::ErcIssue> issues = lcad::runErc(m_document, nets);
        m_commandLine.appendLine(QStringLiteral("*ERC: %1 net(s), %2 issue(s)*").arg(nets.size()).arg(issues.size()));
        for (const lcad::ErcIssue& issue : issues) {
            const QString tag = issue.severity == lcad::ErcIssue::Severity::Error
                                    ? QStringLiteral("Error")
                                    : QStringLiteral("Warning");
            m_commandLine.appendLine(QStringLiteral("  [%1] %2").arg(tag, QString::fromStdString(issue.message)));
        }
    } else if (cmd == QLatin1String("NETLIST")) {
        startCommand(std::make_unique<NetlistExportCommand>(m_document), QStringLiteral("NETLIST"));
    } else if (cmd == QLatin1String("SIM")) {
        const lcad::CircuitBuildResult built = lcad::buildCircuitFromDocument(m_document);
        if (built.circuit.elements.empty()) {
            m_commandLine.appendLine(
                QStringLiteral("*No simulatable components (R/C/L/V/I/D with REFDES+VALUE) found*"));
        } else {
            const lcad::OperatingPointResult op = lcad::solveDcOperatingPoint(built.circuit);
            if (!op.converged) {
                m_commandLine.appendLine(QStringLiteral("*DC operating point did not converge*"));
            } else {
                m_commandLine.appendLine(QStringLiteral("*SIM: DC operating point*"));
                for (int n = 1; n <= built.circuit.nodeCount; ++n) {
                    m_commandLine.appendLine(QStringLiteral("  %1 = %2 V")
                                                 .arg(QString::fromStdString(built.nodeNames[n]))
                                                 .arg(op.nodeVoltages[n], 0, 'g', 5));
                }
            }
        }
        if (!built.skippedRefDes.empty()) {
            QString skipped;
            for (const std::string& refDes : built.skippedRefDes) {
                if (!skipped.isEmpty()) skipped += QStringLiteral(", ");
                skipped += QString::fromStdString(refDes);
            }
            m_commandLine.appendLine(QStringLiteral("*Skipped (no VALUE or unconnected pin): %1*").arg(skipped));
        }
    } else if (cmd == QLatin1String("TRAN")) {
        startCommand(std::make_unique<TranCommand>(m_document), QStringLiteral("TRAN"));
    } else if (cmd == QLatin1String("NCOPY")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (ids.size() != 1 || !m_document.findEntity(ids[0]) ||
            m_document.findEntity(ids[0])->type() != lcad::EntityType::Insert) {
            if (!ids.empty()) m_commandLine.appendLine(QStringLiteral("*Select exactly one block (INSERT)*"));
        } else {
            startCommand(std::make_unique<NCopyCommand>(m_document, ids[0]), QStringLiteral("NCOPY"));
        }
    } else if (cmd == QLatin1String("TEXTFIT")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (ids.size() != 1 || !m_document.findEntity(ids[0]) ||
            m_document.findEntity(ids[0])->type() != lcad::EntityType::Text) {
            if (!ids.empty()) m_commandLine.appendLine(QStringLiteral("*Select exactly one single-line TEXT*"));
        } else {
            startCommand(std::make_unique<TextFitCommand>(m_document, ids[0]), QStringLiteral("TEXTFIT"));
        }
    } else if (cmd == QLatin1String("AUDIT")) {
        startCommand(std::make_unique<AuditCommand>(m_document), QStringLiteral("AUDIT"));
    } else if (cmd == QLatin1String("LAYTRANS")) {
        startCommand(std::make_unique<LayTransCommand>(m_document), QStringLiteral("LAYTRANS"));
    } else if (cmd == QLatin1String("WIPEOUT")) {
        startCommand(std::make_unique<WipeoutCommand>(m_document), QStringLiteral("WIPEOUT"));
    } else if (cmd == QLatin1String("REVCLOUD")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (ids.size() != 1) {
            if (!ids.empty()) m_commandLine.appendLine(QStringLiteral("*Select exactly one polyline or circle*"));
        } else {
            startCommand(std::make_unique<RevcloudCommand>(m_document, ids[0]), QStringLiteral("REVCLOUD"));
        }
    } else if (cmd == QLatin1String("DIFFPAIR")) {
        startCommand(std::make_unique<DiffPairCommand>(m_document), QStringLiteral("DIFFPAIR"));
    } else if (cmd == QLatin1String("LENGTHTUNE")) {
        const std::vector<lcad::EntityId> ids = selectionForModify();
        if (ids.size() != 1 || !m_document.findEntity(ids[0]) ||
            m_document.findEntity(ids[0])->type() != lcad::EntityType::Track) {
            if (!ids.empty()) m_commandLine.appendLine(QStringLiteral("*Select exactly one track*"));
        } else {
            startCommand(std::make_unique<LengthTuneCommand>(m_document, ids[0]), QStringLiteral("LENGTHTUNE"));
        }
    } else if (cmd == QLatin1String("WIRENUM")) {
        lcad::assignWireNumbers(m_document);
        m_commandLine.appendLine(QStringLiteral("*Wire numbers assigned*"));
        emit documentChanged();
    } else if (cmd == QLatin1String("WIRELIST") || cmd == QLatin1String("LINELIST")) {
        startCommand(std::make_unique<WireListCommand>(m_document), QStringLiteral("WIRELIST"));
    } else if (cmd == QLatin1String("BOM")) {
        startCommand(std::make_unique<BomCommand>(m_document), QStringLiteral("BOM"));
    } else if (cmd == QLatin1String("DATAEXTRACTION") || cmd == QLatin1String("EATTEXT")) {
        startCommand(std::make_unique<DataExtractionCommand>(m_document), QStringLiteral("DATAEXTRACTION"));
    } else if (cmd == QLatin1String("SHEETNEW")) {
        startCommand(std::make_unique<SheetNewCommand>(m_document), QStringLiteral("SHEETNEW"));
    } else if (cmd == QLatin1String("SHEETGOTO")) {
        startCommand(std::make_unique<SheetGoToCommand>(m_document), QStringLiteral("SHEETGOTO"));
    } else if (cmd == QLatin1String("SHEETS")) {
        const std::vector<lcad::Sheet> sheets = lcad::listSheets(m_document);
        if (sheets.empty()) {
            m_commandLine.appendLine(QStringLiteral("*No sheets yet -- use SHEETNEW to create one*"));
        } else {
            m_commandLine.appendLine(QStringLiteral("*%1 sheet(s):*").arg(sheets.size()));
            for (const lcad::Sheet& sheet : sheets) {
                const lcad::Layer* layer = m_document.findLayer(sheet.layerId);
                const QString activeMark = (layer && layer->visible) ? QStringLiteral(" (active)") : QString();
                m_commandLine.appendLine(QStringLiteral("  %1%2").arg(QString::fromStdString(sheet.name), activeMark));
            }
        }
    } else if (cmd == QLatin1String("TAGINST")) {
        lcad::assignInstrumentTags(m_document);
        m_commandLine.appendLine(QStringLiteral("*Instrument tags assigned*"));
        emit documentChanged();
    } else if (!cmd.isEmpty()) {
        m_commandLine.appendLine(QStringLiteral("*Unknown command \"%1\"*").arg(trimmed));
    }
}

void CommandDispatcher::handlePointPicked(const lcad::Point2D& pt, const std::optional<lcad::SnapRef>& snapRef,
                                          bool recordClick) {
    if (m_lispLoop && m_lispWaitIsPoint) {
        m_lispInputResult = QStringLiteral("%1,%2").arg(pt.x, 0, 'g', 15).arg(pt.y, 0, 'g', 15).toStdString();
        m_lispLoop->quit();
        return;
    }
    if (!m_activeCommand) return;
    if (m_recording && recordClick) {
        m_recordingBuffer << QStringLiteral("%1,%2").arg(pt.x, 0, 'g', 15).arg(pt.y, 0, 'g', 15);
    }
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
    if (m_lispLoop) {
        m_lispWaitCancelled = true;
        m_lispLoop->quit();
        return;
    }
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
            // KeepDefinitions: attributes revert to their ATTDEF tag
            // placeholders, exactly like real EXPLODE -- BURST is the tool
            // that keeps the values (see burstSelection).
            for (auto& child : insert.instantiate(lcad::InsertEntity::AttributeMode::KeepDefinitions)) {
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

void CommandDispatcher::overkillSelection() {
    const std::vector<lcad::EntityId> ids = selectionForModify();
    if (ids.empty()) return;

    std::vector<const lcad::Entity*> entities;
    entities.reserve(ids.size());
    for (lcad::EntityId id : ids) entities.push_back(m_document.findEntity(id));

    const auto duplicateIndices = lcad::findDuplicateEntities(entities);
    if (duplicateIndices.empty()) {
        m_commandLine.appendLine(QStringLiteral("*No duplicates found*"));
        return;
    }

    auto batch = std::make_unique<lcad::BatchCommand>("Overkill");
    for (std::size_t index : duplicateIndices) {
        batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, ids[index]));
    }
    m_document.commandStack().execute(std::move(batch));
    m_commandLine.appendLine(QStringLiteral("*%1 duplicate(s) removed*").arg(duplicateIndices.size()));
    emit documentChanged();
}

void CommandDispatcher::runScriptFile(const QString& path) {
    if (m_scriptDepth >= 8) {
        m_commandLine.appendLine(QStringLiteral("*Script nesting too deep -- stopped*"));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_commandLine.appendLine(QStringLiteral("*Cannot open script \"%1\"*").arg(path));
        return;
    }
    const std::vector<lcad::ScriptLine> lines = lcad::parseScript(QString::fromUtf8(file.readAll()).toStdString());
    m_commandLine.appendLine(QStringLiteral("*Running script: %1*").arg(path));

    ++m_scriptDepth;
    int fed = 0;
    // RSCRIPT (real AutoCAD's own script-restart command, typically the
    // last line of a continuously-looping slideshow-style script) is
    // bounded here rather than truly infinite: real AutoCAD relies on the
    // user pressing Escape to break the loop, a live keypress this
    // batch/typed-command player has no hook for mid-playback -- capping
    // at kMaxRscriptRepeats keeps a script that ends in RSCRIPT (a very
    // common real .scr pattern) from hanging the app forever instead of
    // erroring out immediately.
    constexpr int kMaxRscriptRepeats = 1000;
    int repeats = 0;
    bool restart;
    do {
        restart = false;
        for (const lcad::ScriptLine& line : lines) {
            if (line.blank()) {
                handleCommandText(QString());
                ++fed;
                continue;
            }
            for (std::size_t i = 0; i < line.tokens.size(); ++i) {
                if (m_activeCommand && m_activeCommand->wantsTextInput()) {
                    // Free-text stage (TEXT's "Enter text:"): the
                    // untokenized rest of the line is one entry, spaces
                    // included.
                    handleCommandText(QString::fromStdString(line.raw.substr(line.offsets[i])));
                    ++fed;
                    break;
                }
                // DELAY/RSCRIPT are script-control tokens, not real
                // commands -- intercepted here rather than routed through
                // handleCommandText, which has no concept of pausing or
                // restarting script playback.
                if (!m_activeCommand && line.tokens[i].compare(0, 5, "DELAY") == 0 &&
                    line.tokens[i].size() == 5 && i + 1 < line.tokens.size()) {
                    bool ok = false;
                    const int ms = QString::fromStdString(line.tokens[i + 1]).toInt(&ok);
                    // A real, blocking delay -- matching real AutoCAD's own
                    // DELAY, which also blocks command processing during
                    // the pause (the UI won't repaint until it elapses).
                    if (ok && ms > 0) QThread::msleep(static_cast<unsigned long>(ms));
                    ++i;
                    ++fed;
                    continue;
                }
                if (!m_activeCommand && line.tokens[i] == "RSCRIPT") {
                    if (++repeats < kMaxRscriptRepeats) restart = true;
                    ++fed;
                    continue;
                }
                handleCommandText(QString::fromStdString(line.tokens[i]));
                ++fed;
            }
            if (restart) break;
        }
    } while (restart);
    --m_scriptDepth;
    m_commandLine.appendLine(QStringLiteral("*Script finished: %1 input(s)*").arg(fed));
    emit documentChanged();
}

void CommandDispatcher::burstSelection() {
    const std::vector<lcad::EntityId> ids = selectionForModify();
    if (ids.empty()) return;

    auto batch = std::make_unique<lcad::BatchCommand>("Burst");
    int made = 0;
    int skipped = 0;
    for (lcad::EntityId id : ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;
        if (e->type() != lcad::EntityType::Insert) {
            ++skipped;
            continue;
        }
        const auto& insert = static_cast<const lcad::InsertEntity&>(*e);
        batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
        for (auto& child : insert.instantiate()) { // ResolveToText: values become TEXT
            child->setId(m_document.reserveEntityId());
            batch->add(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(child)));
        }
        ++made;
    }
    if (!batch->empty()) {
        m_document.commandStack().execute(std::move(batch));
        emit documentChanged();
    }
    if (skipped > 0) {
        m_commandLine.appendLine(QStringLiteral("*%1 burst, %2 skipped (blocks only)*").arg(made).arg(skipped));
    } else {
        m_commandLine.appendLine(QStringLiteral("*%1 burst*").arg(made));
    }
}

void CommandDispatcher::txt2mtxtSelection() {
    const std::vector<lcad::EntityId> ids = selectionForModify();
    if (ids.empty()) return;

    std::vector<lcad::EntityId> textIds;
    std::vector<const lcad::TextEntity*> texts;
    for (lcad::EntityId id : ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (e && e->type() == lcad::EntityType::Text) {
            textIds.push_back(id);
            texts.push_back(static_cast<const lcad::TextEntity*>(e));
        }
    }
    if (texts.size() < 2) {
        m_commandLine.appendLine(QStringLiteral("*Select at least two TEXT objects*"));
        return;
    }

    const auto combined = lcad::combineTextEntities(texts);
    if (!combined) return;

    auto batch = std::make_unique<lcad::BatchCommand>("Txt2MText");
    const lcad::LayerId layer = texts.front()->layer();
    for (lcad::EntityId id : textIds) {
        batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
    }
    batch->add(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::MTextEntity>(m_document.reserveEntityId(), layer, combined->position,
                                                        combined->text, combined->height, 0.0, combined->rotation)));
    m_document.commandStack().execute(std::move(batch));
    m_commandLine.appendLine(QStringLiteral("*%1 TEXT combined into one MTEXT*").arg(texts.size()));
    emit documentChanged();
}

void CommandDispatcher::torientSelection() {
    const std::vector<lcad::EntityId> ids = selectionForModify();
    if (ids.empty()) return;

    auto batch = std::make_unique<lcad::BatchCommand>("TOrient");
    int changed = 0;
    for (lcad::EntityId id : ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (!e) continue;
        double rotation = 0.0;
        if (e->type() == lcad::EntityType::Text) {
            rotation = static_cast<const lcad::TextEntity&>(*e).rotation();
        } else if (e->type() == lcad::EntityType::MText) {
            rotation = static_cast<const lcad::MTextEntity&>(*e).rotation();
        } else {
            continue;
        }
        const double target = lcad::torientRotation(rotation);
        if (std::abs(target - rotation) < 1e-12) continue;
        // Flip in place about the text's own anchor so it stays put.
        std::unique_ptr<lcad::Entity> clone = e->clone();
        if (clone->type() == lcad::EntityType::Text) {
            auto& t = static_cast<lcad::TextEntity&>(*clone);
            t.rotate(t.position(), target - rotation);
        } else {
            auto& t = static_cast<lcad::MTextEntity&>(*clone);
            t.rotate(t.position(), target - rotation);
        }
        batch->add(std::make_unique<lcad::ReplaceEntityCommand>(m_document, id, std::move(clone)));
        ++changed;
    }
    if (!batch->empty()) {
        m_document.commandStack().execute(std::move(batch));
        emit documentChanged();
    }
    m_commandLine.appendLine(QStringLiteral("*%1 text object(s) reoriented*").arg(changed));
}

void CommandDispatcher::layerIsolate() {
    const std::vector<lcad::EntityId> ids = selectionForModify();
    if (ids.empty()) return;

    std::set<lcad::LayerId> keep;
    for (lcad::EntityId id : ids) {
        if (const lcad::Entity* e = m_document.findEntity(id)) keep.insert(e->layer());
    }
    m_layIsoHidden.clear();
    for (const lcad::Layer& layer : m_document.layers()) {
        if (keep.count(layer.id) || !layer.visible) continue;
        m_document.findLayer(layer.id)->visible = false;
        m_layIsoHidden.push_back(layer.id);
    }
    m_commandLine.appendLine(QStringLiteral("*%1 layer(s) isolated, %2 hidden -- LAYUNISO to restore*")
                                 .arg(keep.size())
                                 .arg(m_layIsoHidden.size()));
    if (m_view) m_view->pruneSelectionForLayerState();
    emit documentChanged();
}

void CommandDispatcher::layerUnisolate() {
    if (m_layIsoHidden.empty()) {
        m_commandLine.appendLine(QStringLiteral("*Nothing to restore -- LAYISO first*"));
        return;
    }
    int restored = 0;
    for (lcad::LayerId id : m_layIsoHidden) {
        if (lcad::Layer* layer = m_document.findLayer(id)) {
            layer->visible = true;
            ++restored;
        }
    }
    m_layIsoHidden.clear();
    m_commandLine.appendLine(QStringLiteral("*%1 layer(s) restored*").arg(restored));
    emit documentChanged();
}

void CommandDispatcher::layerToolOnSelection(bool freeze) {
    const std::vector<lcad::EntityId> ids = selectionForModify();
    if (ids.empty()) return;

    std::set<lcad::LayerId> layers;
    for (lcad::EntityId id : ids) {
        if (const lcad::Entity* e = m_document.findEntity(id)) layers.insert(e->layer());
    }
    int changed = 0;
    int skipped = 0;
    for (lcad::LayerId id : layers) {
        if (freeze && id == m_document.currentLayer()) {
            ++skipped; // AutoCAD refuses to freeze the current layer
            continue;
        }
        if (lcad::Layer* layer = m_document.findLayer(id)) {
            if (freeze) layer->frozen = true;
            else layer->visible = false;
            ++changed;
        }
    }
    QString message = QStringLiteral("*%1 layer(s) %2*").arg(changed).arg(freeze ? QStringLiteral("frozen")
                                                                                  : QStringLiteral("turned off"));
    if (skipped > 0) message += QStringLiteral(" *(current layer skipped)*");
    m_commandLine.appendLine(message);
    if (m_view) m_view->pruneSelectionForLayerState();
    emit documentChanged();
}

void CommandDispatcher::layerAllVisible(bool thaw) {
    int changed = 0;
    for (const lcad::Layer& layer : m_document.layers()) {
        lcad::Layer* mutableLayer = m_document.findLayer(layer.id);
        if (thaw ? mutableLayer->frozen : !mutableLayer->visible) {
            if (thaw) mutableLayer->frozen = false;
            else mutableLayer->visible = true;
            ++changed;
        }
    }
    m_commandLine.appendLine(
        QStringLiteral("*%1 layer(s) %2*").arg(changed).arg(thaw ? QStringLiteral("thawed") : QStringLiteral("turned on")));
    emit documentChanged();
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
