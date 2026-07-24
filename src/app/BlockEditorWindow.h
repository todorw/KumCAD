#pragma once

#include "core/document/Document.h"

#include <QMainWindow>

#include <string>

class DrawingView;
class CommandLine;
class CommandDispatcher;

// AutoCAD's BEDIT: an in-place block editor. Owns its own small Document
// seeded from the target block's own entities (see core/document/
// BlockEdit.h's extractBlockForEditing) so every existing 2D editing
// command (LINE, TRIM, everything CommandDispatcher already knows) works
// completely unchanged inside it -- the real editing environment reused on
// a scoped document, rather than a separate hand-built geometry editor.
// "Save Block Definition" (applyBlockEdit) writes the edited entities back
// into the SAME BlockDefinition in the document BEDIT was launched from
// (parentDoc), which every existing INSERT of that block picks up
// immediately since they already read the BlockDefinition's own entities
// live, not a snapshot. parentDoc must outlive this window: a nested
// INSERT inside the block being edited carries a raw pointer into
// parentDoc's own block table.
//
// Disclosed scope limit: only geometry entities round-trip through Save.
// The block's own dynamic parameters, pins (schematic symbols), and pads
// (PCB footprints) are untouched by an edit session (BlockParamCommand/
// PINADD remain the way to change those) -- BEDIT here is real AutoCAD's
// own core use case (edit a block's drawn geometry in place), not a
// combined editor for every kind of block metadata.
class BlockEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    BlockEditorWindow(lcad::Document& parentDoc, std::string blockName, QWidget* parent = nullptr);

signals:
    // Emitted after a successful Save Block Definition, so the window that
    // launched BEDIT can refresh anything showing the parent document.
    void blockSaved();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void saveBlock();

    lcad::Document& m_parentDoc;
    std::string m_blockName;
    lcad::Document m_blockDoc;
    DrawingView* m_view = nullptr;
    CommandLine* m_commandLine = nullptr;
    CommandDispatcher* m_dispatcher = nullptr;
    bool m_dirty = false;
};
