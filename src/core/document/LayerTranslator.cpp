#include "core/document/LayerTranslator.h"

#include "core/document/Commands.h"
#include "core/document/Document.h"
#include "core/document/ExpressTools.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace lcad {

namespace {

std::optional<Color> parseHexColor(const std::string& hex) {
    if (hex.size() != 6) return std::nullopt;
    char* end = nullptr;
    const long value = std::strtol(hex.c_str(), &end, 16);
    if (end != hex.c_str() + hex.size()) return std::nullopt;
    return Color{static_cast<std::uint8_t>((value >> 16) & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
                static_cast<std::uint8_t>(value & 0xFF)};
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

} // namespace

std::vector<LayerTranslation> parseLayerTranslationFile(const std::string& text) {
    std::vector<LayerTranslation> result;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        LayerTranslation t;
        t.oldName = trim(line.substr(0, eq));
        if (t.oldName.empty()) continue;

        std::string rest = line.substr(eq + 1);
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            const auto comma = rest.find(',', start);
            fields.push_back(trim(rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (fields.empty() || fields[0].empty()) continue;
        t.newName = fields[0];

        for (std::size_t i = 1; i < fields.size(); ++i) {
            const auto colon = fields[i].find('=');
            if (colon == std::string::npos) continue;
            const std::string key = trim(fields[i].substr(0, colon));
            const std::string value = trim(fields[i].substr(colon + 1));
            if (key == "color") {
                t.color = parseHexColor(value);
            } else if (key == "linetype") {
                t.linetype = lineTypeFromName(value);
            } else if (key == "lineweight") {
                try {
                    t.lineweight = std::stod(value);
                } catch (...) {
                }
            }
        }
        result.push_back(std::move(t));
    }
    return result;
}

bool loadLayerTranslationFile(const std::string& path, std::vector<LayerTranslation>& out, std::string* errorOut) {
    std::ifstream in(path);
    if (!in) {
        if (errorOut) *errorOut = "Could not open file for reading";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = parseLayerTranslationFile(buffer.str());
    return true;
}

LayerTranslationResult applyLayerTranslations(Document& doc, const std::vector<LayerTranslation>& translations) {
    LayerTranslationResult result;
    for (const LayerTranslation& t : translations) {
        const Layer* source = findLayerByName(doc, t.oldName);
        if (!source || source->id == 0) {
            result.notFound.push_back(t.oldName);
            continue;
        }
        const LayerId sourceId = source->id;

        if (const Layer* target = findLayerByName(doc, t.newName)) {
            // Merge: same mechanism as Express Tools' own LAYMRG. The
            // target's OWN properties win -- merging into an already-
            // standardized layer shouldn't silently redefine it.
            const std::vector<EntityId> ids = entityIdsOnLayer(doc, sourceId);
            if (!ids.empty()) doc.commandStack().execute(std::make_unique<SetEntityLayerCommand>(doc, ids, target->id));
            doc.deleteLayer(sourceId);
            ++result.merged;
        } else {
            // Rename in place: the layer record itself becomes the new
            // name, so every entity referencing it by id is unaffected.
            if (Layer* mutableSource = doc.findLayer(sourceId)) {
                mutableSource->name = t.newName;
                if (t.color) mutableSource->color = *t.color;
                if (t.linetype) mutableSource->linetype = *t.linetype;
                if (t.lineweight) mutableSource->lineweight = *t.lineweight;
            }
            ++result.renamed;
        }
    }
    return result;
}

} // namespace lcad
