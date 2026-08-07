#include "annotationcommands.h"
#include "annotationcontroller.h"
#include "project.h"

AddShapeCommand::AddShapeCommand(AnnotationController *controller, Project *project,
                                 int frameIndex, const AnnoShape &shape)
    : QUndoCommand(QStringLiteral("Add %1").arg(shapeTypeToString(shape.type())))
    , m_controller(controller)
    , m_project(project)
    , m_frameIndex(frameIndex)
    , m_shape(shape)
{
}

void AddShapeCommand::redo()
{
    FrameAnnotations &frame = m_project->annotationsAt(m_frameIndex);
    m_insertedAt = frame.shapes().size();
    frame.addShape(m_shape);
    m_controller->notifyFrameChanged(m_frameIndex);
}

void AddShapeCommand::undo()
{
    if (m_insertedAt >= 0)
        m_project->annotationsAt(m_frameIndex).removeShapeAt(m_insertedAt);
    m_controller->notifyFrameChanged(m_frameIndex);
}

RemoveShapeCommand::RemoveShapeCommand(AnnotationController *controller, Project *project,
                                       int frameIndex, int shapeIndex)
    : QUndoCommand(QStringLiteral("Delete shape"))
    , m_controller(controller)
    , m_project(project)
    , m_frameIndex(frameIndex)
    , m_shapeIndex(shapeIndex)
{
    if (const AnnoShape *existing = project->annotationsAt(frameIndex).shapeAt(shapeIndex)) {
        m_shape = *existing;
        m_valid = true;
        setText(QStringLiteral("Delete %1").arg(shapeTypeToString(m_shape.type())));
    }
}

void RemoveShapeCommand::redo()
{
    m_project->annotationsAt(m_frameIndex).removeShapeAt(m_shapeIndex);
    m_controller->notifyFrameChanged(m_frameIndex);
}

void RemoveShapeCommand::undo()
{
    m_project->annotationsAt(m_frameIndex).insertShape(m_shapeIndex, m_shape);
    m_controller->notifyFrameChanged(m_frameIndex);
}

ReplaceShapeCommand::ReplaceShapeCommand(AnnotationController *controller, Project *project,
                                         int frameIndex, int shapeIndex,
                                         const AnnoShape &before, const AnnoShape &after,
                                         const QString &text)
    : QUndoCommand(text)
    , m_controller(controller)
    , m_project(project)
    , m_frameIndex(frameIndex)
    , m_shapeIndex(shapeIndex)
    , m_before(before)
    , m_after(after)
{
}

void ReplaceShapeCommand::redo()
{
    m_project->annotationsAt(m_frameIndex).replaceShapeAt(m_shapeIndex, m_after);
    m_controller->notifyFrameChanged(m_frameIndex);
}

void ReplaceShapeCommand::undo()
{
    m_project->annotationsAt(m_frameIndex).replaceShapeAt(m_shapeIndex, m_before);
    m_controller->notifyFrameChanged(m_frameIndex);
}

SetFrameAnnotationsCommand::SetFrameAnnotationsCommand(AnnotationController *controller,
                                                       Project *project, int frameIndex,
                                                       const FrameAnnotations &before,
                                                       const FrameAnnotations &after,
                                                       const QString &text)
    : QUndoCommand(text)
    , m_controller(controller)
    , m_project(project)
    , m_frameIndex(frameIndex)
    , m_before(before)
    , m_after(after)
{
}

void SetFrameAnnotationsCommand::redo()
{
    m_project->setAnnotations(m_frameIndex, m_after);
    m_controller->notifyFrameChanged(m_frameIndex);
}

void SetFrameAnnotationsCommand::undo()
{
    m_project->setAnnotations(m_frameIndex, m_before);
    m_controller->notifyFrameChanged(m_frameIndex);
}

SetTrackKeyframeCommand::SetTrackKeyframeCommand(AnnotationController *controller, Project *project,
                                                 int trackId, int frameIndex,
                                                 const AnnoShape &shape, const QString &text)
    : QUndoCommand(text)
    , m_controller(controller)
    , m_project(project)
    , m_trackId(trackId)
    , m_frameIndex(frameIndex)
    , m_after(shape)
{
    const AnnoTrack *track = project->tracks().track(trackId);
    if (!track || frameIndex < 0)
        return;

    m_hadKeyframe = track->hasKeyframeAt(frameIndex);
    if (m_hadKeyframe)
        m_before = track->keyframes().value(frameIndex);
    m_valid = true;
}

void SetTrackKeyframeCommand::redo()
{
    AnnoTrack *track = m_project->tracks().track(m_trackId);
    if (!track)
        return;

    // Flags already on this frame survive a geometry edit, so nudging a box does
    // not silently mark an occluded object visible again.
    track->setKeyframe(m_frameIndex, m_after,
                       m_hadKeyframe && m_before.outside,
                       m_hadKeyframe && m_before.occluded);
    m_controller->notifyTracksChanged();
}

void SetTrackKeyframeCommand::undo()
{
    AnnoTrack *track = m_project->tracks().track(m_trackId);
    if (!track)
        return;

    if (m_hadKeyframe)
        track->setKeyframe(m_frameIndex, m_before.shape, m_before.outside, m_before.occluded);
    else
        track->removeKeyframe(m_frameIndex);
    m_controller->notifyTracksChanged();
}

SetTrackCommand::SetTrackCommand(AnnotationController *controller, Project *project,
                                 int trackId, const AnnoTrack &after, const QString &text)
    : QUndoCommand(text)
    , m_controller(controller)
    , m_project(project)
    , m_trackId(trackId)
    , m_after(after)
{
    if (const AnnoTrack *existing = project->tracks().track(trackId)) {
        m_before = *existing;
        m_existedBefore = true;
    }
}

void SetTrackCommand::applyState(const AnnoTrack &track, bool existed)
{
    if (!existed || track.isEmpty())
        m_project->tracks().removeTrack(m_trackId);
    else
        m_project->tracks().insertTrack(track);

    m_controller->notifyTracksChanged();
}

void SetTrackCommand::redo()
{
    applyState(m_after, true);
}

void SetTrackCommand::undo()
{
    applyState(m_before, m_existedBefore);
}

RemoveLabelClassCommand::RemoveLabelClassCommand(AnnotationController *controller, Project *project,
                                                 int labelId, const QString &text)
    : QUndoCommand(text)
    , m_controller(controller)
    , m_project(project)
{
    const LabelClass *existing = project->labelSchema().findClass(labelId);
    if (!existing)
        return;

    m_class = *existing;
    m_index = project->labelSchema().classIndex(labelId);
    m_valid = true;
}

void RemoveLabelClassCommand::redo()
{
    m_project->labelSchema().removeClass(m_class.id);
    m_controller->notifyLabelSchemaChanged();
}

void RemoveLabelClassCommand::undo()
{
    m_project->labelSchema().insertClassAt(m_index, m_class);
    m_controller->notifyLabelSchemaChanged();
}
