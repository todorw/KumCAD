#include "core/core3d/Assembly.h"

#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace lcad {

int Assembly::addComponent(AssemblyComponent component) {
    m_components.push_back(std::move(component));
    return static_cast<int>(m_components.size()) - 1;
}

void Assembly::addMate(Mate mate) {
    m_mates.push_back(mate);
}

void Assembly::solve() {
    for (const Mate& m : m_mates) {
        if (m.componentA < 0 || m.componentA >= static_cast<int>(m_components.size())) continue;
        if (m.componentB < 0 || m.componentB >= static_cast<int>(m_components.size())) continue;

        AssemblyComponent& compA = m_components[static_cast<std::size_t>(m.componentA)];
        AssemblyComponent& compB = m_components[static_cast<std::size_t>(m.componentB)];

        const gp_Pnt localPointA(m.ax, m.ay, m.az);
        const gp_Dir localDirA(m.adx, m.ady, m.adz);
        const gp_Pnt localPointB(m.bx, m.by, m.bz);
        const gp_Dir localDirB(m.bdx, m.bdy, m.bdz);

        const gp_Pnt worldPointA = localPointA.Transformed(compA.placement);
        const gp_Dir worldDirA = localDirA.Transformed(compA.placement);

        if (m.type == MateType::Parallel || m.type == MateType::Perpendicular) {
            const gp_Dir currentDirB = localDirB.Transformed(compB.placement);
            gp_Dir target = worldDirA;

            if (m.type == MateType::Perpendicular) {
                // Project currentDirB onto the plane perpendicular to
                // worldDirA -- the closest perpendicular direction to
                // componentB's own current one, for a minimal rotation.
                gp_Vec proj(currentDirB);
                proj -= gp_Vec(worldDirA) * proj.Dot(gp_Vec(worldDirA));
                if (proj.Magnitude() < 1e-9) {
                    // currentDirB is already exactly parallel to worldDirA --
                    // every perpendicular direction is equally valid, so
                    // fall back to worldDirA crossed with any non-parallel
                    // helper axis.
                    gp_Dir helper(0, 0, 1);
                    if (std::abs(worldDirA.Dot(helper)) > 0.9) helper = gp_Dir(1, 0, 0);
                    proj = gp_Vec(worldDirA.Crossed(helper));
                }
                target = gp_Dir(proj);
            }

            gp_Quaternion alignment;
            alignment.SetRotation(gp_Vec(currentDirB), gp_Vec(target));
            gp_Vec axis;
            double angle = 0.0;
            alignment.GetVectorAndAngle(axis, angle);

            if (axis.Magnitude() > 1e-9 && std::abs(angle) > 1e-12) {
                const gp_Pnt worldPointB = localPointB.Transformed(compB.placement);
                gp_Trsf pivotRotation;
                pivotRotation.SetRotation(gp_Ax1(worldPointB, gp_Dir(axis)), angle);
                compB.placement = pivotRotation.Multiplied(compB.placement);
            }
            continue;
        }

        const bool antiParallel = (m.type == MateType::Coincident || m.type == MateType::Distance);
        gp_Dir targetDir = worldDirA;
        if (antiParallel) targetDir.Reverse();

        // Rotation that takes componentB's local reference direction onto
        // targetDir -- this alone leaves componentB's reference point
        // wherever that rotation (about the world origin) happens to send
        // it, which the translation below then corrects for.
        gp_Quaternion alignment;
        alignment.SetRotation(gp_Vec(localDirB), gp_Vec(targetDir));
        gp_Trsf rotation;
        rotation.SetRotation(alignment);

        if (m.type == MateType::Angle) {
            gp_Trsf spin;
            spin.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), targetDir), m.value * M_PI / 180.0);
            rotation = spin.Multiplied(rotation);
        }

        const gp_Pnt rotatedLocalPointB = localPointB.Transformed(rotation);

        gp_Pnt targetWorldPoint = worldPointA;
        // The offset moves B further away from A along A's own outward
        // direction (worldDirA) -- not targetDir, which is the *reversed*
        // direction B's own reference direction gets aligned to.
        if (m.type == MateType::Distance) targetWorldPoint.Translate(gp_Vec(worldDirA) * m.value);

        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(rotatedLocalPointB, targetWorldPoint));

        compB.placement = translation.Multiplied(rotation);
    }
}

AssemblyDofReport analyzeAssemblyDof(const Assembly& assembly) {
    AssemblyDofReport report;
    const auto& components = assembly.components();

    std::vector<int> matedCount(components.size(), 0);
    for (const Mate& mate : assembly.mates()) {
        if (mate.componentB >= 0 && mate.componentB < static_cast<int>(components.size())) {
            ++matedCount[static_cast<std::size_t>(mate.componentB)];
        }
    }

    for (std::size_t i = 0; i < components.size(); ++i) {
        if (!components[i].fixed && matedCount[i] == 0) report.unplacedComponentIndices.push_back(static_cast<int>(i));
        if (matedCount[i] > 1) report.multiplyMatedComponentIndices.push_back(static_cast<int>(i));
    }
    return report;
}

} // namespace lcad
