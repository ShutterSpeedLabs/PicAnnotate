#ifndef TENSOR_H
#define TENSOR_H

#include <QString>
#include <QVector>

#include <cstdint>
#include <vector>

// A dense N-D array crossing the boundary between the app and ONNX Runtime.
//
// Only the three element types the models here actually use are supported:
// float32 for images and embeddings, int64 for CLIP token ids, int32 for the
// odd graph that wants narrow indices. Keeping this a plain struct means nothing
// outside ai/onnxsession.cpp has to see an ORT header.
struct Tensor
{
    enum class Type { Float32, Int32, Int64 };

    QVector<qint64> shape;
    Type type = Type::Float32;

    std::vector<float> f32;
    std::vector<qint32> i32;
    std::vector<qint64> i64;

    static Tensor fromFloats(const QVector<qint64> &shape, std::vector<float> data);
    static Tensor fromInt64s(const QVector<qint64> &shape, std::vector<qint64> data);
    static Tensor fromInt32s(const QVector<qint64> &shape, std::vector<qint32> data);

    // Product of the dimensions, or 0 for an empty shape. A dimension of -1
    // (dynamic) makes this meaningless, so it returns 0 for those too.
    qint64 elementCount() const;

    // True when the shape is concrete and the buffer length matches it.
    bool isValid() const;

    qint64 dim(int index) const;
    int rank() const { return shape.size(); }

    // Human-readable "1x3x640x640", for error messages that would otherwise be
    // impossible to act on.
    QString shapeString() const;

    void clear();
    bool isEmpty() const { return shape.isEmpty(); }
};

// A tensor bound to the graph input it feeds.
struct NamedTensor
{
    QString name;
    Tensor tensor;
};

#endif // TENSOR_H
