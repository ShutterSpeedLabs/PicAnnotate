#include "skeletontemplate.h"
#include "keypointtemplate.h"

QVector<QPair<int, int>> SkeletonTemplate::coco17Edges()
{
    return KeypointTemplate::coco17().edges;
}

QVector<QPointF> SkeletonTemplate::coco17DefaultLayout(const QPointF &center)
{
    return KeypointTemplate::coco17().layoutAt(center);
}
