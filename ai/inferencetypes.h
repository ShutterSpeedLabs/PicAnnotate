#ifndef INFERENCETYPES_H
#define INFERENCETYPES_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

// Where a model is asked to run. Providers are a request, not a guarantee: if
// the requested one is missing from the ONNX Runtime build, OnnxSession falls
// back to CPU and reports which provider it actually got, so the UI can say so
// instead of silently running 20x slower than the user expects.
enum class ExecutionProvider {
    Cpu,
    DirectMl,   // Windows, any DX12 GPU (Intel/AMD/NVIDIA)
    Cuda        // NVIDIA only, needs the CUDA-enabled ORT build
};

QString executionProviderToString(ExecutionProvider provider);
QString executionProviderDisplayName(ExecutionProvider provider);
ExecutionProvider executionProviderFromString(const QString &s);

// One region produced by a model, in image pixel coordinates.
//
// `classIndex` indexes the *model's own* label set (COCO-80 for stock YOLO), not
// the project's LabelSchema. Mapping a model class onto a LabelClass is a
// separate, user-visible decision, so the two id spaces are kept apart here.
struct Detection
{
    QRectF box;
    int classIndex = -1;
    QString className;
    float score = 0.0f;

    // Outline for a segmentation result; empty for a plain box.
    QVector<QPointF> polygon;

    bool hasPolygon() const { return polygon.size() >= 3; }
};

#endif // INFERENCETYPES_H
