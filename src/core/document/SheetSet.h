#pragma once

#include <string>
#include <vector>

namespace lcad {

// One sheet in a Sheet Set: a placeholder referencing a specific paper
// space layout in a specific drawing file on disk -- not necessarily the
// currently open Document. AutoCAD's own Sheet Set Manager organizes
// construction-document sheets this way precisely because a real project's
// sheets are spread across many DWG files, not one; sheetNumber/sheetTitle
// are the values a title block's Fields (core/document/Fields.h) would
// pull in once placed on that layout.
struct SheetSetEntry {
    std::string sheetNumber;
    std::string sheetTitle;
    std::string drawingPath;
    std::string layoutName;
};

// A named group of sheets. AutoCAD subsets nest arbitrarily; this is a
// single flat level -- a real, disclosed scope cut (a tree adds real
// complexity for marginal benefit at this stage, and every AutoCAD sheet
// set still works fine as a flat list of subsets in practice).
struct SheetSubset {
    std::string name;
    std::vector<SheetSetEntry> sheets;
};

// A Sheet Set: AutoCAD's own Sheet Set Manager (.dst) concept -- a named,
// ordered collection of subsets organizing sheets (layouts) that may live
// across many different drawing files, kept in one place for numbering,
// title-block consistency, and (eventually) batch publishing.
struct SheetSet {
    std::string name;
    std::string description;
    std::vector<SheetSubset> subsets;

    int sheetCount() const;
    // First sheet with this exact sheetNumber across every subset, or
    // nullptr -- sheet numbers are meant to be unique within a set, like
    // AutoCAD's own SSM enforces in its UI (not re-checked here).
    SheetSetEntry* findSheet(const std::string& sheetNumber);
    const SheetSetEntry* findSheet(const std::string& sheetNumber) const;
};

// Adds a new, empty subset named name (even if one by that name already
// exists -- callers that care about uniqueness check first) and returns it.
SheetSubset& addSubset(SheetSet& set, const std::string& name);

// Appends entry to the named subset, creating the subset first if it
// doesn't exist yet. Returns false only if subsetName is empty.
bool addSheet(SheetSet& set, const std::string& subsetName, const SheetSetEntry& entry);

// Removes the first sheet with this sheetNumber from whichever subset holds
// it. Returns false if no such sheet exists.
bool removeSheet(SheetSet& set, const std::string& sheetNumber);

// .kss (KumCAD Sheet Set) persistence: a simple percent-encoded,
// tag-per-line text format, the same idea as core3d/Persistence3D.cpp's own
// .kcad3d format, just for this much smaller data shape.
bool saveSheetSet(const SheetSet& set, const std::string& path, std::string* errorOut = nullptr);
bool loadSheetSet(SheetSet& set, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
