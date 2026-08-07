#ifndef CVIMAGECONVERT_H
#define CVIMAGECONVERT_H

#include <QImage>
#include <opencv2/core.hpp>

cv::Mat qImageToBgrMat(const QImage &image);

#endif // CVIMAGECONVERT_H
