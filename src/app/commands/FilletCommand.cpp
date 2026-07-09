#include "commands/FilletCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Line.h"

#include <cmath>

namespace {

// Intersection of the two INFINITE lines; nullopt if parallel.
std::optional<lcad::Point2D> infiniteIntersection(const lcad::LineEntity& l1, const lcad::LineEntity& l2) {
    const lcad::Point2D r = l1.end() - l1.start();
    const lcad::Point2D s = l2.end() - l2.start();
    const double denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-12) return std::nullopt;
    const lcad::Point2D qp = l2.start() - l1.start();
    const double t = (qp.x * s.y - qp.y * s.x) / denom;
    return l1.start() + r * t;
}

lcad::Point2D normalized(const lcad::Point2D& v) {
    const double len = v.length();
    return len > 1e-12 ? v * (1.0 / len) : lcad::Point2D(1, 0);
}

} // namespace

QString FilletCommand::start() {
    return QStringLiteral("FILLET  Specify fillet radius or [Enter for sharp corner]:");
}

std::optional<QString> FilletCommand::onPoint(const lcad::Point2D& pt) {
    (void)pt;
    return std::nullopt; // radius comes typed; points are not meaningful here
}

std::optional<QString> FilletCommand::onScalar(double value) {
    if (value < 0) return std::nullopt;
    m_result = apply(value);
    m_finished = true;
    return m_result; // printed by the dispatcher's finished-with-prompt path
}

bool FilletCommand::requestFinish() {
    if (!m_finished) m_result = apply(0.0);
    m_finished = true;
    return true;
}

QString FilletCommand::apply(double radius) {
    lcad::Entity* e1 = m_document.findEntity(m_line1);
    lcad::Entity* e2 = m_document.findEntity(m_line2);
    if (!e1 || !e2 || e1->type() != lcad::EntityType::Line || e2->type() != lcad::EntityType::Line) {
        return QStringLiteral("*FILLET needs two lines*");
    }
    const auto& l1 = static_cast<const lcad::LineEntity&>(*e1);
    const auto& l2 = static_cast<const lcad::LineEntity&>(*e2);

    const auto cornerOpt = infiniteIntersection(l1, l2);
    if (!cornerOpt) return QStringLiteral("*Lines are parallel; cannot fillet*");
    const lcad::Point2D corner = *cornerOpt;

    // Keep each line's endpoint that is farther from the corner; the nearer
    // endpoint is the one that gets moved (to the corner or a tangent point).
    const bool keep1End = corner.distanceTo(l1.end()) >= corner.distanceTo(l1.start());
    const bool keep2End = corner.distanceTo(l2.end()) >= corner.distanceTo(l2.start());
    const lcad::Point2D far1 = keep1End ? l1.end() : l1.start();
    const lcad::Point2D far2 = keep2End ? l2.end() : l2.start();
    const lcad::Point2D near1 = keep1End ? l1.start() : l1.end();
    const lcad::Point2D near2 = keep2End ? l2.start() : l2.end();
    const std::size_t grip1 = keep1End ? 0 : 1; // index of the endpoint being moved
    const std::size_t grip2 = keep2End ? 0 : 1;

    auto batch = std::make_unique<lcad::BatchCommand>("Fillet");

    if (radius < 1e-9) {
        batch->add(std::make_unique<lcad::MoveGripCommand>(m_document, m_line1, grip1, near1, corner));
        batch->add(std::make_unique<lcad::MoveGripCommand>(m_document, m_line2, grip2, near2, corner));
        m_document.commandStack().execute(std::move(batch));
        return QStringLiteral("*Corner joined*");
    }

    const lcad::Point2D d1 = normalized(far1 - corner);
    const lcad::Point2D d2 = normalized(far2 - corner);
    const double cosAngle = std::clamp(d1.dot(d2), -1.0, 1.0);
    const double angle = std::acos(cosAngle);
    if (angle < 1e-6 || angle > M_PI - 1e-6) return QStringLiteral("*Lines are collinear; cannot fillet*");

    const double tangentDist = radius / std::tan(angle / 2.0);
    if (tangentDist > corner.distanceTo(far1) - 1e-9 || tangentDist > corner.distanceTo(far2) - 1e-9) {
        return QStringLiteral("*Radius too large for these lines*");
    }

    const lcad::Point2D t1 = corner + d1 * tangentDist;
    const lcad::Point2D t2 = corner + d2 * tangentDist;
    const lcad::Point2D bisector = normalized(d1 + d2);
    const lcad::Point2D center = corner + bisector * (radius / std::sin(angle / 2.0));

    double a1 = std::atan2(t1.y - center.y, t1.x - center.x);
    double a2 = std::atan2(t2.y - center.y, t2.x - center.x);
    // The fillet arc is the short way around; our arcs sweep CCW start-to-end.
    double sweep = std::fmod(a2 - a1, 2.0 * M_PI);
    if (sweep < 0) sweep += 2.0 * M_PI;
    if (sweep > M_PI) std::swap(a1, a2);

    batch->add(std::make_unique<lcad::MoveGripCommand>(m_document, m_line1, grip1, near1, t1));
    batch->add(std::make_unique<lcad::MoveGripCommand>(m_document, m_line2, grip2, near2, t2));
    batch->add(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::ArcEntity>(m_document.reserveEntityId(), e1->layer(), center, radius, a1, a2)));
    m_document.commandStack().execute(std::move(batch));
    return QStringLiteral("*Filleted with radius %1*").arg(radius);
}
