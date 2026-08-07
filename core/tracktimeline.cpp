#include "tracktimeline.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {

QPointF lerpPoint(const QPointF &from, const QPointF &to, double t)
{
    return QPointF(from.x() + (to.x() - from.x()) * t,
                   from.y() + (to.y() - from.y()) * t);
}

// Geometry between two keyframes. Point-bearing shapes only interpolate when the
// two keyframes agree on point count: with a different count there is no defined
// correspondence between the points, so holding the earlier shape is the only
// honest answer.
AnnoShape interpolateShape(const AnnoShape &from, const AnnoShape &to, double t)
{
    AnnoShape result = from;

    if (from.type() == ShapeType::Rect && to.type() == ShapeType::Rect) {
        const QRectF a = from.rect();
        const QRectF b = to.rect();
        result.setRect(QRectF(lerpPoint(a.topLeft(), b.topLeft(), t),
                              lerpPoint(a.bottomRight(), b.bottomRight(), t)));
        result.setRotation(from.rotation() + (to.rotation() - from.rotation()) * t);
        return result;
    }

    const QVector<QPointF> fromPoints = from.points();
    const QVector<QPointF> toPoints = to.points();
    if (fromPoints.size() != toPoints.size() || fromPoints.isEmpty())
        return result;

    QVector<QPointF> points;
    points.reserve(fromPoints.size());
    for (int i = 0; i < fromPoints.size(); ++i)
        points.append(lerpPoint(fromPoints.at(i), toPoints.at(i), t));

    // Visibility is categorical, so it steps from the earlier keyframe rather
    // than blending.
    const QVector<int> visibility = from.visibilityFlags();
    result.setPoints(points);
    result.setVisibilityFlags(visibility);
    return result;
}

} // namespace

QJsonObject TrackKeyframe::toJson() const
{
    QJsonObject obj;
    obj["shape"] = shape.toJson();
    if (outside)
        obj["outside"] = true;
    if (occluded)
        obj["occluded"] = true;
    return obj;
}

TrackKeyframe TrackKeyframe::fromJson(const QJsonObject &obj)
{
    TrackKeyframe keyframe;
    keyframe.shape = AnnoShape::fromJson(obj["shape"].toObject());
    keyframe.outside = obj["outside"].toBool(false);
    keyframe.occluded = obj["occluded"].toBool(false);
    return keyframe;
}

AnnoTrack::AnnoTrack(int id, int labelId, ShapeType type)
    : m_id(id)
    , m_labelId(labelId)
    , m_type(type)
{
}

void AnnoTrack::setLabelId(int labelId)
{
    m_labelId = labelId;
    // The label lives on the track, so every keyframe follows it.
    for (TrackKeyframe &keyframe : m_keyframes)
        keyframe.shape.setLabelId(labelId);
}

QList<int> AnnoTrack::keyframeFrames() const
{
    return m_keyframes.keys();
}

int AnnoTrack::firstFrame() const
{
    return m_keyframes.isEmpty() ? -1 : m_keyframes.firstKey();
}

int AnnoTrack::lastFrame() const
{
    return m_keyframes.isEmpty() ? -1 : m_keyframes.lastKey();
}

bool AnnoTrack::spans(int frame) const
{
    if (m_keyframes.isEmpty())
        return false;
    return frame >= firstFrame() && frame <= lastFrame();
}

int AnnoTrack::keyframeAtOrBefore(int frame) const
{
    auto it = m_keyframes.upperBound(frame);
    if (it == m_keyframes.constBegin())
        return -1;
    --it;
    return it.key();
}

int AnnoTrack::keyframeAfter(int frame) const
{
    const auto it = m_keyframes.upperBound(frame);
    return it == m_keyframes.constEnd() ? -1 : it.key();
}

void AnnoTrack::setKeyframe(int frame, const AnnoShape &shape, bool outside, bool occluded)
{
    if (frame < 0)
        return;

    TrackKeyframe keyframe;
    keyframe.shape = shape;
    keyframe.shape.setTrackId(m_id);
    keyframe.shape.setLabelId(m_labelId);
    keyframe.shape.setSource(ShapeSource::Manual);
    keyframe.outside = outside;
    keyframe.occluded = occluded;
    m_keyframes.insert(frame, keyframe);
}

bool AnnoTrack::removeKeyframe(int frame)
{
    return m_keyframes.remove(frame) > 0;
}

void AnnoTrack::truncateFrom(int frame)
{
    const QList<int> frames = m_keyframes.keys();
    for (int existing : frames) {
        if (existing >= frame)
            m_keyframes.remove(existing);
    }
}

bool AnnoTrack::setOutside(int frame, bool outside)
{
    const auto it = m_keyframes.find(frame);
    if (it == m_keyframes.end())
        return false;
    it->outside = outside;
    return true;
}

bool AnnoTrack::setOccluded(int frame, bool occluded)
{
    const auto it = m_keyframes.find(frame);
    if (it == m_keyframes.end())
        return false;
    it->occluded = occluded;
    return true;
}

std::optional<AnnoShape> AnnoTrack::shapeAt(int frame) const
{
    if (!spans(frame))
        return std::nullopt;

    const int previousFrame = keyframeAtOrBefore(frame);
    if (previousFrame < 0)
        return std::nullopt;

    const TrackKeyframe &previous = m_keyframes.value(previousFrame);

    // An `outside` keyframe marks the object gone from that frame onward, until
    // whatever the next keyframe says.
    if (previous.outside)
        return std::nullopt;

    if (previousFrame == frame) {
        AnnoShape shape = previous.shape;
        shape.setSource(ShapeSource::Manual);
        return shape;
    }

    const int nextFrame = keyframeAfter(frame);
    if (nextFrame < 0)
        return std::nullopt;

    const TrackKeyframe &next = m_keyframes.value(nextFrame);

    // An `outside` next keyframe still records where the object last was, so it
    // remains a valid endpoint to interpolate towards. Its flag governs presence
    // from that frame onward, not whether its geometry means anything.
    const double span = nextFrame - previousFrame;
    const double t = span > 0 ? (frame - previousFrame) / span : 0.0;
    AnnoShape shape = interpolateShape(previous.shape, next.shape, t);

    shape.setSource(ShapeSource::Interpolated);
    shape.setTrackId(m_id);
    shape.setLabelId(m_labelId);
    // Deterministic per-frame identity, so repeated reads of the same frame
    // produce the same shape id instead of a fresh UUID every time.
    shape.setId(QStringLiteral("track%1-frame%2").arg(m_id).arg(frame));
    return shape;
}

QJsonObject AnnoTrack::toJson() const
{
    QJsonArray keyframesArray;
    for (auto it = m_keyframes.constBegin(); it != m_keyframes.constEnd(); ++it) {
        QJsonObject entry = it.value().toJson();
        entry["frame"] = it.key();
        keyframesArray.append(entry);
    }

    QJsonObject obj;
    obj["id"] = m_id;
    obj["labelId"] = m_labelId;
    obj["type"] = shapeTypeToString(m_type);
    obj["keyframes"] = keyframesArray;
    return obj;
}

AnnoTrack AnnoTrack::fromJson(const QJsonObject &obj)
{
    AnnoTrack track;
    track.m_id = obj["id"].toInt(-1);
    track.m_labelId = obj["labelId"].toInt(-1);
    track.m_type = shapeTypeFromString(obj["type"].toString());

    const QJsonArray keyframesArray = obj["keyframes"].toArray();
    for (const QJsonValue &value : keyframesArray) {
        const QJsonObject entry = value.toObject();
        const int frame = entry["frame"].toInt(-1);
        if (frame < 0)
            continue;
        track.m_keyframes.insert(frame, TrackKeyframe::fromJson(entry));
    }
    return track;
}

int TrackTimeline::createTrack(int labelId, ShapeType type)
{
    const int id = nextTrackId();
    m_tracks.insert(id, AnnoTrack(id, labelId, type));
    m_nextId = id + 1;
    return id;
}

void TrackTimeline::insertTrack(const AnnoTrack &track)
{
    if (track.id() < 0)
        return;
    m_tracks.insert(track.id(), track);
    m_nextId = qMax(m_nextId, track.id() + 1);
}

bool TrackTimeline::removeTrack(int trackId)
{
    return m_tracks.remove(trackId) > 0;
}

void TrackTimeline::clear()
{
    m_tracks.clear();
    m_nextId = 0;
}

AnnoTrack *TrackTimeline::track(int trackId)
{
    const auto it = m_tracks.find(trackId);
    return it == m_tracks.end() ? nullptr : &it.value();
}

const AnnoTrack *TrackTimeline::track(int trackId) const
{
    const auto it = m_tracks.constFind(trackId);
    return it == m_tracks.constEnd() ? nullptr : &it.value();
}

QList<int> TrackTimeline::trackIds() const
{
    return m_tracks.keys();
}

int TrackTimeline::nextTrackId() const
{
    int candidate = m_nextId;
    if (!m_tracks.isEmpty())
        candidate = qMax(candidate, m_tracks.lastKey() + 1);
    return candidate;
}

QList<int> TrackTimeline::tracksOnFrame(int frame) const
{
    QList<int> ids;
    for (auto it = m_tracks.constBegin(); it != m_tracks.constEnd(); ++it) {
        if (it.value().shapeAt(frame).has_value())
            ids.append(it.key());
    }
    return ids;
}

QList<int> TrackTimeline::coveredFrames() const
{
    QSet<int> frames;
    for (auto it = m_tracks.constBegin(); it != m_tracks.constEnd(); ++it) {
        const AnnoTrack &track = it.value();
        if (track.isEmpty())
            continue;
        for (int frame = track.firstFrame(); frame <= track.lastFrame(); ++frame) {
            if (track.shapeAt(frame).has_value())
                frames.insert(frame);
        }
    }

    QList<int> sorted = frames.values();
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

QJsonArray TrackTimeline::toJson() const
{
    QJsonArray array;
    for (auto it = m_tracks.constBegin(); it != m_tracks.constEnd(); ++it)
        array.append(it.value().toJson());
    return array;
}

TrackTimeline TrackTimeline::fromJson(const QJsonArray &array)
{
    TrackTimeline timeline;
    for (const QJsonValue &value : array)
        timeline.insertTrack(AnnoTrack::fromJson(value.toObject()));
    return timeline;
}
