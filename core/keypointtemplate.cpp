#include "keypointtemplate.h"

#include <QJsonArray>
#include <QJsonObject>

bool KeypointTemplate::isValid() const
{
    if (id.isEmpty() || pointNames.isEmpty())
        return false;

    for (const auto &edge : edges) {
        if (edge.first < 0 || edge.first >= pointNames.size())
            return false;
        if (edge.second < 0 || edge.second >= pointNames.size())
            return false;
    }
    return true;
}

QVector<QPointF> KeypointTemplate::layoutAt(const QPointF &center) const
{
    QVector<QPointF> points;
    points.reserve(pointNames.size());

    for (int i = 0; i < pointNames.size(); ++i) {
        // Templates imported from a dataset have no artwork to place from, so
        // fan the points out in a small grid the user can then drag apart.
        const QPointF offset = i < defaultOffsets.size()
            ? defaultOffsets.at(i)
            : QPointF((i % 5) * 14 - 28, (i / 5) * 14 - 28);
        points.append(center + offset);
    }
    return points;
}

QVector<QPair<int, int>> KeypointTemplate::oneBasedEdges() const
{
    QVector<QPair<int, int>> result;
    result.reserve(edges.size());
    for (const auto &edge : edges)
        result.append({edge.first + 1, edge.second + 1});
    return result;
}

QJsonObject KeypointTemplate::toJson() const
{
    QJsonArray namesArray;
    for (const QString &pointName : pointNames)
        namesArray.append(pointName);

    QJsonArray edgesArray;
    for (const auto &edge : edges)
        edgesArray.append(QJsonArray{edge.first, edge.second});

    QJsonArray offsetsArray;
    for (const QPointF &offset : defaultOffsets)
        offsetsArray.append(QJsonArray{offset.x(), offset.y()});

    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["pointNames"] = namesArray;
    obj["edges"] = edgesArray;
    obj["defaultOffsets"] = offsetsArray;
    return obj;
}

KeypointTemplate KeypointTemplate::fromJson(const QJsonObject &obj)
{
    KeypointTemplate tmpl;
    tmpl.id = obj["id"].toString();
    tmpl.name = obj["name"].toString();

    const QJsonArray namesArray = obj["pointNames"].toArray();
    for (const QJsonValue &v : namesArray)
        tmpl.pointNames.append(v.toString());

    const QJsonArray edgesArray = obj["edges"].toArray();
    for (const QJsonValue &v : edgesArray) {
        const QJsonArray pair = v.toArray();
        if (pair.size() == 2)
            tmpl.edges.append({pair.at(0).toInt(), pair.at(1).toInt()});
    }

    const QJsonArray offsetsArray = obj["defaultOffsets"].toArray();
    for (const QJsonValue &v : offsetsArray) {
        const QJsonArray pair = v.toArray();
        if (pair.size() == 2)
            tmpl.defaultOffsets.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
    }

    return tmpl;
}

QString KeypointTemplate::coco17Id()
{
    return QStringLiteral("coco17");
}

KeypointTemplate KeypointTemplate::coco17()
{
    KeypointTemplate tmpl;
    tmpl.id = coco17Id();
    tmpl.name = QStringLiteral("COCO 17 (person)");
    tmpl.pointNames = {
        "nose", "left_eye", "right_eye", "left_ear", "right_ear",
        "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
        "left_wrist", "right_wrist", "left_hip", "right_hip",
        "left_knee", "right_knee", "left_ankle", "right_ankle",
    };
    tmpl.edges = {
        {0, 1}, {0, 2}, {1, 3}, {2, 4},
        {5, 6},
        {5, 7}, {7, 9}, {6, 8}, {8, 10},
        {5, 11}, {6, 12}, {11, 12},
        {11, 13}, {13, 15}, {12, 14}, {14, 16},
    };
    tmpl.defaultOffsets = {
        {0, -60}, {-5, -65}, {5, -65}, {-10, -62}, {10, -62},
        {-20, -40}, {20, -40}, {-30, -15}, {30, -15},
        {-35, 10}, {35, 10}, {-15, 10}, {15, 10},
        {-18, 45}, {18, 45}, {-20, 80}, {20, 80},
    };
    return tmpl;
}

KeypointTemplate KeypointTemplate::fromNames(const QString &id, const QString &name,
                                             const QStringList &pointNames,
                                             const QVector<QPair<int, int>> &edges)
{
    KeypointTemplate tmpl;
    tmpl.id = id;
    tmpl.name = name.isEmpty() ? id : name;
    tmpl.pointNames = pointNames;

    for (const auto &edge : edges) {
        if (edge.first >= 0 && edge.first < pointNames.size()
            && edge.second >= 0 && edge.second < pointNames.size()) {
            tmpl.edges.append(edge);
        }
    }

    // A COCO-17 name list is common enough that reusing its layout and edges
    // gives imported datasets a sensible skeleton for free.
    if (tmpl.edges.isEmpty() && pointNames == coco17().pointNames) {
        const KeypointTemplate reference = coco17();
        tmpl.edges = reference.edges;
        tmpl.defaultOffsets = reference.defaultOffsets;
    }
    return tmpl;
}
