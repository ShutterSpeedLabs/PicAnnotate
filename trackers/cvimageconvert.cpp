#include "cvimageconvert.h"

#include <opencv2/imgproc.hpp>

cv::Mat qImageToBgrMat(const QImage &image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    const cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3,
                          const_cast<uchar *>(rgb.bits()), static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat bgrMat;
    cv::cvtColor(rgbMat, bgrMat, cv::COLOR_RGB2BGR);
    return bgrMat;
}
