#include "tensor.h"

#include <utility>

Tensor Tensor::fromFloats(const QVector<qint64> &shape, std::vector<float> data)
{
    Tensor t;
    t.shape = shape;
    t.type = Type::Float32;
    t.f32 = std::move(data);
    return t;
}

Tensor Tensor::fromInt64s(const QVector<qint64> &shape, std::vector<qint64> data)
{
    Tensor t;
    t.shape = shape;
    t.type = Type::Int64;
    t.i64 = std::move(data);
    return t;
}

Tensor Tensor::fromInt32s(const QVector<qint64> &shape, std::vector<qint32> data)
{
    Tensor t;
    t.shape = shape;
    t.type = Type::Int32;
    t.i32 = std::move(data);
    return t;
}

qint64 Tensor::elementCount() const
{
    if (shape.isEmpty())
        return 0;

    qint64 count = 1;
    for (qint64 extent : shape) {
        if (extent < 0)
            return 0;   // dynamic dimension: no meaningful element count
        count *= extent;
    }
    return count;
}

qint64 Tensor::dim(int index) const
{
    return (index >= 0 && index < shape.size()) ? shape.at(index) : 0;
}

bool Tensor::isValid() const
{
    const qint64 expected = elementCount();
    if (expected <= 0)
        return false;

    switch (type) {
    case Type::Float32: return static_cast<qint64>(f32.size()) == expected;
    case Type::Int32:   return static_cast<qint64>(i32.size()) == expected;
    case Type::Int64:   return static_cast<qint64>(i64.size()) == expected;
    }
    return false;
}

QString Tensor::shapeString() const
{
    if (shape.isEmpty())
        return QStringLiteral("<empty>");

    QString text;
    for (int i = 0; i < shape.size(); ++i) {
        if (i > 0)
            text += QLatin1Char('x');
        const qint64 extent = shape.at(i);
        text += extent < 0 ? QStringLiteral("?") : QString::number(extent);
    }
    return text;
}

void Tensor::clear()
{
    shape.clear();
    f32.clear();
    i32.clear();
    i64.clear();
    type = Type::Float32;
}
