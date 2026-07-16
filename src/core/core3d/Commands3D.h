#pragma once

#include "core/core3d/Document3D.h"
#include "core/document/Command.h"

namespace lcad {

// Reuses this codebase's existing generic Command/CommandStack (see
// core/document/Command.h -- it isn't actually coupled to the 2D Document
// class despite living in the same file) for the 3D feature tree's
// undo/redo, the same way core/document/Commands.h does for 2D entities.

class AddFeature3DCommand : public Command {
public:
    AddFeature3DCommand(Document3D& document, Feature3D feature) : m_document(document), m_feature(std::move(feature)) {}

    void execute() override { m_index = m_document.addFeature(m_feature); }
    void undo() override { m_document.removeFeature(m_index); }
    std::string description() const override { return "Add 3D feature"; }

    int index() const { return m_index; }

private:
    Document3D& m_document;
    Feature3D m_feature;
    int m_index = -1;
};

class UpdateFeature3DCommand : public Command {
public:
    UpdateFeature3DCommand(Document3D& document, int index, Feature3D newFeature)
        : m_document(document), m_index(index), m_newFeature(std::move(newFeature)) {
        if (const Feature3D* existing = m_document.findFeature(index)) m_oldFeature = *existing;
    }

    void execute() override { m_document.updateFeature(m_index, m_newFeature); }
    void undo() override { m_document.updateFeature(m_index, m_oldFeature); }
    std::string description() const override { return "Edit 3D feature"; }

private:
    Document3D& m_document;
    int m_index;
    Feature3D m_newFeature;
    Feature3D m_oldFeature;
};

class RemoveFeature3DCommand : public Command {
public:
    RemoveFeature3DCommand(Document3D& document, int index) : m_document(document), m_index(index) {
        if (const Feature3D* existing = m_document.findFeature(index)) m_feature = *existing;
    }

    // No-op (leaves the document unchanged) if something still depends on
    // the feature -- same guard as Document3D::removeFeature itself.
    void execute() override { m_removed = m_document.removeFeature(m_index); }
    void undo() override {
        if (m_removed) m_document.insertFeatureAt(m_index, m_feature);
    }
    std::string description() const override { return "Remove 3D feature"; }

private:
    Document3D& m_document;
    int m_index;
    Feature3D m_feature;
    bool m_removed = false;
};

} // namespace lcad
