#ifndef SKELETONTEMPLATE_H
#define SKELETONTEMPLATE_H

#include <QPair>
#include <QPointF>
#include <QVector>

// Thin compatibility layer over KeypointTemplate::coco17(). New code should ask
// LabelSchema::templateForClass() so skeletons follow the project's own
// definitions instead of a hardcoded one.
namespace SkeletonTemplate {

constexpr int kCoco17PointCount = 17;

QVector<QPair<int, int>> coco17Edges();
QVector<QPointF> coco17DefaultLayout(const QPointF &center);

} // namespace SkeletonTemplate

#endif // SKELETONTEMPLATE_H
