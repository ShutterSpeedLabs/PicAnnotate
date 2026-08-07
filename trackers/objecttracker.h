#ifndef OBJECTTRACKER_H
#define OBJECTTRACKER_H

#include <QRect>
#include <QString>

#include <opencv2/core.hpp>
#include <opencv2/video/tracking.hpp>

#include <optional>

struct TrackResult
{
    QRect box;
    float score = 0.0f;
    bool reacquired = false;
};

class ObjectTracker
{
public:
    bool ensureInitialized(const QString &backboneModelPath, const QString &neckheadModelPath, QString *error);

    // Returns false (with a reason) rather than letting OpenCV throw: an empty or
    // fully off-image box makes TrackerNano::init raise, which would otherwise
    // propagate out of a Qt slot and terminate.
    bool start(const cv::Mat &frame, const QRect &box, QString *error = nullptr);
    std::optional<TrackResult> update(const cv::Mat &frame);

private:
    std::optional<TrackResult> tryReacquire(const cv::Mat &frame) const;

    cv::Ptr<cv::TrackerNano> m_tracker;
    cv::Mat m_template;
    cv::Rect m_lastBox;

    static constexpr float kLowConfidenceThreshold = 0.3f;
    static constexpr double kReacquireMatchThreshold = 0.6;
    static constexpr int kSearchMargin = 60;
};

#endif // OBJECTTRACKER_H
