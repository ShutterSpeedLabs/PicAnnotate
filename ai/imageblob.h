#ifndef IMAGEBLOB_H
#define IMAGEBLOB_H

#include "tensor.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QVector>

#include <opencv2/core.hpp>

// Preprocessing shared by every model here. YOLO, SAM and CLIP all want an
// image resized into a fixed square, normalised, and laid out NCHW — they differ
// only in the constants — and every one of them needs to map coordinates back
// afterwards. Getting that mapping wrong shifts every box by the padding, which
// looks almost right, so it lives in one tested place.
namespace ImageBlob {

// How a source image was fitted into the network's input square.
//
// SAM and YOLO pad differently — YOLO centres the image, SAM pins it to the top
// left — so the offsets are stored rather than assumed.
struct LetterboxTransform
{
    double scale = 1.0;    // source pixels -> network pixels
    double padX = 0.0;     // network-space offset of the image's left edge
    double padY = 0.0;
    QSize sourceSize;
    QSize networkSize;

    // Network coordinates back to source-image pixels, and the reverse.
    QPointF toSource(const QPointF &networkPoint) const;
    QPointF toNetwork(const QPointF &sourcePoint) const;
    QRectF toSource(const QRectF &networkRect) const;
    QRectF toNetwork(const QRectF &sourceRect) const;
};

// Scales `src` to fit inside `target` preserving aspect ratio, padding the rest
// with `padValue`. With `centred` false the image sits at the top-left, which is
// what SAM's preprocessing does.
LetterboxTransform letterbox(const cv::Mat &src, cv::Mat *dst, const cv::Size &target,
                             const cv::Scalar &padValue = cv::Scalar(114, 114, 114),
                             bool centred = true);

// Packs an 8-bit image into an NCHW float32 tensor of shape 1xCxHxW.
//
//   value = (pixel / scaleDivisor - mean[c]) / stdDev[c]
//
// so YOLO passes scaleDivisor 255 with zero mean and unit deviation, CLIP passes
// its ImageNet statistics, and SAM passes raw 0-255 with its own mean/std.
Tensor toNchwFloat(const cv::Mat &image, bool swapRedBlue,
                   double scaleDivisor = 255.0,
                   const cv::Scalar &mean = cv::Scalar(0, 0, 0),
                   const cv::Scalar &stdDev = cv::Scalar(1, 1, 1));

// Largest connected outline in a binary mask, simplified with Douglas-Peucker.
// `epsilonFactor` is a fraction of the contour perimeter: 0.002 keeps curves
// smooth, 0.01 gives a coarse polygon. Empty when the mask has no foreground.
QVector<QPointF> largestContourPolygon(const cv::Mat &binaryMask, double epsilonFactor = 0.002,
                                       int minimumArea = 16);

// Every outline in the mask, largest first. Used where a single instance mask
// legitimately has disjoint parts and the caller wants to keep them.
QVector<QVector<QPointF>> contourPolygons(const cv::Mat &binaryMask,
                                          double epsilonFactor = 0.002,
                                          int minimumArea = 16);

// Axis-aligned bounds of the mask's foreground, empty when there is none.
QRectF maskBounds(const cv::Mat &binaryMask);

} // namespace ImageBlob

#endif // IMAGEBLOB_H
