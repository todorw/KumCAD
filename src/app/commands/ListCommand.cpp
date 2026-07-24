#include "commands/ListCommand.h"

#include "core/geometry/Arc.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/HatchPattern.h"
#include "core/geometry/Image.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MLine.h"
#include "core/geometry/MText.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/geometry/PointCloud.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Region.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Table.h"
#include "core/geometry/Text.h"
#include "core/geometry/Tolerance.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/geometry/Wipeout.h"
#include "core/geometry/Wire.h"

#include <cmath>

namespace {

QString num(double v) {
    return QString::number(v, 'f', 4);
}

QString deg(double radians) {
    return QString::number(radians * 180.0 / M_PI, 'f', 2) + QStringLiteral(" deg");
}

QString xy(const lcad::Point2D& p) {
    return QStringLiteral("(%1, %2)").arg(num(p.x), num(p.y));
}

} // namespace

QStringList formatEntityList(const lcad::Document& document, lcad::EntityId id) {
    const lcad::Entity* e = document.findEntity(id);
    if (!e) return {QStringLiteral("*Entity #%1 no longer exists*").arg(id)};

    QStringList lines;
    const lcad::Layer* layer = document.findLayer(e->layer());
    const QString layerName = layer ? QString::fromStdString(layer->name) : QStringLiteral("?");

    switch (e->type()) {
    case lcad::EntityType::Line: {
        const auto& line = static_cast<const lcad::LineEntity&>(*e);
        lines << QStringLiteral("LINE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  from %1 to %2  length %3").arg(xy(line.start()), xy(line.end()),
                                                                  num(line.start().distanceTo(line.end())));
        break;
    }
    case lcad::EntityType::Circle: {
        const auto& circle = static_cast<const lcad::CircleEntity&>(*e);
        lines << QStringLiteral("CIRCLE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  center %1  radius %2  circumference %3")
                     .arg(xy(circle.center()), num(circle.radius()), num(2.0 * M_PI * circle.radius()));
        break;
    }
    case lcad::EntityType::Arc: {
        const auto& arc = static_cast<const lcad::ArcEntity&>(*e);
        lines << QStringLiteral("ARC  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  center %1  radius %2  start %3  end %4")
                     .arg(xy(arc.center()), num(arc.radius()), deg(arc.startAngle()), deg(arc.endAngle()));
        break;
    }
    case lcad::EntityType::Polyline: {
        const auto& pl = static_cast<const lcad::PolylineEntity&>(*e);
        lines << QStringLiteral("LWPOLYLINE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 vertices  %2").arg(pl.vertices().size())
                     .arg(pl.closed() ? QStringLiteral("closed") : QStringLiteral("open"));
        break;
    }
    case lcad::EntityType::Ellipse: {
        const auto& el = static_cast<const lcad::EllipseEntity&>(*e);
        lines << QStringLiteral("ELLIPSE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  center %1  radiusX %2  radiusY %3  rotation %4")
                     .arg(xy(el.center()), num(el.radiusX()), num(el.radiusY()), deg(el.rotation()));
        break;
    }
    case lcad::EntityType::Spline: {
        const auto& spline = static_cast<const lcad::SplineEntity&>(*e);
        lines << QStringLiteral("SPLINE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  degree %1  %2 control point(s)  %3 fit point(s)")
                     .arg(spline.degree())
                     .arg(spline.controlPoints().size())
                     .arg(spline.fitPoints().size());
        break;
    }
    case lcad::EntityType::Text: {
        const auto& text = static_cast<const lcad::TextEntity&>(*e);
        lines << QStringLiteral("TEXT  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1  height %2  rotation %3")
                     .arg(xy(text.position()), num(text.height()), deg(text.rotation()));
        lines << QStringLiteral("  \"%1\"").arg(QString::fromStdString(text.text()));
        break;
    }
    case lcad::EntityType::MText: {
        const auto& mtext = static_cast<const lcad::MTextEntity&>(*e);
        lines << QStringLiteral("MTEXT  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1  height %2  width %3")
                     .arg(xy(mtext.position()), num(mtext.height()), num(mtext.width()));
        lines << QStringLiteral("  \"%1\"").arg(QString::fromStdString(mtext.text()));
        break;
    }
    case lcad::EntityType::Dimension: {
        const auto& dim = static_cast<const lcad::DimensionEntity&>(*e);
        QString kind;
        switch (dim.kind()) {
        case lcad::DimensionKind::Linear: kind = QStringLiteral("Linear"); break;
        case lcad::DimensionKind::Aligned: kind = QStringLiteral("Aligned"); break;
        case lcad::DimensionKind::Radius: kind = QStringLiteral("Radius"); break;
        case lcad::DimensionKind::Diameter: kind = QStringLiteral("Diameter"); break;
        case lcad::DimensionKind::Angular: kind = QStringLiteral("Angular"); break;
        case lcad::DimensionKind::Ordinate: kind = QStringLiteral("Ordinate"); break;
        case lcad::DimensionKind::Jogged: kind = QStringLiteral("Jogged Radius"); break;
        case lcad::DimensionKind::ArcLength: kind = QStringLiteral("Arc Length"); break;
        }
        lines << QStringLiteral("DIMENSION  Handle=%1  Layer=%2  Type=%3").arg(id).arg(layerName, kind);
        lines << QStringLiteral("  %1  point1 %2  point2 %3")
                     .arg(QString::fromStdString(dim.geometry().label), xy(dim.point1()), xy(dim.point2()));
        break;
    }
    case lcad::EntityType::Leader: {
        const auto& leader = static_cast<const lcad::LeaderEntity&>(*e);
        lines << QStringLiteral("LEADER  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 point(s)  arrow size %2").arg(leader.points().size()).arg(num(leader.arrowSize()));
        break;
    }
    case lcad::EntityType::MLeader: {
        const auto& mleader = static_cast<const lcad::MLeaderEntity&>(*e);
        lines << QStringLiteral("MULTILEADER  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  landing %1  %2 leg(s)").arg(xy(mleader.landing())).arg(mleader.legs().size());
        break;
    }
    case lcad::EntityType::Hatch: {
        const auto& hatch = static_cast<const lcad::HatchEntity&>(*e);
        lines << QStringLiteral("%1  Handle=%2  Layer=%3")
                     .arg(hatch.isGradient() ? QStringLiteral("GRADIENT") : QStringLiteral("HATCH"))
                     .arg(id)
                     .arg(layerName);
        lines << QStringLiteral("  pattern %1  %2 boundary vertices")
                     .arg(QLatin1String(lcad::hatchPatternName(hatch.pattern())))
                     .arg(hatch.vertices().size());
        break;
    }
    case lcad::EntityType::Insert: {
        const auto& insert = static_cast<const lcad::InsertEntity&>(*e);
        lines << QStringLiteral("INSERT  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  block \"%1\"  at %2  scale %3  rotation %4")
                     .arg(QString::fromStdString(insert.blockName()), xy(insert.position()),
                          num(insert.scaleFactor()), deg(insert.rotation()));
        for (const auto& [tag, value] : insert.attributes()) {
            lines << QStringLiteral("    %1 = %2").arg(QString::fromStdString(tag), QString::fromStdString(value));
        }
        break;
    }
    case lcad::EntityType::Point: {
        const auto& point = static_cast<const lcad::PointEntity&>(*e);
        lines << QStringLiteral("POINT  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1").arg(xy(point.position()));
        break;
    }
    case lcad::EntityType::ConstructionLine: {
        const auto& cl = static_cast<const lcad::ConstructionLineEntity&>(*e);
        lines << QStringLiteral("%1  Handle=%2  Layer=%3")
                     .arg(cl.isRay() ? QStringLiteral("RAY") : QStringLiteral("XLINE"))
                     .arg(id)
                     .arg(layerName);
        lines << QStringLiteral("  base %1  angle %2")
                     .arg(xy(cl.basePoint()), deg(std::atan2(cl.direction().y, cl.direction().x)));
        break;
    }
    case lcad::EntityType::AttDef: {
        const auto& attdef = static_cast<const lcad::AttDefEntity&>(*e);
        lines << QStringLiteral("ATTDEF  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  tag \"%1\"  prompt \"%2\"  default \"%3\"")
                     .arg(QString::fromStdString(attdef.tag()), QString::fromStdString(attdef.prompt()),
                          QString::fromStdString(attdef.defaultValue()));
        break;
    }
    case lcad::EntityType::Table: {
        const auto& table = static_cast<const lcad::TableEntity&>(*e);
        lines << QStringLiteral("TABLE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1  %2 rows x %3 columns").arg(xy(table.position())).arg(table.rows()).arg(table.cols());
        break;
    }
    case lcad::EntityType::Image: {
        const auto& image = static_cast<const lcad::ImageEntity&>(*e);
        lines << QStringLiteral("IMAGE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1  %2 x %3  rotation %4")
                     .arg(xy(image.position()), num(image.width()), num(image.height()), deg(image.rotation()));
        lines << QStringLiteral("  \"%1\"").arg(QString::fromStdString(image.path()));
        break;
    }
    case lcad::EntityType::PointCloud: {
        const auto& cloud = static_cast<const lcad::PointCloudEntity&>(*e);
        lines << QStringLiteral("POINTCLOUD  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 point(s)  source \"%2\"")
                     .arg(cloud.points().size())
                     .arg(QString::fromStdString(cloud.path()));
        break;
    }
    case lcad::EntityType::Wire: {
        const auto& wire = static_cast<const lcad::WireEntity&>(*e);
        lines << QStringLiteral("WIRE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 vertices").arg(wire.vertices().size());
        break;
    }
    case lcad::EntityType::Junction: {
        const auto& junction = static_cast<const lcad::JunctionEntity&>(*e);
        lines << QStringLiteral("JUNCTION  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1").arg(xy(junction.position()));
        break;
    }
    case lcad::EntityType::NoConnect: {
        const auto& nc = static_cast<const lcad::NoConnectEntity&>(*e);
        lines << QStringLiteral("NOCONNECT  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1").arg(xy(nc.position()));
        break;
    }
    case lcad::EntityType::NetLabel: {
        const auto& label = static_cast<const lcad::NetLabelEntity&>(*e);
        lines << QStringLiteral("NETLABEL  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  \"%1\"  at %2").arg(QString::fromStdString(label.name()), xy(label.position()));
        break;
    }
    case lcad::EntityType::Track: {
        const auto& track = static_cast<const lcad::TrackEntity&>(*e);
        lines << QStringLiteral("TRACK  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 vertices  width %2").arg(track.vertices().size()).arg(num(track.width()));
        break;
    }
    case lcad::EntityType::Via: {
        const auto& via = static_cast<const lcad::ViaEntity&>(*e);
        lines << QStringLiteral("VIA  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1  diameter %2  drill %3")
                     .arg(xy(via.position()), num(via.diameter()), num(via.drillDiameter()));
        break;
    }
    case lcad::EntityType::Wipeout: {
        const auto& wipeout = static_cast<const lcad::WipeoutEntity&>(*e);
        lines << QStringLiteral("WIPEOUT  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 vertices  frame %2")
                     .arg(wipeout.vertices().size())
                     .arg(wipeout.showFrame() ? QStringLiteral("on") : QStringLiteral("off"));
        break;
    }
    case lcad::EntityType::MLine: {
        const auto& mline = static_cast<const lcad::MLineEntity&>(*e);
        lines << QStringLiteral("MLINE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 vertices  %2 element(s)  scale %3  %4")
                     .arg(mline.vertices().size())
                     .arg(mline.elements().size())
                     .arg(num(mline.scale()))
                     .arg(mline.closed() ? QStringLiteral("closed") : QStringLiteral("open"));
        break;
    }
    case lcad::EntityType::Region: {
        const auto& region = static_cast<const lcad::RegionEntity&>(*e);
        lines << QStringLiteral("REGION  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  %1 loop(s)  area %2").arg(region.loops().size()).arg(num(region.area()));
        break;
    }
    case lcad::EntityType::Tolerance: {
        const auto& tolerance = static_cast<const lcad::ToleranceEntity&>(*e);
        lines << QStringLiteral("TOLERANCE  Handle=%1  Layer=%2").arg(id).arg(layerName);
        lines << QStringLiteral("  at %1  %2 row(s)").arg(xy(tolerance.position())).arg(tolerance.rows().size());
        for (const auto& row : tolerance.rows()) {
            lines << QStringLiteral("    %1").arg(QString::fromStdString(lcad::ToleranceEntity::rowText(row)));
        }
        break;
    }
    }

    return lines;
}
