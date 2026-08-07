#ifndef FRAMEANNOTATIONS_H
#define FRAMEANNOTATIONS_H

#include "annoshape.h"

#include <QSize>
#include <QVector>

class QJsonObject;

enum class ReviewStatus {
    Unlabeled,
    InProgress,
    Completed,
    Skipped
};

class FrameAnnotations
{
public:
    const QVector<AnnoShape> &shapes() const { return m_shapes; }

    void addShape(const AnnoShape &shape);
    void insertShape(int index, const AnnoShape &shape);
    bool removeShapeAt(int index);
    bool replaceShapeAt(int index, const AnnoShape &shape);
    const AnnoShape *shapeAt(int index) const;
    int indexOfShapeId(const QString &id) const;
    bool isEmpty() const { return m_shapes.isEmpty(); }

    // Pixel size of the frame these shapes were drawn on. Every dataset writer
    // needs it — COCO records it, YOLO normalises by it — and recovering it
    // later means decoding the source again, so it is stored with the shapes.
    QSize imageSize() const { return m_imageSize; }
    void setImageSize(const QSize &size) { m_imageSize = size; }
    bool hasImageSize() const { return m_imageSize.isValid() && !m_imageSize.isEmpty(); }

    ReviewStatus status() const { return m_status; }
    void setStatus(ReviewStatus status) { m_status = status; }

    QJsonObject toJson() const;
    static FrameAnnotations fromJson(const QJsonObject &obj);

private:
    QVector<AnnoShape> m_shapes;
    QSize m_imageSize;
    ReviewStatus m_status = ReviewStatus::Unlabeled;
};

#endif // FRAMEANNOTATIONS_H
