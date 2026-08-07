#ifndef IOGEOMETRY_H
#define IOGEOMETRY_H

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

// Geometry helpers shared by every dataset writer/reader. Centralising the
// clamping and normalisation rules keeps the formats consistent with each other.
namespace IoGeometry {

double clamp01(double value);

// Clips a box to the image. Datasets reject out-of-bounds coordinates, and
// hand-drawn boxes routinely spill a pixel or two past the edge.
QRectF clampRectToImage(const QRectF &rect, const QSize &imageSize);
QVector<QPointF> clampPointsToImage(const QVector<QPointF> &points, const QSize &imageSize);

QRectF boundsOfPoints(const QVector<QPointF> &points);

// Absolute polygon area via the shoelace formula, which is what COCO's `area`
// field expects for polygon segmentations.
double polygonArea(const QVector<QPointF> &points);

// Fixed-precision text for label files; trailing zeros are trimmed so the files
// stay readable and diffable.
QString formatNormalized(double value, int precision = 6);

QVector<QPointF> denormalizePoints(const QVector<double> &normalized, const QSize &imageSize);

// True when a box is large enough to be worth exporting. Sub-pixel boxes are
// usually stray clicks and make training data worse.
bool isExportableRect(const QRectF &rect, double minSide = 1.0);

} // namespace IoGeometry

#endif // IOGEOMETRY_H
