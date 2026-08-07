#include "objecttracker.h"

#include <opencv2/imgproc.hpp>

bool ObjectTracker::ensureInitialized(const QString &backboneModelPath, const QString &neckheadModelPath, QString *error)
{
    if (m_tracker)
        return true;

    cv::TrackerNano::Params params;
    params.backbone = backboneModelPath.toStdString();
    params.neckhead = neckheadModelPath.toStdString();

    try {
        m_tracker = cv::TrackerNano::create(params);
    } catch (const cv::Exception &e) {
        if (error)
            *error = QString("Failed to load NanoTrack models: %1").arg(e.what());
        return false;
    }

    return true;
}

bool ObjectTracker::start(const cv::Mat &frame, const QRect &box, QString *error)
{
    if (!m_tracker) {
        if (error)
            *error = QStringLiteral("Tracker models have not been loaded.");
        return false;
    }
    if (frame.empty()) {
        if (error)
            *error = QStringLiteral("The current frame could not be decoded.");
        return false;
    }

    m_lastBox = cv::Rect(box.x(), box.y(), box.width(), box.height());
    m_lastBox &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (m_lastBox.width <= 0 || m_lastBox.height <= 0) {
        if (error)
            *error = QStringLiteral("The selected box does not overlap the image.");
        return false;
    }

    m_template = frame(m_lastBox).clone();

    try {
        m_tracker->init(frame, m_lastBox);
    } catch (const cv::Exception &e) {
        if (error)
            *error = QString("Could not start the tracker: %1").arg(e.what());
        return false;
    }
    return true;
}

std::optional<TrackResult> ObjectTracker::update(const cv::Mat &frame)
{
    if (!m_tracker || frame.empty())
        return std::nullopt;

    cv::Rect box;
    bool tracked = false;
    float score = 0.0f;
    try {
        tracked = m_tracker->update(frame, box);
        score = m_tracker->getTrackingScore();
    } catch (const cv::Exception &) {
        // Treated as a lost track; the caller stops and reports it.
        return std::nullopt;
    }

    if (tracked && score >= kLowConfidenceThreshold) {
        m_lastBox = box;
        const cv::Rect clamped = box & cv::Rect(0, 0, frame.cols, frame.rows);
        if (clamped.width > 0 && clamped.height > 0)
            m_template = frame(clamped).clone();
        return TrackResult{QRect(box.x, box.y, box.width, box.height), score, false};
    }

    return tryReacquire(frame);
}

std::optional<TrackResult> ObjectTracker::tryReacquire(const cv::Mat &frame) const
{
    if (m_template.empty())
        return std::nullopt;

    cv::Rect searchRegion(
        m_lastBox.x - kSearchMargin,
        m_lastBox.y - kSearchMargin,
        m_lastBox.width + 2 * kSearchMargin,
        m_lastBox.height + 2 * kSearchMargin);
    searchRegion &= cv::Rect(0, 0, frame.cols, frame.rows);

    if (searchRegion.width < m_template.cols || searchRegion.height < m_template.rows)
        return std::nullopt;

    cv::Mat result;
    cv::matchTemplate(frame(searchRegion), m_template, result, cv::TM_CCOEFF_NORMED);

    double maxVal = 0.0;
    cv::Point maxLoc;
    cv::minMaxLoc(result, nullptr, &maxVal, nullptr, &maxLoc);

    if (maxVal < kReacquireMatchThreshold)
        return std::nullopt;

    const cv::Rect matchedBox(searchRegion.x + maxLoc.x, searchRegion.y + maxLoc.y, m_template.cols, m_template.rows);
    return TrackResult{QRect(matchedBox.x, matchedBox.y, matchedBox.width, matchedBox.height),
                        static_cast<float>(maxVal), true};
}
