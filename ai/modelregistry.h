#ifndef MODELREGISTRY_H
#define MODELREGISTRY_H

#include "modelspec.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

// The catalogue of models the app knows about, whether or not their files are
// present. Built-in entries are loaded from the compiled-in :/ai/models.json;
// anything the user registers by hand is merged on top and persisted, so a
// custom fine-tuned model is a first-class citizen alongside the public ones.
//
// This is deliberately data, not code: adding YOLO11 or a new SAM export means
// editing one JSON file. Nothing here loads or runs a model — see ModelManager
// for files on disk and the engines for actually running them.
class ModelRegistry
{
public:
    ModelRegistry();

    // Re-reads the built-in catalogue and the user's custom entries.
    void reload();

    QVector<ModelSpec> allModels() const;
    QVector<ModelSpec> modelsForTask(ModelTask task) const;

    // Nothing if no such id is registered.
    const ModelSpec *model(const QString &id) const;

    const LabelSet *labelSet(const QString &id) const;
    QStringList classNamesFor(const ModelSpec &spec) const;

    // Adds or replaces a user-defined entry and writes it to the user catalogue.
    // Built-in ids can be shadowed this way, which is how someone points the app
    // at their own re-export of a stock model.
    bool addUserModel(const ModelSpec &spec, QString *error);
    bool removeUserModel(const QString &id, QString *error);
    bool isUserModel(const QString &id) const;

    // Where user-defined specs are stored, shown in the Model Manager so the
    // file can be edited or shared.
    static QString userCatalogPath();

private:
    void loadBuiltIn();
    void loadUserCatalog();
    bool saveUserCatalog(QString *error) const;
    void mergeSpec(const ModelSpec &spec, bool userDefined);

    // Insertion order is the display order, so the catalogue reads the way it
    // was written rather than in hash order.
    QVector<QString> m_order;
    QHash<QString, ModelSpec> m_models;
    QHash<QString, LabelSet> m_labelSets;
    QSet<QString> m_userIds;
};

#endif // MODELREGISTRY_H
