#include "iogeometry.h"

#include <QtGlobal>

#include <cmath>

namespace IoGeometry {

double clamp01(double value)
{
    return qBound(0.0, value, 1.0);
}

QRectF clampRectToImage(const QRectF &rect, const QSize &imageSize)
{
    if (!imageSize.isValid() || imageSize.isEmpty())
        return rect.normalized();

    const QRectF bounds(0, 0, imageSize.width(), imageSize.height());
    return rect.normalized().intersected(bounds);
}

QVector<QPointF> clampPointsToImage(const QVector<QPointF> &points, const QSize &imageSize)
{
    if (!imageSize.isValid() || imageSize.isEmpty())
        return points;

    const double maxX = imageSize.width();
    const double maxY = imageSize.height();

    QVector<QPointF> clamped;
    clamped.reserve(points.size());
    for (const QPointF &p : points)
        clamped.append(QPointF(qBound(0.0, p.x(), maxX), qBound(0.0, p.y(), maxY)));
    return clamped;
}

QRectF boundsOfPoints(const QVector<QPointF> &points)
{
    if (points.isEmpty())
        return QRectF();

    // Accumulated by hand rather than with QRectF::united(): a zero-size rect
    // around a single point is null by Qt's definition, and united() discards
    // null rectangles, which would collapse the result to nothing.
    double minX = points.first().x();
    double maxX = minX;
    double minY = points.first().y();
    double maxY = minY;

    for (const QPointF &p : points) {
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

double polygonArea(const QVector<QPointF> &points)
{
    if (points.size() < 3)
        return 0.0;

    double sum = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &current = points.at(i);
        const QPointF &next = points.at((i + 1) % points.size());
        sum += current.x() * next.y() - next.x() * current.y();
    }
    return std::fabs(sum) * 0.5;
}

QString formatNormalized(double value, int precision)
{
    QString text = QString::number(value, 'f', precision);

    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    return text.isEmpty() ? QStringLiteral("0") : text;
}

QVector<QPointF> denormalizePoints(const QVector<double> &normalized, const QSize &imageSize)
{
    QVector<QPointF> points;
    if (!imageSize.isValid() || imageSize.isEmpty())
        return points;

    points.reserve(normalized.size() / 2);
    for (int i = 0; i + 1 < normalized.size(); i += 2) {
        points.append(QPointF(normalized.at(i) * imageSize.width(),
                              normalized.at(i + 1) * imageSize.height()));
    }
    return points;
}

bool isExportableRect(const QRectF &rect, double minSide)
{
    const QRectF normalized = rect.normalized();
    return normalized.width() >= minSide && normalized.height() >= minSide;
}

} // namespace IoGeometry
