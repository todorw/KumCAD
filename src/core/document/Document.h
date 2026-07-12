#pragma once

#include "core/Ids.h"
#include "core/document/Block.h"
#include "core/document/Command.h"
#include "core/document/Layer.h"
#include "core/document/Layout.h"
#include "core/document/PlotStyle.h"
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

// AutoCAD's GEOGRAPHICLOCATION: ties one world-space point to a real-world
// latitude/longitude, plus which world-space direction is true north.
// Simplified: no coordinate system (CRS) selection or map/imagery display,
// just the reference point + rotation AutoCAD's own dialog also collects.
struct GeoLocation {
    Point2D designPoint;
    double latitude = 0.0;
    double longitude = 0.0;
    double northRotation = 0.0; // radians CCW from world +Y to true north
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
    // styles work) rather than per-instance. Simultaneous per-viewport scale
    // representations do work -- EntityPainter derives each paper-space
    // viewport's own multiplier from 1/viewport.viewScale when painting
    // through it (DrawingView's layout-tab render, and PrintRenderer) -- but
    // model space itself only has the one "current" representation you're
    // editing at (Document::annotationScale(), a simplified CANNOSCALE),
    // not AutoCAD's list of several simultaneously-visible representations
    // toggled per object.
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

    // Layer States Manager (AutoCAD's LAYERSTATE): named snapshots of every
    // layer's visibility/lock/color/linetype/lineweight. captureLayerState()
    // builds one from the current layers (for saving, or for
    // RestoreLayerStateCommand's undo snapshot) without touching
    // m_layerStates; saveLayerState() stores it under a name, overwriting
    // any existing state with that name.
    const std::vector<LayerState>& layerStates() const { return m_layerStates; }
    LayerState captureLayerState(const std::string& name) const;
    void saveLayerState(const std::string& name) { saveLayerState(captureLayerState(name)); }
    void saveLayerState(LayerState state);
    void applyLayerState(const LayerState& state);
    bool deleteLayerState(const std::string& name);

    // Plot Style Table (see core/document/PlotStyle.h): named styles a
    // layer can opt into (Layer::plotStyle) to change how it plots without
    // changing how it displays. savePlotStyle stores/overwrites by name.
    const std::vector<PlotStyle>& plotStyles() const { return m_plotStyles; }
    PlotStyle* findPlotStyle(const std::string& name);
    const PlotStyle* findPlotStyle(const std::string& name) const;
    void savePlotStyle(PlotStyle style);
    bool deletePlotStyle(const std::string& name);
    // What entity e actually plots with: its layer's color/linetype/
    // lineweight, then its own overrides, then (if the layer has a plot
    // style assigned) that style's overrides on top.
    PlotAppearance plotAppearance(const Entity& e) const;

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

    // GEOGRAPHICLOCATION: nullopt when the drawing has none set.
    const std::optional<GeoLocation>& geoLocation() const { return m_geoLocation; }
    void setGeoLocation(std::optional<GeoLocation> geo) { m_geoLocation = geo; }

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
    std::vector<LayerState> m_layerStates;
    std::vector<PlotStyle> m_plotStyles;
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
    std::optional<GeoLocation> m_geoLocation;

    NamedDimStyle& currentNamedDimStyle();
    const NamedDimStyle& currentNamedDimStyle() const;
    NamedMLeaderStyle& currentNamedMLeaderStyle();
    const NamedMLeaderStyle& currentNamedMLeaderStyle() const;

    CommandStack m_commandStack;
};

} // namespace lcad
