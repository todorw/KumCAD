#pragma once

#include "core/Color.h"
#include "core/document/LineType.h"

#include <optional>
#include <string>
#include <vector>

namespace lcad {

class Document;

// AutoCAD's Layer Translator (LAYTRANS): batch-remaps a drawing's layers
// to a standard set of names/properties, reusable across many drawings via
// a saved mapping. Real LAYTRANS loads its mapping interactively from a
// template drawing's own layer table; this uses a simple text mapping file
// instead (not AutoCAD's binary .dws standards format), one line per rule:
//   oldName=newName[,color=RRGGBB][,linetype=NAME][,lineweight=MM]
// A blank line or one starting with '#' is skipped.
struct LayerTranslation {
    std::string oldName;
    std::string newName;
    std::optional<Color> color;
    std::optional<LineType> linetype;
    std::optional<double> lineweight;
};

std::vector<LayerTranslation> parseLayerTranslationFile(const std::string& text);
bool loadLayerTranslationFile(const std::string& path, std::vector<LayerTranslation>& out,
                              std::string* errorOut = nullptr);

struct LayerTranslationResult {
    int renamed = 0;  // oldName didn't collide with an existing layer -- renamed in place
    int merged = 0;   // newName already existed -- oldName's entities moved in, oldName deleted
    std::vector<std::string> notFound; // oldName wasn't a layer in doc at all (not an error -- see below)
};

// Applies every translation in order. oldName not found in doc is
// recorded in notFound but NOT treated as an error -- a mapping file is
// typically written once against a company standard and reused across
// many drawings that don't all have every layer it lists, exactly how
// real LAYTRANS mapping files work. If newName doesn't already exist as
// a layer, oldName is renamed to newName in place and gets the optional
// color/linetype/lineweight overrides (layer "0" is a real AutoCAD
// layer that can be a translation's newName just fine, but is never
// itself a valid oldName -- it can't be renamed away, same guard
// Document::deleteLayer already enforces). If newName already exists,
// entities move onto it and oldName is deleted (the same merge Express
// Tools' own LAYMRG performs) -- newName's OWN properties win in that
// case, not the mapping's overrides, matching how merging into an
// already-standardized layer shouldn't silently redefine it.
LayerTranslationResult applyLayerTranslations(Document& doc, const std::vector<LayerTranslation>& translations);

} // namespace lcad
