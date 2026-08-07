#ifndef ANNOTATIONCOMMANDS_H
#define ANNOTATIONCOMMANDS_H

#include "annoshape.h"
#include "frameannotations.h"
#include "labelclass.h"
#include "tracktimeline.h"

#include <QUndoCommand>

class AnnotationController;
class Project;

// The shape items emit their change signals once per gesture (on mouse release),
// so every command here is already one user action and none of them merge.

class AddShapeCommand : public QUndoCommand
{
public:
    AddShapeCommand(AnnotationController *controller, Project *project,
                    int frameIndex, const AnnoShape &shape);

    void undo() override;
    void redo() override;

private:
    AnnotationController *m_controller;
    Project *m_project;
    int m_frameIndex;
    AnnoShape m_shape;
    int m_insertedAt = -1;
};

class RemoveShapeCommand : public QUndoCommand
{
public:
    RemoveShapeCommand(AnnotationController *controller, Project *project,
                       int frameIndex, int shapeIndex);

    bool isValid() const { return m_valid; }

    void undo() override;
    void redo() override;

private:
    AnnotationController *m_controller;
    Project *m_project;
    int m_frameIndex;
    int m_shapeIndex;
    AnnoShape m_shape;
    bool m_valid = false;
};

class ReplaceShapeCommand : public QUndoCommand
{
public:
    ReplaceShapeCommand(AnnotationController *controller, Project *project,
                        int frameIndex, int shapeIndex,
                        const AnnoShape &before, const AnnoShape &after,
                        const QString &text);

    void undo() override;
    void redo() override;

private:
    AnnotationController *m_controller;
    Project *m_project;
    int m_frameIndex;
    int m_shapeIndex;
    AnnoShape m_before;
    AnnoShape m_after;
};

// Whole-frame swap: used for "clear frame" and for importers that replace a
// frame's contents wholesale.
class SetFrameAnnotationsCommand : public QUndoCommand
{
public:
    SetFrameAnnotationsCommand(AnnotationController *controller, Project *project,
                               int frameIndex, const FrameAnnotations &before,
                               const FrameAnnotations &after, const QString &text);

    void undo() override;
    void redo() override;

private:
    AnnotationController *m_controller;
    Project *m_project;
    int m_frameIndex;
    FrameAnnotations m_before;
    FrameAnnotations m_after;
};

// Sets one keyframe, storing only that keyframe rather than the whole track.
//
// This is the command a tracking run pushes once per frame. A whole-track
// snapshot would make a 500-frame run cost on the order of n^2 keyframe copies,
// since each command would carry a copy of an ever-growing track.
class SetTrackKeyframeCommand : public QUndoCommand
{
public:
    SetTrackKeyframeCommand(AnnotationController *controller, Project *project,
                            int trackId, int frameIndex, const AnnoShape &shape,
                            const QString &text);

    bool isValid() const { return m_valid; }

    void undo() override;
    void redo() override;

private:
    AnnotationController *m_controller;
    Project *m_project;
    int m_trackId;
    int m_frameIndex;
    AnnoShape m_after;
    TrackKeyframe m_before;
    bool m_hadKeyframe = false;
    bool m_valid = false;
};

// Whole-track swap, for the infrequent structural edits — delete a keyframe,
// truncate, toggle outside/occluded, relabel, delete the track. These are single
// user actions rather than loops, so snapshotting the track keeps undo trivially
// correct without the memory cost mattering.
class SetTrackCommand : public QUndoCommand
{
public:
    SetTrackCommand(AnnotationController *controller, Project *project,
                    int trackId, const AnnoTrack &after, const QString &text);

    void undo() override;
    void redo() override;

private:
    void applyState(const AnnoTrack &track, bool existed);

    AnnotationController *m_controller;
    Project *m_project;
    int m_trackId;
    AnnoTrack m_before;
    AnnoTrack m_after;
    bool m_existedBefore = false;
};

// Removes a label class from the schema, remembering its position so undo puts it
// back in the same slot. Shapes and tracks using the class are removed by separate
// commands in the same macro, so this one only touches the schema.
class RemoveLabelClassCommand : public QUndoCommand
{
public:
    RemoveLabelClassCommand(AnnotationController *controller, Project *project,
                            int labelId, const QString &text);

    bool isValid() const { return m_valid; }

    void undo() override;
    void redo() override;

private:
    AnnotationController *m_controller;
    Project *m_project;
    LabelClass m_class;
    int m_index = -1;
    bool m_valid = false;
};

#endif // ANNOTATIONCOMMANDS_H
