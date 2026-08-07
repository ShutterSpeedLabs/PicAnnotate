#include "videoframesource.h"

#include <opencv2/imgproc.hpp>

bool VideoFrameSource::openVideo(const QString &path, QString *error)
{
    if (!m_capture.open(path.toStdString())) {
        if (error)
            *error = "Could not open video: " + path;
        return false;
    }

    m_frameCount = static_cast<int>(m_capture.get(cv::CAP_PROP_FRAME_COUNT));
    if (m_frameCount <= 0) {
        if (error)
            *error = "Video has no readable frames: " + path;
        m_capture.release();
        return false;
    }

    m_fps = m_capture.get(cv::CAP_PROP_FPS);

    // Every frame of a video shares one size, so read it once at open time and
    // never pay for a decode just to answer frameSize().
    const int width = static_cast<int>(m_capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(m_capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    if (width > 0 && height > 0)
        m_frameSize = QSize(width, height);

    m_lastDecodedIndex = -1;
    return true;
}

int VideoFrameSource::frameCount() const
{
    return m_frameCount;
}

QImage VideoFrameSource::frameAt(int index) const
{
    if (!m_capture.isOpened() || index < 0 || index >= m_frameCount)
        return QImage();

    if (index != m_lastDecodedIndex + 1)
        m_capture.set(cv::CAP_PROP_POS_FRAMES, index);

    cv::Mat frame;
    if (!m_capture.read(frame))
        return QImage();

    m_lastDecodedIndex = index;

    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

QString VideoFrameSource::frameLabel(int index) const
{
    if (index < 0 || index >= m_frameCount)
        return QString();
    return QString("frame_%1").arg(index, 6, 10, QChar('0'));
}

QSize VideoFrameSource::frameSize(int index) const
{
    if (index < 0 || index >= m_frameCount)
        return QSize();
    if (m_frameSize.isValid())
        return m_frameSize;
    return frameAt(index).size();
}

double VideoFrameSource::frameRate() const
{
    return m_fps;
}
