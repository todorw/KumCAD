#include "core/schematic/Annotate.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"

#include <cctype>
#include <optional>
#include <unordered_map>

namespace lcad {

namespace {

bool isPowerSymbolName(const std::string& name) { return name == "GND" || name == "VCC"; }

// Block name with any trailing digits stripped -- "R" stays "R",
// "CONN2" becomes "CONN".
std::string prefixOf(const std::string& blockName) {
    std::size_t end = blockName.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(blockName[end - 1]))) --end;
    return blockName.substr(0, end);
}

// The trailing number of refdes if it starts with EXACTLY prefix
// followed by one or more digits and nothing else, or nullopt otherwise
// (e.g. an unrelated or hand-typed REFDES that doesn't match this
// symbol's own derived prefix at all).
std::optional<int> trailingNumber(const std::string& refdes, const std::string& prefix) {
    if (refdes.rfind(prefix, 0) != 0) return std::nullopt;
    const std::string digits = refdes.substr(prefix.size());
    if (digits.empty()) return std::nullopt;
    for (char c : digits) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    return std::stoi(digits);
}

int& counterFor(std::unordered_map<std::string, int>& counters, const std::string& prefix) {
    return counters.try_emplace(prefix, 1).first->second; // starts at 1 the first time a prefix is seen
}

} // namespace

int annotateSchematic(Document& doc) {
    std::unordered_map<std::string, int> nextNumber;

    // Seed every prefix's counter from whatever REFDES values are
    // already in use, so annotating twice (or annotating after some
    // instances were hand-tagged) never collides with them.
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (!insert->block() || !insert->block()->isSymbol() || isPowerSymbolName(insert->blockName())) continue;
        const std::string prefix = prefixOf(insert->blockName());
        if (const std::string* refdes = insert->attributeValue("REFDES")) {
            if (const auto n = trailingNumber(*refdes, prefix)) {
                int& counter = counterFor(nextNumber, prefix);
                counter = std::max(counter, *n + 1);
            }
        }
    }

    int labeled = 0;
    for (Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        auto* insert = static_cast<InsertEntity*>(e);
        if (!insert->block() || !insert->block()->isSymbol() || isPowerSymbolName(insert->blockName())) continue;
        if (insert->attributeValue("REFDES")) continue; // already tagged -- leave it alone

        const std::string prefix = prefixOf(insert->blockName());
        int& next = counterFor(nextNumber, prefix);
        insert->setAttribute("REFDES", prefix + std::to_string(next));
        ++next;
        ++labeled;
    }
    return labeled;
}

} // namespace lcad
