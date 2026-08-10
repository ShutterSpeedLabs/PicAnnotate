#include "imageblob.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ImageBlob {

QPointF LetterboxTransform::toSource(const QPointF &networkPoint) const
{
    if (scale <= 0.0)
        return networkPoint;
    return QPointF((networkPoint.x() - padX) / scale, (networkPoint.y() - padY) / scale);
}

QPointF LetterboxTransform::toNetwork(const QPointF &sourcePoint) const
{
    return QPointF(sourcePoint.x() * scale + padX, sourcePoint.y() * scale + padY);
}

QRectF LetterboxTransform::toSource(const QRectF &networkRect) const
{
    const QPointF topLeft = toSource(networkRect.topLeft());
    const QPointF bottomRight = toSource(networkRect.bottomRight());
    return QRectF(topLeft, bottomRight).normalized();
}

QRectF LetterboxTransform::toNetwork(const QRectF &sourceRect) const
{
    const QPointF topLeft = toNetwork(sourceRect.topLeft());
    const QPointF bottomRight = toNetwork(sourceRect.bottomRight());
    return QRectF(topLeft, bottomRight).normalized();
}

LetterboxTransform letterbox(const cv::Mat &src, cv::Mat *dst, const cv::Size &target,
                             const cv::Scalar &padValue, bool centred)
{
    LetterboxTransform transform;
    if (src.empty() || target.width <= 0 || target.height <= 0)
        return transform;

    transform.sourceSize = QSize(src.cols, src.rows);
    transform.networkSize = QSize(target.width, target.height);
    transform.scale = std::min(static_cast<double>(target.width) / src.cols,
                               static_cast<double>(target.height) / src.rows);

    // Round rather than truncate: a 1px difference between the size used here
    // and the size implied by `scale` shows up as a systematic offset in every
    // coordinate mapped back.
    const int scaledWidth = std::max(1, static_cast<int>(std::round(src.cols * transform.scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::round(src.rows * transform.scale)));

    if (centred) {
        transform.padX = (target.width - scaledWidth) / 2.0;
        transform.padY = (target.height - scaledHeight) / 2.0;
    }

    if (!dst)
        return transform;

    cv::Mat resized;
    // INTER_AREA is the right filter when shrinking, which is the usual case for
    // a 4K frame going into a 640px network; it avoids the aliasing INTER_LINEAR
    // leaves behind on thin structures.
    const int interpolation = transform.scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR;
    cv::resize(src, resized, cv::Size(scaledWidth, scaledHeight), 0, 0, interpolation);

    *dst = cv::Mat(target, src.type(), padValue);
    const int offsetX = static_cast<int>(std::round(transform.padX));
    const int offsetY = static_cast<int>(std::round(transform.padY));
    resized.copyTo((*dst)(cv::Rect(offsetX, offsetY, scaledWidth, scaledHeight)));

    return transform;
}

Tensor toNchwFloat(const cv::Mat &image, bool swapRedBlue, double scaleDivisor,
                   const cv::Scalar &mean, const cv::Scalar &stdDev)
{
    Tensor tensor;
    if (image.empty())
        return tensor;

    cv::Mat prepared;
    if (swapRedBlue)
        cv::cvtColor(image, prepared, cv::COLOR_BGR2RGB);
    else
        prepared = image;

    const int channels = prepared.channels();
    const int height = prepared.rows;
    const int width = prepared.cols;

    cv::Mat asFloat;
    prepared.convertTo(asFloat, CV_32F, scaleDivisor != 0.0 ? 1.0 / scaleDivisor : 1.0);

    // Split into planes: NCHW wants all of channel 0, then all of channel 1, so
    // a plane-wise copy is both the correct layout and the fast one.
    std::vector<cv::Mat> planes;
    cv::split(asFloat, planes);

    std::vector<float> data(static_cast<size_t>(channels) * height * width);
    for (int c = 0; c < channels; ++c) {
        const double channelMean = mean[std::min(c, 3)];
        const double channelStd = stdDev[std::min(c, 3)];
        cv::Mat plane = planes[static_cast<size_t>(c)];
        if (channelMean != 0.0 || channelStd != 1.0) {
            plane = (plane - channelMean) / (channelStd != 0.0 ? channelStd : 1.0);
        }

        // Rows of a split plane are contiguous, but the plane as a whole may not
        // be if it came from a submatrix; copy row by row to stay correct.
        for (int y = 0; y < height; ++y) {
            const float *rowPtr = plane.ptr<float>(y);
            std::copy(rowPtr, rowPtr + width,
                      data.begin() + (static_cast<size_t>(c) * height + y) * width);
        }
    }

    return Tensor::fromFloats({1, channels, height, width}, std::move(data));
}

namespace {

// Contours of the mask's foreground, sorted largest-area first and filtered by
// `minimumArea` so single-pixel speckle never becomes a polygon.
std::vector<std::vector<cv::Point>> sortedContours(const cv::Mat &binaryMask, int minimumArea)
{
    std::vector<std::vector<cv::Point>> contours;
    if (binaryMask.empty())
        return contours;

    cv::Mat mask8u;
    if (binaryMask.type() != CV_8UC1)
        binaryMask.convertTo(mask8u, CV_8UC1);
    else
        mask8u = binaryMask;

    // RETR_EXTERNAL: holes inside an instance are not separate regions, and
    // AnnoShape has no concept of a polygon with a hole to put them in.
    cv::findContours(mask8u, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    contours.erase(std::remove_if(contours.begin(), contours.end(),
                                  [minimumArea](const std::vector<cv::Point> &contour) {
                                      return contour.size() < 3
                                             || cv::contourArea(contour) < minimumArea;
                                  }),
                   contours.end());

    std::sort(contours.begin(), contours.end(),
              [](const std::vector<cv::Point> &a, const std::vector<cv::Point> &b) {
                  return cv::contourArea(a) > cv::contourArea(b);
              });

    return contours;
}

QVector<QPointF> simplify(const std::vector<cv::Point> &contour, double epsilonFactor)
{
    std::vector<cv::Point> simplified;
    const double epsilon = std::max(0.5, epsilonFactor * cv::arcLength(contour, true));
    cv::approxPolyDP(contour, simplified, epsilon, true);

    // approxPolyDP can collapse a thin sliver below three points; the unsimplified
    // contour is still a usable polygon, so fall back to it rather than dropping
    // the instance entirely.
    const std::vector<cv::Point> &source = simplified.size() >= 3 ? simplified : contour;

    QVector<QPointF> points;
    points.reserve(static_cast<int>(source.size()));
    for (const cv::Point &point : source)
        points.append(QPointF(point.x, point.y));
    return points;
}

} // namespace

QVector<QPointF> largestContourPolygon(const cv::Mat &binaryMask, double epsilonFactor,
                                       int minimumArea)
{
    const std::vector<std::vector<cv::Point>> contours = sortedContours(binaryMask, minimumArea);
    if (contours.empty())
        return {};
    return simplify(contours.front(), epsilonFactor);
}

QVector<QVector<QPointF>> contourPolygons(const cv::Mat &binaryMask, double epsilonFactor,
                                          int minimumArea)
{
    QVector<QVector<QPointF>> result;
    for (const std::vector<cv::Point> &contour : sortedContours(binaryMask, minimumArea)) {
        const QVector<QPointF> polygon = simplify(contour, epsilonFactor);
        if (polygon.size() >= 3)
            result.append(polygon);
    }
    return result;
}

QRectF maskBounds(const cv::Mat &binaryMask)
{
    if (binaryMask.empty())
        return QRectF();

    cv::Mat mask8u;
    if (binaryMask.type() != CV_8UC1)
        binaryMask.convertTo(mask8u, CV_8UC1);
    else
        mask8u = binaryMask;

    const cv::Rect bounds = cv::boundingRect(mask8u);
    if (bounds.width <= 0 || bounds.height <= 0)
        return QRectF();

    return QRectF(bounds.x, bounds.y, bounds.width, bounds.height);
}

} // namespace ImageBlob
