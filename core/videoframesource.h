#ifndef VIDEOFRAMESOURCE_H
#define VIDEOFRAMESOURCE_H

#include "framesource.h"

#include <opencv2/videoio.hpp>

class VideoFrameSource : public FrameSource
{
public:
    bool openVideo(const QString &path, QString *error);

    int frameCount() const override;
    QImage frameAt(int index) const override;
    QString frameLabel(int index) const override;
    QSize frameSize(int index) const override;
    double frameRate() const override;

private:
    mutable cv::VideoCapture m_capture;
    int m_frameCount = 0;
    double m_fps = 0.0;
    QSize m_frameSize;
    mutable int m_lastDecodedIndex = -1;
};

#endif // VIDEOFRAMESOURCE_H
