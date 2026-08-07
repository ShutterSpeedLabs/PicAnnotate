#ifndef ANNOTATIONCONTROLLER_H
#define ANNOTATIONCONTROLLER_H

#include "annoshape.h"
#include "frameannotations.h"
#include "tracktimeline.h"

#include <QObject>

class Project;
class QUndoStack;
class QAction;

// Single funnel for every annotation mutation. Routing edits through here (and
// never through Project directly) is what makes undo/redo reliable: each change
// becomes a command, and the UI is told to refresh exactly once per change.
class AnnotationController : public QObject
{
    Q_OBJECT
public:
    explicit AnnotationController(Project *project, QObject *parent = nullptr);

    QUndoStack *undoStack() const { return m_undoStack; }
    QAction *createUndoAction(QObject *parent) const;
    QAction *createRedoAction(QObject *parent) const;

    void addShape(int frameIndex, const AnnoShape &shape);
    void setFrameAnnotations(int frameIndex, const FrameAnnotations &annotations,
                             const QString &commandText);
    void clearFrame(int frameIndex);
    void clearAll();

    // ---- edits addressed the way the UI sees a frame ------------------------
    //
    // A shape index on a frame may resolve to a shape annotated on that frame or
    // to a track. These route to the right one, so editing an interpolated track
    // shape turns into a new keyframe rather than a stray per-frame shape.
    void editShape(int frameIndex, int shapeIndex, const AnnoShape &shape,
                   const QString &commandText = QString());
    void deleteShape(int frameIndex, int shapeIndex);

    // ---- track operations ---------------------------------------------------
    int startTrack(int frameIndex, const AnnoShape &shape, ShapeType type);
    void setTrackKeyframe(int trackId, int frameIndex, const AnnoShape &shape,
                          const QString &commandText = QString());
    void removeTrackKeyframe(int trackId, int frameIndex);
    void endTrackAt(int trackId, int frameIndex);
    void setTrackOutside(int trackId, int frameIndex, bool outside);
    void setTrackOccluded(int trackId, int frameIndex, bool occluded);
    void setTrackLabel(int trackId, int labelId);
    void deleteTrack(int trackId);

    // Converts a frame shape into a one-keyframe track, so an object first drawn
    // as a plain box can start being followed without being redrawn.
    int promoteToTrack(int frameIndex, int shapeIndex);

    // Groups everything pushed until endCompound() into a single undo step. Used
    // by a tracking run, which would otherwise leave one undo entry per frame.
    // Calls nest; only the outermost pair opens and closes the macro.
    void beginCompound(const QString &text);
    void endCompound();
    bool inCompound() const { return m_compoundDepth > 0; }

    // ---- label schema ------------------------------------------------------
    struct LabelUsage
    {
        int frameShapes = 0;
        int frames = 0;
        int tracks = 0;

        bool isUsed() const { return frameShapes > 0 || tracks > 0; }
    };

    // How much annotation work references a class. Used to tell the user what a
    // removal will actually destroy before they confirm it.
    LabelUsage labelUsage(int labelId) const;

    // Removes a class along with every shape and track using it, as one undo step.
    void removeLabelClass(int labelId);

    // Called by the commands themselves once they have touched the project.
    void notifyFrameChanged(int frameIndex);
    void notifyTracksChanged();
    void notifyLabelSchemaChanged();

    void clearHistory();

signals:
    // Emitted after any mutation. `frameIndex` is the frame that changed, which
    // may not be the frame currently on screen (undo after navigating away).
    void frameAnnotationsChanged(int frameIndex);

    // A track changed, so any frame it spans may now look different.
    void tracksChanged();

    // The label list changed: names and colours are baked into drawn shapes, so
    // the label panel and the canvas both need rebuilding.
    void labelSchemaChanged();

private:
    void pushTrackState(int trackId, const AnnoTrack &track, const QString &commandText);

    Project *m_project;
    QUndoStack *m_undoStack;
    int m_compoundDepth = 0;
};

#endif // ANNOTATIONCONTROLLER_H
