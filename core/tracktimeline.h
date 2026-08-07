#ifndef TRACKTIMELINE_H
#define TRACKTIMELINE_H

#include "annoshape.h"

#include <QList>
#include <QMap>
#include <QString>

#include <optional>

class QJsonArray;
class QJsonObject;

// One annotated frame of a track. Frames between keyframes are interpolated on
// read, so a long shot needs only the frames where the object actually changed
// direction — the same model Label Studio's video `sequence` and CVAT's tracks
// use.
struct TrackKeyframe
{
    AnnoShape shape;

    // The object is absent from this frame until the next keyframe. This is how
    // a gap is expressed inside a track (the object left and came back) without
    // splitting it into two tracks.
    bool outside = false;

    // Present but hidden. Exported as visibility information rather than as an
    // absence.
    bool occluded = false;

    QJsonObject toJson() const;
    static TrackKeyframe fromJson(const QJsonObject &obj);
};

// A single tracked object across time.
//
// Liveness: a track exists on [firstFrame(), lastFrame()] only. It deliberately
// does not persist past its last keyframe — leaving a stale box on every
// remaining frame of a long video silently poisons an export.
class AnnoTrack
{
public:
    AnnoTrack() = default;
    AnnoTrack(int id, int labelId, ShapeType type);

    int id() const { return m_id; }

    int labelId() const { return m_labelId; }
    void setLabelId(int labelId);

    ShapeType type() const { return m_type; }

    const QMap<int, TrackKeyframe> &keyframes() const { return m_keyframes; }
    QList<int> keyframeFrames() const;
    int keyframeCount() const { return m_keyframes.size(); }

    bool isEmpty() const { return m_keyframes.isEmpty(); }
    bool hasKeyframeAt(int frame) const { return m_keyframes.contains(frame); }
    int firstFrame() const;
    int lastFrame() const;
    bool spans(int frame) const;

    // Nearest keyframe at or before / strictly after `frame`, or -1.
    int keyframeAtOrBefore(int frame) const;
    int keyframeAfter(int frame) const;

    void setKeyframe(int frame, const AnnoShape &shape, bool outside = false, bool occluded = false);
    bool removeKeyframe(int frame);

    // Ends the track at `frame`: drops every keyframe from there on.
    void truncateFrom(int frame);

    bool setOutside(int frame, bool outside);
    bool setOccluded(int frame, bool occluded);

    // Resolved geometry for a frame, interpolating between keyframes. Returns
    // nothing when the track is not live there (before it starts, after it ends,
    // or inside an `outside` gap).
    std::optional<AnnoShape> shapeAt(int frame) const;

    QJsonObject toJson() const;
    static AnnoTrack fromJson(const QJsonObject &obj);

private:
    int m_id = -1;
    int m_labelId = -1;
    ShapeType m_type = ShapeType::Rect;
    QMap<int, TrackKeyframe> m_keyframes;
};

class TrackTimeline
{
public:
    const QMap<int, AnnoTrack> &tracks() const { return m_tracks; }
    bool isEmpty() const { return m_tracks.isEmpty(); }
    int trackCount() const { return m_tracks.size(); }

    int createTrack(int labelId, ShapeType type);
    void insertTrack(const AnnoTrack &track);
    bool removeTrack(int trackId);
    void clear();

    AnnoTrack *track(int trackId);
    const AnnoTrack *track(int trackId) const;

    QList<int> trackIds() const;

    // Track ids live on the same frame that carries them, so a shape's trackId
    // must be unique across the whole project.
    int nextTrackId() const;

    // Ids of tracks live on this frame, ascending — the order the resolver uses.
    QList<int> tracksOnFrame(int frame) const;

    // Every frame touched by any track, for exporters deciding what to write.
    QList<int> coveredFrames() const;

    QJsonArray toJson() const;
    static TrackTimeline fromJson(const QJsonArray &array);

private:
    QMap<int, AnnoTrack> m_tracks;
    int m_nextId = 0;
};

#endif // TRACKTIMELINE_H
