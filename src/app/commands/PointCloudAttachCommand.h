#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD POINTCLOUDATTACH (simplified): reads a plain-text XYZ point cloud
// file (see core/io/PointCloudFile.h) at its own file coordinates -- no
// insertion point/scale/rotation prompt, since survey XYZ exports are
// normally already in the target coordinate system, unlike a block or
// image. Only the .xyz/.txt dialect is understood; the real binary
// .rcs/.rcp/.las formats would each need their own parser.
class PointCloudAttachCommand : public DrawCommand {
public:
    explicit PointCloudAttachCommand(lcad::Document& document) : m_document(document) {}

    QString start() override { return QStringLiteral("POINTCLOUDATTACH  Enter XYZ file path:"); }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_finished = false;
};
