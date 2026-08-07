#ifndef KEYPOINTTEMPLATE_H
#define KEYPOINTTEMPLATE_H

#include <QPair>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

class QJsonObject;

// Describes one skeleton layout: the ordered, named points plus the edges drawn
// between them. The order is the contract with every keypoint format — COCO
// `categories[].keypoints` and YOLO-pose triplets are both positional — so it
// must never be reordered once annotations exist against it.
struct KeypointTemplate
{
    QString id;
    QString name;
    QStringList pointNames;
    QVector<QPair<int, int>> edges;    // 0-based indices into pointNames
    QVector<QPointF> defaultOffsets;   // placement offsets from the click point

    int pointCount() const { return pointNames.size(); }
    bool isValid() const;

    // Point positions for a freshly placed skeleton centred on `center`.
    QVector<QPointF> layoutAt(const QPointF &center) const;

    // COCO writes skeleton edges as 1-based index pairs.
    QVector<QPair<int, int>> oneBasedEdges() const;

    QJsonObject toJson() const;
    static KeypointTemplate fromJson(const QJsonObject &obj);

    static QString coco17Id();
    static KeypointTemplate coco17();

    // Builds a usable template from just a name list (e.g. when importing a
    // COCO category that has `keypoints` but no `skeleton`).
    static KeypointTemplate fromNames(const QString &id, const QString &name,
                                      const QStringList &pointNames,
                                      const QVector<QPair<int, int>> &edges = QVector<QPair<int, int>>());
};

#endif // KEYPOINTTEMPLATE_H
