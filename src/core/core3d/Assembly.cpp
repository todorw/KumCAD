#include "core/core3d/Assembly.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
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

        if (m.type == MateType::Parallel || m.type == MateType::Perpendicular || m.type == MateType::Tangent ||
            m.type == MateType::AxisTangentExternal || m.type == MateType::AxisTangentInternal) {
            const gp_Dir currentDirB = localDirB.Transformed(compB.placement);
            gp_Dir target = worldDirA;

            if (m.type == MateType::Perpendicular || m.type == MateType::Tangent) {
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

            if (m.type == MateType::Tangent) {
                // Slide componentB along worldDirA only (never within the
                // plane, which the rotation above already left untouched)
                // so its reference axis point ends up exactly `value`
                // (the cylinder radius) from componentA's plane.
                const gp_Pnt worldPointB = localPointB.Transformed(compB.placement);
                const double currentDist = gp_Vec(worldPointA, worldPointB).Dot(gp_Vec(worldDirA));
                gp_Trsf slide;
                slide.SetTranslation(gp_Vec(worldDirA) * (m.value - currentDist));
                compB.placement = slide.Multiplied(compB.placement);
            } else if (m.type == MateType::AxisTangentExternal || m.type == MateType::AxisTangentInternal) {
                // The rotation above already made componentB's axis
                // parallel to worldDirA; now correct only the PERPENDICULAR
                // offset between the two axes, preserving both componentB's
                // position along the shared axis and which radial side of
                // componentA's axis it's currently on (closest-to-current,
                // same philosophy as Perpendicular/Tangent's rotation).
                const gp_Pnt worldPointB = localPointB.Transformed(compB.placement);
                const gp_Vec toB(worldPointA, worldPointB);
                const gp_Vec axisComp = gp_Vec(worldDirA) * toB.Dot(gp_Vec(worldDirA));
                gp_Vec perpComp = toB - axisComp;
                gp_Dir perpDir;
                if (perpComp.Magnitude() > 1e-9) {
                    perpDir = gp_Dir(perpComp);
                } else {
                    // Axes currently coincide -- no existing radial side to
                    // preserve, so pick an arbitrary perpendicular direction
                    // the same way Perpendicular's own degenerate case does.
                    gp_Dir helper(0, 0, 1);
                    if (std::abs(worldDirA.Dot(helper)) > 0.9) helper = gp_Dir(1, 0, 0);
                    perpDir = gp_Dir(worldDirA.Crossed(helper));
                }
                const double targetPerp = (m.type == MateType::AxisTangentExternal) ? (m.value + m.value2)
                                                                                     : std::abs(m.value - m.value2);
                const gp_Pnt targetWorldPointB = worldPointA.Translated(axisComp + gp_Vec(perpDir) * targetPerp);
                gp_Trsf slide;
                slide.SetTranslation(gp_Vec(worldPointB, targetWorldPointB));
                compB.placement = slide.Multiplied(compB.placement);
            }
            continue;
        }

        if (m.type == MateType::Slider) {
            // Position-only: slide componentB's reference point along
            // worldDirA until its distance from worldPointA is exactly
            // `value`, touching nothing else (no rotation, no off-axis
            // move) -- see MateType::Slider's own comment.
            const gp_Pnt worldPointB = localPointB.Transformed(compB.placement);
            const double currentDist = gp_Vec(worldPointA, worldPointB).Dot(gp_Vec(worldDirA));
            gp_Trsf slide;
            slide.SetTranslation(gp_Vec(worldDirA) * (m.value - currentDist));
            compB.placement = slide.Multiplied(compB.placement);
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

        if (m.type == MateType::Angle || m.type == MateType::Fixed) {
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

std::vector<PartsListEntry> buildPartsList(const Assembly& assembly) {
    std::vector<PartsListEntry> entries;
    for (const AssemblyComponent& component : assembly.components()) {
        auto it = std::find_if(entries.begin(), entries.end(),
                               [&](const PartsListEntry& e) { return e.name == component.name; });
        if (it != entries.end()) {
            ++it->quantity;
        } else {
            entries.push_back({component.name, 1});
        }
    }
    return entries;
}

std::vector<int> patternComponent(Assembly& assembly, int sourceIndex, const ComponentPatternParams& params) {
    std::vector<int> added;
    const auto& components = assembly.components();
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(components.size()) || params.count < 2) return added;

    const AssemblyComponent source = components[static_cast<std::size_t>(sourceIndex)]; // copy: addComponent below may reallocate the vector

    if (params.kind == ComponentPatternKind::Linear) {
        const double dirMag = std::sqrt(params.dirX * params.dirX + params.dirY * params.dirY + params.dirZ * params.dirZ);
        if (dirMag < 1e-9) return added;
        for (int i = 1; i < params.count; ++i) {
            const gp_Vec step(params.dirX / dirMag * params.spacing * i, params.dirY / dirMag * params.spacing * i,
                              params.dirZ / dirMag * params.spacing * i);
            gp_Trsf trsf;
            trsf.SetTranslation(step);

            AssemblyComponent copy = source;
            copy.name = source.name + " (" + std::to_string(i + 1) + ")";
            copy.fixed = true;
            copy.placement = trsf.Multiplied(source.placement);
            added.push_back(assembly.addComponent(std::move(copy)));
        }
    } else {
        const double axisMag = std::sqrt(params.axisX * params.axisX + params.axisY * params.axisY + params.axisZ * params.axisZ);
        if (axisMag < 1e-9) return added;
        const gp_Ax1 axis(gp_Pnt(params.originX, params.originY, params.originZ),
                          gp_Dir(params.axisX / axisMag, params.axisY / axisMag, params.axisZ / axisMag));
        // totalAngle/count (not Document3D's own PolarPattern's
        // totalAngle/(count-1)) -- see ComponentPatternParams' own
        // comment on why a whole-component pattern deliberately uses a
        // different, real-AutoCAD-ARRAYPOLAR-matching convention here.
        const double angleStep = (params.totalAngleDegrees * M_PI / 180.0) / static_cast<double>(params.count);
        for (int i = 1; i < params.count; ++i) {
            gp_Trsf trsf;
            trsf.SetRotation(axis, angleStep * i);

            AssemblyComponent copy = source;
            copy.name = source.name + " (" + std::to_string(i + 1) + ")";
            copy.fixed = true;
            copy.placement = trsf.Multiplied(source.placement);
            added.push_back(assembly.addComponent(std::move(copy)));
        }
    }
    return added;
}

std::vector<TopoDS_Shape> assemblyPlacedShapes(const Assembly& assembly) {
    const auto& components = assembly.components();
    std::vector<TopoDS_Shape> placed(components.size());
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (components[i].shape.IsNull()) continue;
        placed[i] = BRepBuilderAPI_Transform(components[i].shape, components[i].placement, true).Shape();
    }
    return placed;
}

std::vector<InterferencePair> detectInterferences(const Assembly& assembly) {
    std::vector<InterferencePair> result;
    constexpr double kVolumeEpsilon = 1e-6;

    const std::vector<TopoDS_Shape> placed = assemblyPlacedShapes(assembly);

    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (placed[i].IsNull()) continue;
        for (std::size_t j = i + 1; j < placed.size(); ++j) {
            if (placed[j].IsNull()) continue;
            BRepAlgoAPI_Common common(placed[i], placed[j]);
            if (!common.IsDone()) continue;
            GProp_GProps props;
            BRepGProp::VolumeProperties(common.Shape(), props);
            const double volume = props.Mass();
            if (volume > kVolumeEpsilon) {
                result.push_back({static_cast<int>(i), static_cast<int>(j), volume});
            }
        }
    }
    return result;
}

} // namespace lcad
