#include "project.h"
#include "annoshape.h"
#include "imagefoldersource.h"
#include "videoframesource.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>

namespace {
constexpr int kProjectFormatVersion = 3;
}

bool Project::openFolder(const QString &path, QString *error)
{
    auto source = std::make_unique<ImageFolderSource>();
    if (!source->openFolder(path, error))
        return false;

    m_source = std::move(source);
    m_sourceKind = SourceKind::Folder;
    m_sourcePath = path;
    m_currentIndex = 0;
    resetAnnotations();
    return true;
}

bool Project::openImage(const QString &filePath, QString *error)
{
    auto source = std::make_unique<ImageFolderSource>();
    if (!source->openSingleImage(filePath, error))
        return false;

    m_source = std::move(source);
    m_sourceKind = SourceKind::Image;
    m_sourcePath = filePath;
    m_currentIndex = 0;
    resetAnnotations();
    return true;
}

bool Project::openImageList(const QString &folderPath, const QStringList &fileNames, QString *error)
{
    auto source = std::make_unique<ImageFolderSource>();
    if (!source->openFileList(folderPath, fileNames, error))
        return false;

    m_source = std::move(source);
    m_sourceKind = SourceKind::Folder;
    m_sourcePath = folderPath;
    m_currentIndex = 0;
    resetAnnotations();
    return true;
}

bool Project::openVideo(const QString &path, QString *error)
{
    auto source = std::make_unique<VideoFrameSource>();
    if (!source->openVideo(path, error))
        return false;

    m_source = std::move(source);
    m_sourceKind = SourceKind::Video;
    m_sourcePath = path;
    m_currentIndex = 0;
    resetAnnotations();
    return true;
}

bool Project::openDataset(const QString &videoPath, const QString &annotationPath, QString *error)
{
    QFile file(annotationPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = "Could not read annotation file: " + annotationPath;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        if (error)
            *error = "Invalid annotation file: " + parseError.errorString();
        return false;
    }

    if (!openVideo(videoPath, error))
        return false;

    applyAnnotationsFromJson(doc.object());
    return true;
}

void Project::resetAnnotations()
{
    m_annotations.clear();
    m_tracks.clear();
    m_frameSizeCache.clear();
}

QVector<AnnoShape> Project::resolvedShapes(int index) const
{
    QVector<AnnoShape> shapes = annotationsAt(index).shapes();

    for (int trackId : m_tracks.trackIds()) {
        const AnnoTrack *track = m_tracks.track(trackId);
        if (!track)
            continue;
        if (const std::optional<AnnoShape> shape = track->shapeAt(index))
            shapes.append(*shape);
    }
    return shapes;
}

QVector<ShapeTarget> Project::shapeTargets(int index) const
{
    QVector<ShapeTarget> targets;

    const int frameShapeCount = annotationsAt(index).shapes().size();
    targets.reserve(frameShapeCount + m_tracks.trackCount());

    for (int i = 0; i < frameShapeCount; ++i) {
        ShapeTarget target;
        target.kind = ShapeTarget::Kind::Frame;
        target.frameShapeIndex = i;
        targets.append(target);
    }

    // Must walk tracks in the same order as resolvedShapes() so indices line up.
    for (int trackId : m_tracks.trackIds()) {
        const AnnoTrack *track = m_tracks.track(trackId);
        if (!track || !track->shapeAt(index).has_value())
            continue;

        ShapeTarget target;
        target.kind = ShapeTarget::Kind::Track;
        target.trackId = trackId;
        targets.append(target);
    }
    return targets;
}

ShapeTarget Project::shapeTargetAt(int index, int shapeIndex) const
{
    const QVector<ShapeTarget> targets = shapeTargets(index);
    if (shapeIndex < 0 || shapeIndex >= targets.size())
        return ShapeTarget();
    return targets.at(shapeIndex);
}

bool Project::hasSource() const
{
    return m_source != nullptr;
}

int Project::frameCount() const
{
    return m_source ? m_source->frameCount() : 0;
}

double Project::frameRate() const
{
    return m_source ? m_source->frameRate() : 0.0;
}

int Project::currentIndex() const
{
    return m_currentIndex;
}

QImage Project::currentFrame() const
{
    return frameAt(m_currentIndex);
}

QImage Project::frameAt(int index) const
{
    if (!m_source || index < 0 || index >= m_source->frameCount())
        return QImage();
    return m_source->frameAt(index);
}

QString Project::currentLabel() const
{
    return frameLabel(m_currentIndex);
}

QString Project::frameLabel(int index) const
{
    if (!m_source || index < 0 || index >= m_source->frameCount())
        return QString();
    return m_source->frameLabel(index);
}

QString Project::framePath(int index) const
{
    if (!m_source || index < 0 || index >= m_source->frameCount())
        return QString();
    return m_source->framePath(index);
}

QStringList Project::fileNames() const
{
    QStringList names;
    if (!m_source)
        return names;

    const int count = m_source->frameCount();
    names.reserve(count);
    for (int i = 0; i < count; ++i)
        names << m_source->frameLabel(i);
    return names;
}

QSize Project::frameSize(int index) const
{
    // A size stored with the annotations wins: it is what the shapes were
    // actually drawn against, even if the source file has since been replaced.
    const auto stored = m_annotations.constFind(index);
    if (stored != m_annotations.constEnd() && stored.value().hasImageSize())
        return stored.value().imageSize();

    const auto cached = m_frameSizeCache.constFind(index);
    if (cached != m_frameSizeCache.constEnd())
        return cached.value();

    if (!m_source || index < 0 || index >= m_source->frameCount())
        return QSize();

    const QSize size = m_source->frameSize(index);
    if (size.isValid() && !size.isEmpty())
        m_frameSizeCache.insert(index, size);
    return size;
}

void Project::stampImageSize(int index)
{
    FrameAnnotations &frame = m_annotations[index];
    if (frame.hasImageSize())
        return;

    const QSize size = frameSize(index);
    if (size.isValid() && !size.isEmpty())
        frame.setImageSize(size);
}

bool Project::next()
{
    if (!m_source || m_currentIndex + 1 >= m_source->frameCount())
        return false;
    ++m_currentIndex;
    return true;
}

bool Project::previous()
{
    if (!m_source || m_currentIndex <= 0)
        return false;
    --m_currentIndex;
    return true;
}

bool Project::goToFrame(int index)
{
    if (!m_source || index < 0 || index >= m_source->frameCount())
        return false;
    m_currentIndex = index;
    return true;
}

FrameAnnotations &Project::currentAnnotations()
{
    return annotationsAt(m_currentIndex);
}

const FrameAnnotations &Project::currentAnnotations() const
{
    return annotationsAt(m_currentIndex);
}

FrameAnnotations &Project::annotationsAt(int index)
{
    // Handing out a mutable frame is the moment a shape is about to be added,
    // so record the frame size now while the source is still open.
    stampImageSize(index);
    return m_annotations[index];
}

const FrameAnnotations &Project::annotationsAt(int index) const
{
    static const FrameAnnotations empty;
    const auto it = m_annotations.constFind(index);
    return it == m_annotations.constEnd() ? empty : it.value();
}

void Project::setAnnotations(int index, const FrameAnnotations &annotations)
{
    if (annotations.shapes().isEmpty() && !annotations.hasImageSize()) {
        m_annotations.remove(index);
        return;
    }

    m_annotations[index] = annotations;
    stampImageSize(index);
}

QList<int> Project::annotatedFrameIndices() const
{
    QSet<int> indices;
    for (auto it = m_annotations.constBegin(); it != m_annotations.constEnd(); ++it) {
        if (!it.value().shapes().isEmpty())
            indices.insert(it.key());
    }
    for (int frame : m_tracks.coveredFrames())
        indices.insert(frame);

    QList<int> sorted = indices.values();
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

int Project::totalShapeCount() const
{
    int count = 0;
    for (auto it = m_annotations.constBegin(); it != m_annotations.constEnd(); ++it)
        count += it.value().shapes().size();

    // Counts what a frame would actually show, interpolated frames included.
    for (int frame : m_tracks.coveredFrames())
        count += m_tracks.tracksOnFrame(frame).size();
    return count;
}

bool Project::hasAnnotations(int index) const
{
    const auto it = m_annotations.constFind(index);
    if (it != m_annotations.constEnd() && !it.value().shapes().isEmpty())
        return true;
    return !m_tracks.tracksOnFrame(index).isEmpty();
}

void Project::clearAnnotations(int index)
{
    m_annotations.remove(index);
}

void Project::clearAllAnnotations()
{
    m_annotations.clear();
    m_tracks.clear();
}

int Project::nextTrackId() const
{
    int maxId = m_tracks.nextTrackId() - 1;
    for (auto it = m_annotations.constBegin(); it != m_annotations.constEnd(); ++it) {
        for (const AnnoShape &shape : it.value().shapes()) {
            if (shape.trackId() > maxId)
                maxId = shape.trackId();
        }
    }
    return maxId + 1;
}

void Project::migrateFrameShapesToTracks()
{
    // Collect the tracked shapes frame by frame, then rebuild them as tracks.
    QMap<int, QMap<int, AnnoShape>> byTrack;   // trackId -> frame -> shape

    for (auto it = m_annotations.begin(); it != m_annotations.end(); ++it) {
        const QVector<AnnoShape> shapes = it.value().shapes();

        FrameAnnotations rebuilt;
        rebuilt.setImageSize(it.value().imageSize());
        rebuilt.setStatus(it.value().status());

        for (const AnnoShape &shape : shapes) {
            if (shape.trackId() < 0) {
                rebuilt.addShape(shape);
                continue;
            }
            byTrack[shape.trackId()].insert(it.key(), shape);
        }
        it.value() = rebuilt;
    }

    for (auto trackIt = byTrack.constBegin(); trackIt != byTrack.constEnd(); ++trackIt) {
        const QMap<int, AnnoShape> &frames = trackIt.value();
        if (frames.isEmpty())
            continue;

        const AnnoShape &first = frames.first();
        AnnoTrack track(trackIt.key(), first.labelId(), first.type());
        for (auto frameIt = frames.constBegin(); frameIt != frames.constEnd(); ++frameIt)
            track.setKeyframe(frameIt.key(), frameIt.value());

        m_tracks.insertTrack(track);
    }

    // Drop frames left holding nothing but a recorded image size.
    const QList<int> frames = m_annotations.keys();
    for (int frame : frames) {
        if (m_annotations.value(frame).shapes().isEmpty())
            m_annotations.remove(frame);
    }
}

QString Project::sourceDirectory() const
{
    if (m_sourcePath.isEmpty())
        return QString();
    if (m_sourceKind == SourceKind::Folder)
        return m_sourcePath;
    return QFileInfo(m_sourcePath).absolutePath();
}

bool Project::save(const QString &filePath, QString *error) const
{
    QJsonObject root;
    root["version"] = kProjectFormatVersion;

    QString sourceTypeName = "image";
    if (m_sourceKind == SourceKind::Folder)
        sourceTypeName = "folder";
    else if (m_sourceKind == SourceKind::Video)
        sourceTypeName = "video";

    QJsonObject source;
    source["type"] = sourceTypeName;
    source["path"] = m_sourcePath;
    if (m_sourceKind == SourceKind::Video && m_source) {
        const QSize size = m_source->frameSize(0);
        if (size.isValid() && !size.isEmpty()) {
            source["frameWidth"] = size.width();
            source["frameHeight"] = size.height();
        }
        source["frameRate"] = m_source->frameRate();
        source["frameCount"] = m_source->frameCount();
    }
    root["source"] = source;

    root["labels"] = m_labelSchema.toJsonObject();
    root["tracks"] = m_tracks.toJson();

    QJsonObject frames;
    for (auto it = m_annotations.constBegin(); it != m_annotations.constEnd(); ++it) {
        // Merely visiting a frame records its size, which is not worth saving on
        // its own — the source can always be asked again.
        if (it.value().shapes().isEmpty())
            continue;
        frames[QString::number(it.key())] = it.value().toJson();
    }
    root["frames"] = frames;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = "Could not write file: " + filePath;
        return false;
    }

    file.write(QJsonDocument(root).toJson());
    return true;
}

bool Project::load(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = "Could not read file: " + filePath;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        if (error)
            *error = "Invalid project file: " + parseError.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    if (root["version"].toInt(1) > kProjectFormatVersion) {
        if (error) {
            *error = QString("Project was written by a newer version (format %1, this build reads %2).")
                         .arg(root["version"].toInt())
                         .arg(kProjectFormatVersion);
        }
        return false;
    }

    const QJsonObject source = root["source"].toObject();
    const QString sourceType = source["type"].toString();
    const QString sourcePathValue = source["path"].toString();

    bool opened = false;
    if (sourceType == "folder") {
        opened = openFolder(sourcePathValue, error);
    } else if (sourceType == "image") {
        opened = openImage(sourcePathValue, error);
    } else if (sourceType == "video") {
        opened = openVideo(sourcePathValue, error);
    } else if (error) {
        *error = "Unknown source type in project file";
    }

    if (!opened)
        return false;

    applyAnnotationsFromJson(root);
    return true;
}

void Project::applyAnnotationsFromJson(const QJsonObject &root)
{
    // Format 1 wrote the schema as a bare array of classes; format 2 wraps it in
    // an object so keypoint templates travel with it.
    const QJsonValue labelsValue = root["labels"];
    m_labelSchema = labelsValue.isArray() ? LabelSchema::fromJson(labelsValue.toArray())
                                          : LabelSchema::fromJsonObject(labelsValue.toObject());

    resetAnnotations();
    const QJsonObject frames = root["frames"].toObject();
    for (auto it = frames.constBegin(); it != frames.constEnd(); ++it) {
        bool ok = false;
        const int frameIndex = it.key().toInt(&ok);
        if (ok)
            m_annotations[frameIndex] = FrameAnnotations::fromJson(it.value().toObject());
    }

    const int version = root["version"].toInt(1);
    if (root.contains("tracks")) {
        m_tracks = TrackTimeline::fromJson(root["tracks"].toArray());
    } else if (version < 3) {
        migrateFrameShapesToTracks();
    }
}
