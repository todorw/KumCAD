#include "core/schematic/Bom.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Table.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <tuple>

namespace lcad {

namespace {
bool isTruthyFlag(const std::string& v) {
    std::string lower;
    lower.reserve(v.size());
    for (char c : v) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == "1" || lower == "true" || lower == "yes" || lower == "dnp";
}

// "000" -> "0", "007" -> "7", "70" -> "70": leading zeros dropped so
// equal-VALUE digit runs compare purely by length first (a natural-sort
// numeric comparison), never as a byte-for-byte prefix match.
std::string stripLeadingZeros(const std::string& digits) {
    const std::size_t nonZero = digits.find_first_not_of('0');
    return nonZero == std::string::npos ? "0" : digits.substr(nonZero);
}

// Natural/numeric ordering for refDes strings (KiCad's own BOM sort):
// "R2" before "R10", not "R10" before "R2" (which plain lexicographic
// ordering gets wrong since '1' < '2'). Alternates between runs of
// digits (compared numerically, via length-then-lexicographic on the
// zero-stripped run, exactly equivalent to numeric comparison since same-
// length digit strings sort identically either way) and runs of
// non-digits (compared byte-for-byte) -- not just a single leading-
// letters/trailing-digits split, so it stays correct for any refDes
// shape, not only the common single-prefix one.
bool refDesLess(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool aDigit = std::isdigit(static_cast<unsigned char>(a[i]));
        const bool bDigit = std::isdigit(static_cast<unsigned char>(b[j]));
        if (aDigit && bDigit) {
            const std::size_t aStart = i, bStart = j;
            while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) ++i;
            while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) ++j;
            const std::string numA = stripLeadingZeros(a.substr(aStart, i - aStart));
            const std::string numB = stripLeadingZeros(b.substr(bStart, j - bStart));
            if (numA.size() != numB.size()) return numA.size() < numB.size();
            if (numA != numB) return numA < numB;
            // Equal numeric value (possibly different zero-padding) -- keep
            // comparing the rest of the string instead of calling it a tie.
        } else {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i;
            ++j;
        }
    }
    return a.size() < b.size();
}
} // namespace

std::vector<BomRow> generateBom(const Document& doc, bool includeDnp) {
    // Keyed by (part, value, dnp) so a DNP instance never merges into a
    // fitted group sharing the same part/value; a group's own std::map
    // keeps refDes entries sorted as they're inserted, avoiding a second
    // sort pass.
    std::map<std::tuple<std::string, std::string, bool>, std::vector<std::string>> groups;

    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (!insert->block() || !insert->block()->isSymbol()) continue;
        const std::string* refDes = insert->attributeValue("REFDES");
        if (!refDes || refDes->empty()) continue;

        const std::string* dnpAttr = insert->attributeValue("DNP");
        const bool dnp = dnpAttr && isTruthyFlag(*dnpAttr);
        if (dnp && !includeDnp) continue;

        const std::string* value = insert->attributeValue("VALUE");
        const std::tuple<std::string, std::string, bool> key{insert->block()->name, value ? *value : std::string(), dnp};
        groups[key].push_back(*refDes);
    }

    std::vector<BomRow> rows;
    for (auto& [key, refDesList] : groups) {
        std::sort(refDesList.begin(), refDesList.end(), refDesLess);
        BomRow row;
        row.part = std::get<0>(key);
        row.value = std::get<1>(key);
        row.dnp = std::get<2>(key);
        row.refDes = std::move(refDesList);
        row.quantity = static_cast<int>(row.refDes.size());
        rows.push_back(std::move(row));
    }
    return rows;
}

TableEntity* buildBomTable(Document& doc2d, const std::vector<BomRow>& rows, Point2D position) {
    const int rowCount = static_cast<int>(rows.size()) + 1;
    std::vector<double> rowHeights(static_cast<std::size_t>(rowCount), 4.0);
    std::vector<double> colWidths = {15.0, 15.0, 10.0, 12.0, 40.0};
    std::vector<std::string> cells(static_cast<std::size_t>(rowCount) * 5);

    cells[0] = "Part";
    cells[1] = "Value";
    cells[2] = "Qty";
    cells[3] = "DNP";
    cells[4] = "Ref Des";

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const BomRow& row = rows[i];
        const std::size_t r = i + 1;
        cells[r * 5 + 0] = row.part;
        cells[r * 5 + 1] = row.value;
        cells[r * 5 + 2] = std::to_string(row.quantity);
        cells[r * 5 + 3] = row.dnp ? "DNP" : "";
        std::string refDesJoined;
        for (std::size_t j = 0; j < row.refDes.size(); ++j) {
            if (j > 0) refDesJoined += ", ";
            refDesJoined += row.refDes[j];
        }
        cells[r * 5 + 4] = refDesJoined;
    }

    auto table = std::make_unique<TableEntity>(doc2d.reserveEntityId(), doc2d.currentLayer(), position, rowHeights,
                                                colWidths, cells);
    TableEntity* raw = table.get();
    doc2d.addEntity(std::move(table));
    return raw;
}

} // namespace lcad
