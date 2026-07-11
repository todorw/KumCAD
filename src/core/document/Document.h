#pragma once

#include "core/Ids.h"
#include "core/document/Block.h"
#include "core/document/Command.h"
#include "core/document/Layer.h"
#include "core/document/Layout.h"
#include "core/geometry/Entity.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lcad {

// Dimension style values (a DIMSTYLE-lite): new dimensions snapshot the
// current style's values at creation. Persisted via the DXF DIMSTYLE table
// (and the $DIM* header variables for the current style).
struct DimStyle {
    double textHeight = 2.5; // DIMTXT
    double arrowSize = 1.25; // DIMASZ
    int decimals = 2;        // DIMDEC
};

// A named dimension style (AutoCAD's DIMSTYLE table entry).
struct NamedDimStyle {
    std::string name = "Standard";
    DimStyle style;
};

// A named text style (AutoCAD's STYLE table entry). font is a family name
// (persisted in the DXF STYLE table's font group); fixedHeight 0 means the
// TEXT command prompts for a height.
struct TextStyle {
    std::string name = "Standard";
    std::string font = "Arial";
    double fixedHeight = 0.0;  // DXF 40
    double widthFactor = 1.0;  // DXF 41
    double obliqueDeg = 0.0;   // DXF 50
};

class Document {
public:
    Document();

    // Layers
    LayerId addLayer(const std::string& name, Color color);
    Layer* findLayer(LayerId id);
    const Layer* findLayer(LayerId id) const;
    const std::vector<Layer>& layers() const { return m_layers; }
    LayerId currentLayer() const { return m_currentLayer; }
    void setCurrentLayer(LayerId id) { m_currentLayer = id; }

    // Entities. addEntity/removeEntity are the low-level primitives that Commands
    // (see Commands.h) wrap to make every mutation undoable. New entities land
    // in the active space: model space (activeSpace() == -1) or a layout's
    // paper space (>= 0, indexing layouts()). entities() lists model space;
    // paperEntities(i) a layout's paper space. removeEntity works across all
    // spaces, and an entity re-added after removeEntity (undo) returns to the
    // space it was removed from because ids stay in exactly one order list.
    EntityId reserveEntityId() { return m_nextEntityId++; }
    void addEntity(std::unique_ptr<Entity> entity);
    std::unique_ptr<Entity> removeEntity(EntityId id);
    Entity* findEntity(EntityId id);
    const Entity* findEntity(EntityId id) const;
    std::vector<Entity*> entities();
    std::vector<const Entity*> entities() const;
    std::vector<Entity*> paperEntities(int layoutIndex);
    std::vector<const Entity*> paperEntities(int layoutIndex) const;

    int activeSpace() const { return m_activeSpace; }
    void setActiveSpace(int layoutIndex) {
        m_activeSpace =
            (layoutIndex >= 0 && layoutIndex < static_cast<int>(m_layouts.size())) ? layoutIndex : -1;
    }

    // Blocks. Definitions are append-only (deleting one would dangle any
    // InsertEntity referencing it) and owned by the document, so pointers to
    // them stay valid for the document's lifetime, including across moves.
    const BlockDefinition* addBlock(std::string name, std::vector<std::unique_ptr<Entity>> entities);
    const BlockDefinition* findBlock(const std::string& name) const;
    const std::vector<std::unique_ptr<BlockDefinition>>& blocks() const { return m_blocks; }

    CommandStack& commandStack() { return m_commandStack; }

    // Re-resolves every associative dimension's anchored definition points
    // from the entities they reference (dropping anchors whose entity is
    // gone). The app calls this after each document mutation so dimensions
    // follow the geometry they measure.
    void reassociateDimensions();

    // Global linetype pattern scale, AutoCAD's LTSCALE (DXF header $LTSCALE).
    double lineTypeScale() const { return m_lineTypeScale; }
    void setLineTypeScale(double scale) {
        if (scale > 1e-9) m_lineTypeScale = scale;
    }

    // Named dimension styles. dimStyle() is the current one; new dimensions
    // snapshot its values at creation. The "Standard" style always exists.
    const DimStyle& dimStyle() const { return currentNamedDimStyle().style; }
    DimStyle& dimStyle() { return currentNamedDimStyle().style; }
    const std::vector<NamedDimStyle>& dimStyles() const { return m_dimStyles; }
    const std::string& currentDimStyleName() const { return m_currentDimStyle; }
    NamedDimStyle* findDimStyle(const std::string& name);
    // Creates or overwrites; returns the stored style.
    NamedDimStyle& addOrUpdateDimStyle(const std::string& name, const DimStyle& style);
    // False if no style of that name exists.
    bool setCurrentDimStyle(const std::string& name);

    // Named text styles. The "Standard" style always exists; TEXT/MTEXT tag
    // entities with the current style's name and rendering resolves it.
    const std::vector<TextStyle>& textStyles() const { return m_textStyles; }
    const std::string& currentTextStyleName() const { return m_currentTextStyle; }
    const TextStyle* findTextStyle(const std::string& name) const;
    TextStyle& addOrUpdateTextStyle(const TextStyle& style);
    bool setCurrentTextStyle(const std::string& name);

    // Paper-space layouts. Every document has at least one.
    const std::vector<Layout>& layouts() const { return m_layouts; }
    std::vector<Layout>& layouts() { return m_layouts; }

    // Removes a layout and erases its paper entities. Refuses to drop the
    // last layout. Returns false when index is invalid.
    bool removeLayout(int index);

private:
    std::vector<Layer> m_layers;
    LayerId m_nextLayerId = 1;
    LayerId m_currentLayer = 0;

    std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entityMap;
    std::vector<EntityId> m_entityOrder;
    EntityId m_nextEntityId = 1;

    std::vector<std::unique_ptr<BlockDefinition>> m_blocks;

    double m_lineTypeScale = 1.0;
    std::vector<NamedDimStyle> m_dimStyles{NamedDimStyle{}};
    std::string m_currentDimStyle = "Standard";
    std::vector<TextStyle> m_textStyles{TextStyle{}};
    std::string m_currentTextStyle = "Standard";
    std::vector<Layout> m_layouts{Layout{}};
    int m_activeSpace = -1; // -1 = model space, otherwise a layout index

    NamedDimStyle& currentNamedDimStyle();
    const NamedDimStyle& currentNamedDimStyle() const;

    CommandStack m_commandStack;
};

} // namespace lcad
