#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"
#include "core/geometry/Tolerance.h"

#include <optional>
#include <vector>

// AutoCAD's TOLERANCE: picks an insertion point, then collects one or more
// feature-control-frame rows as typed text (real AutoCAD uses a Geometric
// Tolerance dialog with a symbol picker; this codebase's text-command-line
// convention -- see FOOTPRINTGEN's own family:params precedent -- uses a
// fixed 5-field colon-separated row format instead):
//   characteristic:value:modifier:diameter:datums
// characteristic is a GeoCharacteristic name or common abbreviation
// (case-insensitive; see parseCharacteristic in the .cpp); modifier is
// M, L, or NONE; diameter is DIA or NODIA; datums is a comma-separated
// list of letters (A, A,B, A,B,C) or NONE. Real AutoCAD lets a frame stack
// two rows for a composite tolerance; this accepts any number -- an
// empty line ends row entry, requiring at least one real row.
//
// Per-datum modifiers (e.g. "A(M)") aren't exposed through this typed
// syntax -- a real, disclosed scope limit; ToleranceDatumRef's own
// modifier field still works correctly for a caller that builds rows some
// other way.
class ToleranceCommand : public DrawCommand {
public:
    explicit ToleranceCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool wantsTextInput() const override { return m_havePosition; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::Point2D m_position;
    bool m_havePosition = false;
    std::vector<lcad::ToleranceRow> m_rows;
    bool m_finished = false;
};
