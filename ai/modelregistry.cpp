#include "modelregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const char *kBuiltInCatalog = ":/ai/models.json";

// Parses a catalogue document of the form
//   { "labelSets": [ ... ], "models": [ ... ] }
// Both arrays are optional so a user catalogue can carry models alone.
bool readCatalog(const QByteArray &data, QVector<LabelSet> *labelSets,
                 QVector<ModelSpec> *models, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("Expected a JSON object at the top level.");
        return false;
    }

    const QJsonObject root = doc.object();

    const QJsonArray sets = root.value(QStringLiteral("labelSets")).toArray();
    for (const QJsonValue &value : sets) {
        const LabelSet set = LabelSet::fromJson(value.toObject());
        if (!set.id.isEmpty())
            labelSets->append(set);
    }

    const QJsonArray entries = root.value(QStringLiteral("models")).toArray();
    for (const QJsonValue &value : entries) {
        const ModelSpec spec = ModelSpec::fromJson(value.toObject());
        if (spec.isValid())
            models->append(spec);
    }

    return true;
}

} // namespace

ModelRegistry::ModelRegistry()
{
    reload();
}

void ModelRegistry::reload()
{
    m_order.clear();
    m_models.clear();
    m_labelSets.clear();
    m_userIds.clear();

    loadBuiltIn();
    loadUserCatalog();
}

void ModelRegistry::loadBuiltIn()
{
    QFile file(QString::fromLatin1(kBuiltInCatalog));
    if (!file.open(QIODevice::ReadOnly))
        return;

    QVector<LabelSet> labelSets;
    QVector<ModelSpec> models;
    if (!readCatalog(file.readAll(), &labelSets, &models, nullptr))
        return;

    for (const LabelSet &set : labelSets)
        m_labelSets.insert(set.id, set);
    for (const ModelSpec &spec : models)
        mergeSpec(spec, false);
}

void ModelRegistry::loadUserCatalog()
{
    QFile file(userCatalogPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;

    QVector<LabelSet> labelSets;
    QVector<ModelSpec> models;
    if (!readCatalog(file.readAll(), &labelSets, &models, nullptr))
        return;

    for (const LabelSet &set : labelSets)
        m_labelSets.insert(set.id, set);
    for (const ModelSpec &spec : models)
        mergeSpec(spec, true);
}

void ModelRegistry::mergeSpec(const ModelSpec &spec, bool userDefined)
{
    if (!m_models.contains(spec.id))
        m_order.append(spec.id);

    m_models.insert(spec.id, spec);
    if (userDefined)
        m_userIds.insert(spec.id);
    else
        m_userIds.remove(spec.id);
}

QVector<ModelSpec> ModelRegistry::allModels() const
{
    QVector<ModelSpec> result;
    result.reserve(m_order.size());
    for (const QString &id : m_order) {
        const auto it = m_models.constFind(id);
        if (it != m_models.constEnd())
            result.append(it.value());
    }
    return result;
}

QVector<ModelSpec> ModelRegistry::modelsForTask(ModelTask task) const
{
    QVector<ModelSpec> result;
    for (const QString &id : m_order) {
        const auto it = m_models.constFind(id);
        if (it != m_models.constEnd() && it.value().task == task)
            result.append(it.value());
    }
    return result;
}

const ModelSpec *ModelRegistry::model(const QString &id) const
{
    const auto it = m_models.constFind(id);
    return it == m_models.constEnd() ? nullptr : &it.value();
}

const LabelSet *ModelRegistry::labelSet(const QString &id) const
{
    const auto it = m_labelSets.constFind(id);
    return it == m_labelSets.constEnd() ? nullptr : &it.value();
}

QStringList ModelRegistry::classNamesFor(const ModelSpec &spec) const
{
    const LabelSet *set = labelSet(spec.labelSetId);
    return set ? set->classNames : QStringList();
}

bool ModelRegistry::isUserModel(const QString &id) const
{
    return m_userIds.contains(id);
}

bool ModelRegistry::addUserModel(const ModelSpec &spec, QString *error)
{
    if (!spec.isValid()) {
        if (error)
            *error = QStringLiteral("A model needs at least an id and a file name.");
        return false;
    }

    mergeSpec(spec, true);
    return saveUserCatalog(error);
}

bool ModelRegistry::removeUserModel(const QString &id, QString *error)
{
    if (!m_userIds.contains(id)) {
        if (error)
            *error = QStringLiteral("Only models you added yourself can be removed.");
        return false;
    }

    m_userIds.remove(id);
    m_models.remove(id);
    m_order.removeAll(id);

    if (!saveUserCatalog(error))
        return false;

    // A user entry may have been shadowing a built-in one; bring that back.
    loadBuiltIn();
    return true;
}

bool ModelRegistry::saveUserCatalog(QString *error) const
{
    const QString path = userCatalogPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create %1.").arg(QFileInfo(path).absolutePath());
        return false;
    }

    QJsonArray models;
    for (const QString &id : m_order) {
        if (!m_userIds.contains(id))
            continue;
        const auto it = m_models.constFind(id);
        if (it != m_models.constEnd())
            models.append(it.value().toJson());
    }

    QJsonObject root;
    root.insert(QStringLiteral("models"), models);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Could not save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString ModelRegistry::userCatalogPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/models/custom-models.json");
}
