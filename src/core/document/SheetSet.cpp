#include "core/document/SheetSet.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace lcad {

int SheetSet::sheetCount() const {
    int count = 0;
    for (const SheetSubset& subset : subsets) count += static_cast<int>(subset.sheets.size());
    return count;
}

SheetSetEntry* SheetSet::findSheet(const std::string& sheetNumber) {
    for (SheetSubset& subset : subsets) {
        for (SheetSetEntry& entry : subset.sheets) {
            if (entry.sheetNumber == sheetNumber) return &entry;
        }
    }
    return nullptr;
}

const SheetSetEntry* SheetSet::findSheet(const std::string& sheetNumber) const {
    for (const SheetSubset& subset : subsets) {
        for (const SheetSetEntry& entry : subset.sheets) {
            if (entry.sheetNumber == sheetNumber) return &entry;
        }
    }
    return nullptr;
}

SheetSubset& addSubset(SheetSet& set, const std::string& name) {
    SheetSubset subset;
    subset.name = name;
    set.subsets.push_back(std::move(subset));
    return set.subsets.back();
}

bool addSheet(SheetSet& set, const std::string& subsetName, const SheetSetEntry& entry) {
    if (subsetName.empty()) return false;
    for (SheetSubset& subset : set.subsets) {
        if (subset.name == subsetName) {
            subset.sheets.push_back(entry);
            return true;
        }
    }
    addSubset(set, subsetName).sheets.push_back(entry);
    return true;
}

bool removeSheet(SheetSet& set, const std::string& sheetNumber) {
    for (SheetSubset& subset : set.subsets) {
        auto it = std::find_if(subset.sheets.begin(), subset.sheets.end(),
                               [&](const SheetSetEntry& e) { return e.sheetNumber == sheetNumber; });
        if (it != subset.sheets.end()) {
            subset.sheets.erase(it);
            return true;
        }
    }
    return false;
}

namespace {

// istringstream's >> reads whitespace-delimited tokens, which can't
// represent an empty string or embedded spaces/newlines directly --
// percent-encode the handful of characters that would break that, same
// scheme core3d/Persistence3D.cpp uses for its own .kcad3d format.
const std::string kEmptyMarker = "@@EMPTY@@";

std::string encodeToken(const std::string& s) {
    if (s.empty()) return kEmptyMarker;
    std::string out;
    for (unsigned char c : s) {
        if (c == ' ' || c == '%' || c == '\n' || c == '\r' || c == '\t') {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string decodeToken(const std::string& s) {
    if (s == kEmptyMarker) return "";
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 3;
        } else {
            out += s[i];
            ++i;
        }
    }
    return out;
}

} // namespace

bool saveSheetSet(const SheetSet& set, const std::string& path, std::string* errorOut) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open file for writing";
        return false;
    }
    out << "KSS 1\n";
    out << "NAME " << encodeToken(set.name) << "\n";
    out << "DESC " << encodeToken(set.description) << "\n";
    out << "SUBSETS " << set.subsets.size() << "\n";
    for (const SheetSubset& subset : set.subsets) {
        out << "SUBSET " << encodeToken(subset.name) << "\n";
        out << "SHEETS " << subset.sheets.size() << "\n";
        for (const SheetSetEntry& entry : subset.sheets) {
            out << "SHEET " << encodeToken(entry.sheetNumber) << " " << encodeToken(entry.sheetTitle) << " "
                << encodeToken(entry.drawingPath) << " " << encodeToken(entry.layoutName) << "\n";
        }
    }
    return true;
}

bool loadSheetSet(SheetSet& set, const std::string& path, std::string* errorOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorOut) *errorOut = "Could not open file for reading";
        return false;
    }
    std::string tag;
    int version = 0;
    in >> tag >> version;
    if (tag != "KSS") {
        if (errorOut) *errorOut = "Not a KumCAD Sheet Set (.kss) file";
        return false;
    }

    SheetSet loaded;
    std::string encoded;
    in >> tag >> encoded;
    loaded.name = decodeToken(encoded);
    in >> tag >> encoded;
    loaded.description = decodeToken(encoded);

    std::size_t subsetCount = 0;
    in >> tag >> subsetCount;
    for (std::size_t i = 0; i < subsetCount; ++i) {
        SheetSubset subset;
        in >> tag >> encoded;
        subset.name = decodeToken(encoded);
        std::size_t sheetCount = 0;
        in >> tag >> sheetCount;
        for (std::size_t j = 0; j < sheetCount; ++j) {
            SheetSetEntry entry;
            std::string number, title, drawingPath, layout;
            in >> tag >> number >> title >> drawingPath >> layout;
            entry.sheetNumber = decodeToken(number);
            entry.sheetTitle = decodeToken(title);
            entry.drawingPath = decodeToken(drawingPath);
            entry.layoutName = decodeToken(layout);
            subset.sheets.push_back(std::move(entry));
        }
        loaded.subsets.push_back(std::move(subset));
    }

    if (!in) {
        if (errorOut) *errorOut = "Malformed .kss file";
        return false;
    }
    set = std::move(loaded);
    return true;
}

} // namespace lcad
