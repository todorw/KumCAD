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

// Multileader style values (a MLEADERSTYLE-lite): new MLEADERs snapshot the
// current style's values at creation, like DimStyle. landingGap is a
// multiple of textHeight, matching LEADER's hardcoded text offset.
struct MLeaderStyle {
    double textHeight = 2.5;
    double arrowSize = 1.25;
    double landingGap = 0.6;
};

// A named multileader style (AutoCAD's MLEADERSTYLE table entry).
struct NamedMLeaderStyle {
    std::string name = "Standard";
    MLeaderStyle style;
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
    // AutoCAD's ANNOTATIVE object property, simplified: annotative status
    // lives on the text style (matching how AutoCAD's own Annotative text
    // styles work) rather than per-instance, and there's one document-wide
    // annotation scale (Document::annotationScale()) rather than the several
    // simultaneous per-viewport scale representations AutoCAD supports.
    bool annotative = false; // DXF 290 (this codebase's TABLES extension)
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
    // Re-inserts a previously-removed entity directly into the entity map,
    // without touching any space's draw-order list -- for undoing a layout
    // deletion, which restores the owning layout's entityIds itself.
    void restoreEntity(std::unique_ptr<Entity> entity);
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
    BlockDefinition* findBlock(const std::string& name);
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

    // Point display style (PDMODE/PDSIZE): 0 dot, 2 plus, 3 X, 4 tick;
    // adding 32 draws a circle around it. Size is in drawing units.
    int pointMode() const { return m_pointMode; }
    void setPointMode(int mode) { m_pointMode = mode; }
    double pointSize() const { return m_pointSize; }
    void setPointSize(double size) {
        if (size > 1e-9) m_pointSize = size;
    }

    // Named entity groups (GROUP command): click-selecting one member
    // selects the whole group. Dead ids are tolerated and skipped.
    const std::vector<std::pair<std::string, std::vector<EntityId>>>& groups() const { return m_groups; }
    void setGroup(const std::string& name, std::vector<EntityId> ids);
    bool removeGroup(const std::string& name);
    // The members of the first group containing id, or nullptr.
    const std::vector<EntityId>* groupOf(EntityId id) const;

    // PURGE: drops block definitions no insert references (checking every
    // space and other blocks' children) and layers with no entities that
    // aren't current or layer 0. Not undoable, like layout management.
    struct PurgeResult {
        int blocks = 0;
        int layers = 0;
    };
    PurgeResult purge();

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

    // Named multileader styles, mirroring the DimStyle accessors above.
    const MLeaderStyle& mleaderStyle() const { return currentNamedMLeaderStyle().style; }
    MLeaderStyle& mleaderStyle() { return currentNamedMLeaderStyle().style; }
    const std::vector<NamedMLeaderStyle>& mleaderStyles() const { return m_mleaderStyles; }
    const std::string& currentMLeaderStyleName() const { return m_currentMLeaderStyle; }
    NamedMLeaderStyle* findMLeaderStyle(const std::string& name);
    NamedMLeaderStyle& addOrUpdateMLeaderStyle(const std::string& name, const MLeaderStyle& style);
    bool setCurrentMLeaderStyle(const std::string& name);

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

    // Document-wide annotation scale (a simplified CANNOSCALE): text/mtext
    // using an Annotative TextStyle renders at height * annotationScale, so
    // it reads the same physical size once you match this to your plot
    // scale, without editing every annotative object's height by hand.
    double annotationScale() const { return m_annotationScale; }
    void setAnnotationScale(double scale) {
        if (scale > 1e-9) m_annotationScale = scale;
    }

private:
    std::vector<Layer> m_layers;
    LayerId m_nextLayerId = 1;
    LayerId m_currentLayer = 0;

    std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entityMap;
    std::vector<EntityId> m_entityOrder;
    EntityId m_nextEntityId = 1;

    std::vector<std::unique_ptr<BlockDefinition>> m_blocks;

    double m_lineTypeScale = 1.0;
    int m_pointMode = 3;      // X marker by default so points are visible
    double m_pointSize = 2.0; // drawing units
    std::vector<std::pair<std::string, std::vector<EntityId>>> m_groups;
    std::vector<NamedDimStyle> m_dimStyles{NamedDimStyle{}};
    std::string m_currentDimStyle = "Standard";
    std::vector<NamedMLeaderStyle> m_mleaderStyles{NamedMLeaderStyle{}};
    std::string m_currentMLeaderStyle = "Standard";
    std::vector<TextStyle> m_textStyles{TextStyle{}};
    std::string m_currentTextStyle = "Standard";
    std::vector<Layout> m_layouts{Layout{}};
    int m_activeSpace = -1; // -1 = model space, otherwise a layout index
    double m_annotationScale = 1.0;

    NamedDimStyle& currentNamedDimStyle();
    const NamedDimStyle& currentNamedDimStyle() const;
    NamedMLeaderStyle& currentNamedMLeaderStyle();
    const NamedMLeaderStyle& currentNamedMLeaderStyle() const;

    CommandStack m_commandStack;
};

} // namespace lcad
