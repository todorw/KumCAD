#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <utility>
#include <vector>

namespace lcad {
class AttDefEntity;
}

// AutoCAD-style INSERT: name an existing block, pick an insertion point,
// then answer each of the block's attribute prompts (Enter keeps the
// default). Scale/rotation default to 1/0; use SCALE and ROTATE afterwards.
class InsertCommand : public DrawCommand {
public:
    explicit InsertCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_stage == Stage::Name || m_stage == Stage::Attributes; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Name, Position, Attributes };

    QString availableBlocks() const;
    // Prompt for the attribute definition at m_attIndex, or place the insert
    // and finish when every attribute has been answered.
    QString nextAttributePromptOrFinish();

    lcad::Document& m_document;
    Stage m_stage = Stage::Name;
    const lcad::BlockDefinition* m_block = nullptr;
    lcad::Point2D m_position;
    std::vector<const lcad::AttDefEntity*> m_attDefs;
    std::vector<std::pair<std::string, std::string>> m_values;
    std::size_t m_attIndex = 0;
    bool m_finished = false;
};
