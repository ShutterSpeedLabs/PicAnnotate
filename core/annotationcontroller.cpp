#include "annotationcontroller.h"
#include "annotationcommands.h"
#include "project.h"

#include <QAction>
#include <QUndoStack>

namespace {
// Bounds how much history is retained. A long tracking run pushes one command
// per frame, so an unlimited stack grows without end over a session. Must be set
// while the stack is empty.
constexpr int kUndoLimit = 500;
} // namespace

AnnotationController::AnnotationController(Project *project, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_undoStack(new QUndoStack(this))
{
    m_undoStack->setUndoLimit(kUndoLimit);
}

void AnnotationController::beginCompound(const QString &text)
{
    if (m_compoundDepth++ == 0)
        m_undoStack->beginMacro(text);
}

void AnnotationController::endCompound()
{
    if (m_compoundDepth == 0)
        return;
    if (--m_compoundDepth == 0)
        m_undoStack->endMacro();
}

QAction *AnnotationController::createUndoAction(QObject *parent) const
{
    QAction *action = m_undoStack->createUndoAction(parent, QStringLiteral("Undo"));
    action->setShortcut(QKeySequence::Undo);
    return action;
}

QAction *AnnotationController::createRedoAction(QObject *parent) const
{
    QAction *action = m_undoStack->createRedoAction(parent, QStringLiteral("Redo"));
    action->setShortcut(QKeySequence::Redo);
    return action;
}

void AnnotationController::addShape(int frameIndex, const AnnoShape &shape)
{
    if (frameIndex < 0)
        return;
    m_undoStack->push(new AddShapeCommand(this, m_project, frameIndex, shape));
}

void AnnotationController::setFrameAnnotations(int frameIndex, const FrameAnnotations &annotations,
                                               const QString &commandText)
{
    if (frameIndex < 0)
        return;
    m_undoStack->push(new SetFrameAnnotationsCommand(this, m_project, frameIndex,
                                                     m_project->annotationsAt(frameIndex),
                                                     annotations, commandText));
}

void AnnotationController::clearFrame(int frameIndex)
{
    if (frameIndex < 0)
        return;

    const bool hasFrameShapes = !m_project->annotationsAt(frameIndex).shapes().isEmpty();
    const QList<int> tracksHere = m_project->tracks().tracksOnFrame(frameIndex);
    if (!hasFrameShapes && tracksHere.isEmpty())
        return;

    m_undoStack->beginMacro(QStringLiteral("Clear frame annotations"));

    if (hasFrameShapes) {
        FrameAnnotations cleared;
        cleared.setImageSize(m_project->annotationsAt(frameIndex).imageSize());
        setFrameAnnotations(frameIndex, cleared, QStringLiteral("Clear frame shapes"));
    }

    // Clearing a frame must not silently delete whole tracks: mark them absent
    // from this frame onwards instead, which is what an annotator means.
    for (int trackId : tracksHere)
        setTrackOutside(trackId, frameIndex, true);

    m_undoStack->endMacro();
}

void AnnotationController::clearAll()
{
    const QList<int> indices = m_project->annotatedFrameIndices();
    const QList<int> trackIds = m_project->tracks().trackIds();
    if (indices.isEmpty() && trackIds.isEmpty())
        return;

    m_undoStack->beginMacro(QStringLiteral("Clear all annotations"));
    for (int index : indices) {
        if (m_project->annotationsAt(index).shapes().isEmpty())
            continue;
        FrameAnnotations cleared;
        cleared.setImageSize(m_project->annotationsAt(index).imageSize());
        setFrameAnnotations(index, cleared, QStringLiteral("Clear frame shapes"));
    }
    for (int trackId : trackIds)
        deleteTrack(trackId);
    m_undoStack->endMacro();
}

void AnnotationController::editShape(int frameIndex, int shapeIndex, const AnnoShape &shape,
                                     const QString &commandText)
{
    const ShapeTarget target = m_project->shapeTargetAt(frameIndex, shapeIndex);
    if (!target.isValid())
        return;

    if (target.isTrackShape()) {
        // Editing anywhere on a track pins the result as a keyframe there.
        setTrackKeyframe(target.trackId, frameIndex, shape,
                         commandText.isEmpty() ? QStringLiteral("Set keyframe") : commandText);
        return;
    }

    const AnnoShape *existing =
        m_project->annotationsAt(frameIndex).shapeAt(target.frameShapeIndex);
    if (!existing)
        return;

    const QString text = commandText.isEmpty() ? QStringLiteral("Edit shape") : commandText;
    m_undoStack->push(new ReplaceShapeCommand(this, m_project, frameIndex, target.frameShapeIndex,
                                              *existing, shape, text));
}

void AnnotationController::deleteShape(int frameIndex, int shapeIndex)
{
    const ShapeTarget target = m_project->shapeTargetAt(frameIndex, shapeIndex);
    if (!target.isValid())
        return;

    if (target.isTrackShape()) {
        const AnnoTrack *track = m_project->tracks().track(target.trackId);
        if (!track)
            return;

        // On a keyframe, delete just that keyframe; on an interpolated frame
        // there is nothing to delete, so end the track here instead.
        if (track->hasKeyframeAt(frameIndex) && track->keyframeCount() > 1)
            removeTrackKeyframe(target.trackId, frameIndex);
        else
            endTrackAt(target.trackId, frameIndex);
        return;
    }

    auto *command = new RemoveShapeCommand(this, m_project, frameIndex, target.frameShapeIndex);
    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undoStack->push(command);
}

void AnnotationController::pushTrackState(int trackId, const AnnoTrack &track,
                                          const QString &commandText)
{
    m_undoStack->push(new SetTrackCommand(this, m_project, trackId, track, commandText));
}

int AnnotationController::startTrack(int frameIndex, const AnnoShape &shape, ShapeType type)
{
    if (frameIndex < 0)
        return -1;

    // Project::nextTrackId() spans both the timeline and any track ids carried by
    // plain frame shapes — an imported COCO dataset puts track_id on frame shapes,
    // and allocating from the timeline alone would hand out an id already in use.
    const int trackId = m_project->nextTrackId();
    AnnoTrack track(trackId, shape.labelId(), type);
    track.setKeyframe(frameIndex, shape);
    pushTrackState(trackId, track, QStringLiteral("Start track %1").arg(trackId));
    return trackId;
}

void AnnotationController::setTrackKeyframe(int trackId, int frameIndex, const AnnoShape &shape,
                                            const QString &commandText)
{
    // Deliberately the granular command rather than a whole-track snapshot: this
    // is the one track edit that runs in a loop.
    auto *command = new SetTrackKeyframeCommand(
        this, m_project, trackId, frameIndex, shape,
        commandText.isEmpty() ? QStringLiteral("Set keyframe") : commandText);

    if (!command->isValid()) {
        delete command;
        return;
    }
    m_undoStack->push(command);
}

void AnnotationController::removeTrackKeyframe(int trackId, int frameIndex)
{
    const AnnoTrack *existing = m_project->tracks().track(trackId);
    if (!existing || !existing->hasKeyframeAt(frameIndex))
        return;

    AnnoTrack updated = *existing;
    updated.removeKeyframe(frameIndex);
    pushTrackState(trackId, updated, QStringLiteral("Delete keyframe"));
}

void AnnotationController::endTrackAt(int trackId, int frameIndex)
{
    const AnnoTrack *existing = m_project->tracks().track(trackId);
    if (!existing)
        return;

    AnnoTrack updated = *existing;
    updated.truncateFrom(frameIndex);
    pushTrackState(trackId, updated, QStringLiteral("End track at frame %1").arg(frameIndex));
}

void AnnotationController::setTrackOutside(int trackId, int frameIndex, bool outside)
{
    const AnnoTrack *existing = m_project->tracks().track(trackId);
    if (!existing)
        return;

    AnnoTrack updated = *existing;
    if (!updated.hasKeyframeAt(frameIndex)) {
        // Needs a keyframe to carry the flag; seed it from what is shown there.
        const std::optional<AnnoShape> resolved = existing->shapeAt(frameIndex);
        if (!resolved)
            return;
        updated.setKeyframe(frameIndex, *resolved);
    }
    updated.setOutside(frameIndex, outside);

    pushTrackState(trackId, updated,
                   outside ? QStringLiteral("Mark track absent") : QStringLiteral("Mark track present"));
}

void AnnotationController::setTrackOccluded(int trackId, int frameIndex, bool occluded)
{
    const AnnoTrack *existing = m_project->tracks().track(trackId);
    if (!existing)
        return;

    AnnoTrack updated = *existing;
    if (!updated.hasKeyframeAt(frameIndex)) {
        const std::optional<AnnoShape> resolved = existing->shapeAt(frameIndex);
        if (!resolved)
            return;
        updated.setKeyframe(frameIndex, *resolved);
    }
    updated.setOccluded(frameIndex, occluded);

    pushTrackState(trackId, updated, QStringLiteral("Toggle occluded"));
}

void AnnotationController::setTrackLabel(int trackId, int labelId)
{
    const AnnoTrack *existing = m_project->tracks().track(trackId);
    if (!existing || existing->labelId() == labelId)
        return;

    AnnoTrack updated = *existing;
    updated.setLabelId(labelId);
    pushTrackState(trackId, updated, QStringLiteral("Change track label"));
}

void AnnotationController::deleteTrack(int trackId)
{
    if (!m_project->tracks().track(trackId))
        return;

    // An empty track is removed outright by SetTrackCommand.
    pushTrackState(trackId, AnnoTrack(), QStringLiteral("Delete track %1").arg(trackId));
}

int AnnotationController::promoteToTrack(int frameIndex, int shapeIndex)
{
    const ShapeTarget target = m_project->shapeTargetAt(frameIndex, shapeIndex);
    if (target.isTrackShape())
        return target.trackId;
    if (!target.isFrameShape())
        return -1;

    const AnnoShape *existing =
        m_project->annotationsAt(frameIndex).shapeAt(target.frameShapeIndex);
    if (!existing)
        return -1;

    const AnnoShape shape = *existing;
    // Spans the timeline and frame-shape track ids; see startTrack().
    const int trackId = m_project->nextTrackId();

    // One undo step: the frame shape leaves and the track appears together.
    m_undoStack->beginMacro(QStringLiteral("Convert shape to track"));

    auto *removal = new RemoveShapeCommand(this, m_project, frameIndex, target.frameShapeIndex);
    if (removal->isValid())
        m_undoStack->push(removal);
    else
        delete removal;

    AnnoTrack track(trackId, shape.labelId(), shape.type());
    track.setKeyframe(frameIndex, shape);
    pushTrackState(trackId, track, QStringLiteral("Start track %1").arg(trackId));

    m_undoStack->endMacro();
    return trackId;
}

AnnotationController::LabelUsage AnnotationController::labelUsage(int labelId) const
{
    LabelUsage usage;
    if (labelId < 0)
        return usage;

    const int frameCount = m_project->frameCount();
    for (int frame = 0; frame < frameCount; ++frame) {
        int onThisFrame = 0;
        for (const AnnoShape &shape : m_project->annotationsAt(frame).shapes()) {
            if (shape.labelId() == labelId)
                ++onThisFrame;
        }
        if (onThisFrame > 0) {
            usage.frameShapes += onThisFrame;
            ++usage.frames;
        }
    }

    for (int trackId : m_project->tracks().trackIds()) {
        const AnnoTrack *track = m_project->tracks().track(trackId);
        if (track && track->labelId() == labelId)
            ++usage.tracks;
    }
    return usage;
}

void AnnotationController::removeLabelClass(int labelId)
{
    if (!m_project->labelSchema().findClass(labelId))
        return;

    beginCompound(QStringLiteral("Remove label class"));

    // Shapes are removed from the highest index down: each push executes
    // immediately, so removing low indices first would invalidate the ones above.
    const int frameCount = m_project->frameCount();
    for (int frame = 0; frame < frameCount; ++frame) {
        const QVector<AnnoShape> shapes = m_project->annotationsAt(frame).shapes();
        for (int i = shapes.size() - 1; i >= 0; --i) {
            if (shapes.at(i).labelId() != labelId)
                continue;

            auto *command = new RemoveShapeCommand(this, m_project, frame, i);
            if (command->isValid())
                m_undoStack->push(command);
            else
                delete command;
        }
    }

    for (int trackId : m_project->tracks().trackIds()) {
        const AnnoTrack *track = m_project->tracks().track(trackId);
        if (track && track->labelId() == labelId)
            deleteTrack(trackId);
    }

    auto *schemaCommand = new RemoveLabelClassCommand(this, m_project, labelId,
                                                      QStringLiteral("Remove label class"));
    if (schemaCommand->isValid())
        m_undoStack->push(schemaCommand);
    else
        delete schemaCommand;

    endCompound();
}

void AnnotationController::notifyFrameChanged(int frameIndex)
{
    emit frameAnnotationsChanged(frameIndex);
}

void AnnotationController::notifyLabelSchemaChanged()
{
    emit labelSchemaChanged();
}

void AnnotationController::notifyTracksChanged()
{
    emit tracksChanged();
}

void AnnotationController::clearHistory()
{
    m_undoStack->clear();
}
