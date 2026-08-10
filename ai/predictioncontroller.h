#ifndef PREDICTIONCONTROLLER_H
#define PREDICTIONCONTROLLER_H

#include <QObject>
#include <QString>

class AnnotationController;
class PredictionStore;
class Project;

// Turns predictions into annotations.
//
// The one place that crossing happens, for the same reason AnnotationController
// is the one place annotations are mutated: accepting a batch has to be a single
// undo step, and mapping a model's class name onto a project class has to work
// identically whether it was triggered from the panel, from a keyboard shortcut,
// or from the dataset wizard's unattended pass.
class PredictionController : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        // Create a LabelClass for a model class the schema has no match for.
        // With this off, predictions whose class is unknown are left alone
        // rather than accepted as unlabelled shapes.
        bool createMissingClasses = true;

        // Prefer a segmentation model's polygon over its bounding box. Turning
        // it off downgrades masks to boxes, which is what a detection-only
        // dataset wants.
        bool preferPolygons = true;
    };

    PredictionController(Project *project, AnnotationController *annotations,
                         PredictionStore *store, QObject *parent = nullptr);

    Options &options() { return m_options; }
    const Options &options() const { return m_options; }

    // Accepts one visible prediction. False when the id is unknown, the
    // prediction is below the threshold, or its class could not be resolved.
    bool acceptOne(int frame, const QString &id);

    // Accepts every *visible* prediction on a frame as one undo step. Returns
    // how many were taken; anything skipped stays in the store.
    int acceptFrame(int frame);

    // Accepts every visible prediction on every frame, as one undo step.
    int acceptAll();

    void rejectOne(int frame, const QString &id);
    void rejectFrame(int frame);
    void rejectAll();

    // Project class matching a model's class name, creating it when allowed.
    // -1 when there is no match and creation is off.
    int resolveLabelId(const QString &modelClassName);

signals:
    // `accepted` shapes were added; `skipped` predictions could not be resolved.
    void accepted(int accepted, int skipped);

private:
    // Shared by all three accept entry points. Assumes the caller has opened a
    // compound undo step when more than one frame is involved.
    int acceptFrameInternal(int frame);

    Project *m_project;
    AnnotationController *m_annotations;
    PredictionStore *m_store;
    Options m_options;
};

#endif // PREDICTIONCONTROLLER_H
