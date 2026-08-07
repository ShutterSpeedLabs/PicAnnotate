#ifndef LABELSCHEMA_H
#define LABELSCHEMA_H

#include "keypointtemplate.h"
#include "labelclass.h"

#include <QVector>

class QJsonArray;
class QJsonObject;

class LabelSchema
{
public:
    const QVector<LabelClass> &classes() const { return m_classes; }

    int addClass(const QString &name, const QColor &color);
    int addClass(const QString &name, const QColor &color, ShapeTypeFlags allowedTypes,
                 const QString &keypointTemplateId = QString());
    bool removeClass(int id);

    // Restores a class at a specific position. Class order is not cosmetic — it
    // determines COCO category ids and YOLO class indices — so undoing a removal
    // has to put the class back where it was, not append it.
    bool insertClassAt(int index, const LabelClass &label);

    // The one place class colours are generated. Both manual and imported classes
    // use it, so a project's labels stay visually consistent however they arrived.
    static QColor suggestedColorForIndex(int index);

    bool renameClass(int id, const QString &name);
    bool setClassColor(int id, const QColor &color);
    bool setClassAllowedTypes(int id, ShapeTypeFlags allowedTypes);
    bool setClassKeypointTemplate(int id, const QString &templateId);

    const LabelClass *findClass(int id) const;
    const LabelClass *findClassByName(const QString &name) const;

    // Returns the existing class with this name, or creates one. Used by every
    // importer so a dataset's category list merges instead of duplicating.
    int ensureClass(const QString &name, ShapeTypeFlags allowedTypes = AllShapeTypes,
                    const QString &keypointTemplateId = QString());

    // Classes in declaration order, which is the order exporters use for
    // category ids and YOLO class indices.
    QVector<int> orderedClassIds() const;
    int classIndex(int id) const;

    const QVector<KeypointTemplate> &keypointTemplates() const { return m_keypointTemplates; }
    void addKeypointTemplate(const KeypointTemplate &tmpl);
    const KeypointTemplate *findKeypointTemplate(const QString &id) const;

    // Template a shape of the given class should use. Falls back to COCO-17 so
    // skeletons always have names and edges to draw and export with.
    KeypointTemplate templateForClass(int labelId) const;

    // Legacy array form: classes only, no keypoint templates.
    QJsonArray toJson() const;
    static LabelSchema fromJson(const QJsonArray &array);

    QJsonObject toJsonObject() const;
    static LabelSchema fromJsonObject(const QJsonObject &obj);

private:
    QVector<LabelClass> m_classes;
    QVector<KeypointTemplate> m_keypointTemplates;
    int m_nextId = 0;
};

#endif // LABELSCHEMA_H
