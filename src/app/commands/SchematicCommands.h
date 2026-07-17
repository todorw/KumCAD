#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// JUNCTION: places a T/cross-connection dot per click until Enter -- see
// core/schematic/Netlist.h for why this matters (crossing wires without one
// aren't electrically connected).
class JunctionCommand : public DrawCommand {
public:
    explicit JunctionCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("JUNCTION  Specify a point (Enter to finish):"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// NOCONNECT: marks a pin as deliberately unconnected, per click until Enter.
class NoConnectCommand : public DrawCommand {
public:
    explicit NoConnectCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("NOCONNECT  Specify a point (Enter to finish):"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// NETLABEL: insertion point, then the net name (free text). Pressing Enter
// with no name cancels without creating anything, like TEXT.
class NetLabelCommand : public DrawCommand {
public:
    explicit NetLabelCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("NETLABEL  Specify insertion point:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_havePosition; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::Point2D m_position;
    bool m_havePosition = false;
    bool m_finished = false;
};

// WIRELIST: specify an insertion point, then builds a wire-list/cross-
// reference TABLE there (see core/electrical/WireList.h).
class WireListCommand : public DrawCommand {
public:
    explicit WireListCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("WIRELIST  Specify insertion point:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// SHEETNEW: creates a new schematic sheet (see core/schematic/Sheets.h) --
// prompts for a name, then switches to it (a freshly created sheet is the
// obvious next place to start drawing).
class SheetNewCommand : public DrawCommand {
public:
    explicit SheetNewCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("SHEETNEW  Enter new sheet name:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// SHEETGOTO: switches the active sheet (see core/schematic/Sheets.h) --
// prompts for a sheet name.
class SheetGoToCommand : public DrawCommand {
public:
    explicit SheetGoToCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("SHEETGOTO  Enter sheet name:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};

// PINADD: defines one pin on an existing block definition, turning it into a
// schematic symbol (see BlockDefinition::pins). Not undoable, matching this
// codebase's existing precedent for block-metadata edits (BlockParamCommand).
class PinAddCommand : public DrawCommand {
public:
    explicit PinAddCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("PINADD  Enter block name:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage != Stage::Position && m_stage != Stage::StubEnd; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_stage == Stage::StubEnd ? std::optional<lcad::Point2D>(m_position) : std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BlockName, PinName, PinNumber, ElectricalType, Position, StubEnd };

    lcad::Document& m_document;
    Stage m_stage = Stage::BlockName;
    lcad::BlockDefinition* m_block = nullptr;
    std::string m_pinName;
    std::string m_pinNumber;
    lcad::PinElectricalType m_electricalType = lcad::PinElectricalType::Passive;
    lcad::Point2D m_position;
    bool m_finished = false;
};

// NETLIST: writes formatNetlist() (see core/schematic/Netlist.h) to a
// user-typed path.
class NetlistExportCommand : public DrawCommand {
public:
    explicit NetlistExportCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("NETLIST  Enter output file path:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};
