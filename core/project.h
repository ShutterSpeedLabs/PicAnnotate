#ifndef PROJECT_H
#define PROJECT_H

#include "framesource.h"
#include "frameannotations.h"
#include "labelschema.h"
#include "tracktimeline.h"

#include <QHash>
#include <QImage>
#include <QMap>
#include <QSize>
#include <QStringList>
#include <memory>

class QJsonObject;

// What a resolved shape index on a frame actually points at. Shapes shown on a
// frame come from two places — shapes annotated on that frame alone, and tracks
// resolved at that frame — and an edit has to be written back to the right one.
struct ShapeTarget
{
    enum class Kind { None, Frame, Track };

    Kind kind = Kind::None;
    int frameShapeIndex = -1;   // index into FrameAnnotations, when kind == Frame
    int trackId = -1;           // when kind == Track

    bool isFrameShape() const { return kind == Kind::Frame; }
    bool isTrackShape() const { return kind == Kind::Track; }
    bool isValid() const { return kind != Kind::None; }
};

class Project
{
public:
    enum class SourceKind { None, Folder, Image, Video };

    bool openFolder(const QString &path, QString *error);
    bool openImage(const QString &filePath, QString *error);
    bool openVideo(const QString &path, QString *error);

    // Opens an explicit, ordered list of image files under `folderPath`. Dataset
    // imports use this so frame order follows the dataset's own order.
    bool openImageList(const QString &folderPath, const QStringList &fileNames, QString *error);

    // Opens a video together with a separately-selected annotation JSON file,
    // so a dataset can be replayed without relying on the source path that
    // may be embedded (and possibly stale) inside the JSON itself.
    bool openDataset(const QString &videoPath, const QString &annotationPath, QString *error);

    bool hasSource() const;
    SourceKind sourceKind() const { return m_sourceKind; }
    bool isVideoSource() const { return m_sourceKind == SourceKind::Video; }
    int frameCount() const;
    int currentIndex() const;
    double frameRate() const;

    QImage currentFrame() const;
    QImage frameAt(int index) const;
    QString currentLabel() const;
    QString frameLabel(int index) const;
    QString framePath(int index) const;
    QStringList fileNames() const;

    // Pixel size of a frame. Prefers the size stored with that frame's
    // annotations and otherwise asks the source, caching the answer.
    QSize frameSize(int index) const;

    bool next();
    bool previous();
    bool goToFrame(int index);

    LabelSchema &labelSchema() { return m_labelSchema; }
    const LabelSchema &labelSchema() const { return m_labelSchema; }
    void setLabelSchema(const LabelSchema &schema) { m_labelSchema = schema; }

    FrameAnnotations &currentAnnotations();
    const FrameAnnotations &currentAnnotations() const;

    // Access by explicit frame index, needed by undo commands (which may target
    // a frame the user has since navigated away from) and by importers.
    FrameAnnotations &annotationsAt(int index);
    const FrameAnnotations &annotationsAt(int index) const;
    void setAnnotations(int index, const FrameAnnotations &annotations);

    // ---- resolved view (frame shapes + tracks) ------------------------------
    //
    // Everything the UI and the exporters should read. Frame shapes come first,
    // then tracks in ascending id, so an index is stable for a given frame.
    QVector<AnnoShape> resolvedShapes(int index) const;
    QVector<ShapeTarget> shapeTargets(int index) const;
    ShapeTarget shapeTargetAt(int index, int shapeIndex) const;

    TrackTimeline &tracks() { return m_tracks; }
    const TrackTimeline &tracks() const { return m_tracks; }

    QList<int> annotatedFrameIndices() const;
    int totalShapeCount() const;

    bool hasAnnotations(int index) const;
    void clearAnnotations(int index);
    void clearAllAnnotations();

    int nextTrackId() const;

    QString sourcePath() const { return m_sourcePath; }
    QString sourceDirectory() const;

    bool save(const QString &filePath, QString *error) const;
    bool load(const QString &filePath, QString *error);

private:
    void resetAnnotations();
    void stampImageSize(int index);
    void applyAnnotationsFromJson(const QJsonObject &root);

    // Pre-v3 projects stored a tracked object as a separate shape on every frame.
    // Folds those into real tracks, one keyframe per frame, which is lossless.
    void migrateFrameShapesToTracks();

    std::unique_ptr<FrameSource> m_source;
    SourceKind m_sourceKind = SourceKind::None;
    QString m_sourcePath;
    int m_currentIndex = -1;

    LabelSchema m_labelSchema;
    QMap<int, FrameAnnotations> m_annotations;
    TrackTimeline m_tracks;
    mutable QHash<int, QSize> m_frameSizeCache;
};

#endif // PROJECT_H
