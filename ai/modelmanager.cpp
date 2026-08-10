#include "modelmanager.h"
#include "modeldownload.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace {

const char *kProviderKey = "ai/executionProvider";

} // namespace

ModelManager &ModelManager::instance()
{
    static ModelManager manager;
    return manager;
}

ModelManager::ModelManager()
    : m_network(new QNetworkAccessManager(this))
{
}

QStringList ModelManager::searchPaths() const
{
    // Next to the executable first: that is where the .pro copies bundled models
    // (the NanoTrack pair already lives there), so a shipped build works with no
    // per-user state at all.
    return {QCoreApplication::applicationDirPath() + QStringLiteral("/models"),
            downloadDirectory()};
}

QString ModelManager::downloadDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/models");
}

QString ModelManager::settingsKeyForPath(const QString &modelId) const
{
    return QStringLiteral("ai/modelPaths/") + modelId;
}

QString ModelManager::settingsKeyForSelection(ModelTask task) const
{
    return QStringLiteral("ai/selected/") + modelTaskToString(task);
}

QString ModelManager::userPath(const QString &modelId) const
{
    QSettings settings;
    return settings.value(settingsKeyForPath(modelId)).toString();
}

void ModelManager::setUserPath(const QString &modelId, const QString &absolutePath)
{
    QSettings settings;
    if (absolutePath.isEmpty())
        settings.remove(settingsKeyForPath(modelId));
    else
        settings.setValue(settingsKeyForPath(modelId), absolutePath);

    emit availabilityChanged(modelId);
}

void ModelManager::clearUserPath(const QString &modelId)
{
    setUserPath(modelId, QString());
}

QString ModelManager::localPath(const QString &modelId) const
{
    const ModelSpec *spec = m_registry.model(modelId);
    if (!spec)
        return QString();

    const QString pinned = userPath(modelId);
    if (!pinned.isEmpty() && QFileInfo::exists(pinned))
        return pinned;

    for (const QString &dir : searchPaths()) {
        const QString candidate = QDir(dir).filePath(spec->fileName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return QString();
}

bool ModelManager::isAvailable(const QString &modelId) const
{
    return !localPath(modelId).isEmpty();
}

bool ModelManager::onnxRuntimeAvailable()
{
#ifdef HAVE_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

QString ModelManager::onnxRuntimeVersion()
{
#ifdef HAVE_ONNXRUNTIME
    return QString::fromUtf8(OrtGetApiBase()->GetVersionString());
#else
    return QString();
#endif
}

bool ModelManager::isUsable(const QString &modelId, QString *reason) const
{
    const ModelSpec *spec = m_registry.model(modelId);
    if (!spec) {
        if (reason)
            *reason = tr("No model is registered under the id \"%1\".").arg(modelId);
        return false;
    }

    if (spec->runtime == ModelRuntime::OnnxRuntime && !onnxRuntimeAvailable()) {
        if (reason)
            *reason = tr("%1 needs ONNX Runtime, which this build was compiled without. "
                         "See docs/AI_SETUP.md.")
                          .arg(spec->displayName);
        return false;
    }

    if (localPath(modelId).isEmpty()) {
        if (reason) {
            *reason = spec->isDownloadable()
                          ? tr("%1 has not been downloaded yet.").arg(spec->displayName)
                          : tr("%1 is not on disk. Point the Model Manager at the file.")
                                .arg(spec->displayName);
        }
        return false;
    }

    if (reason)
        reason->clear();
    return true;
}

ExecutionProvider ModelManager::preferredProvider() const
{
    QSettings settings;
    return executionProviderFromString(
        settings.value(QLatin1String(kProviderKey), QStringLiteral("cpu")).toString());
}

void ModelManager::setPreferredProvider(ExecutionProvider provider)
{
    if (provider == preferredProvider())
        return;

    QSettings settings;
    settings.setValue(QLatin1String(kProviderKey), executionProviderToString(provider));
    emit preferredProviderChanged(provider);
}

QString ModelManager::selectedModelId(ModelTask task) const
{
    QSettings settings;
    const QString stored = settings.value(settingsKeyForSelection(task)).toString();
    if (!stored.isEmpty() && m_registry.model(stored))
        return stored;

    // Nothing chosen (or the choice refers to a model that has since been
    // removed): prefer one that would actually run, and only then fall back to
    // the first entry so the UI has something to show and explain.
    const QVector<ModelSpec> candidates = m_registry.modelsForTask(task);
    for (const ModelSpec &spec : candidates) {
        if (isUsable(spec.id))
            return spec.id;
    }
    return candidates.isEmpty() ? QString() : candidates.first().id;
}

const ModelSpec *ModelManager::selectedModel(ModelTask task) const
{
    const QString id = selectedModelId(task);
    return id.isEmpty() ? nullptr : m_registry.model(id);
}

void ModelManager::setSelectedModelId(ModelTask task, const QString &modelId)
{
    if (modelId == selectedModelId(task))
        return;

    QSettings settings;
    if (modelId.isEmpty())
        settings.remove(settingsKeyForSelection(task));
    else
        settings.setValue(settingsKeyForSelection(task), modelId);

    emit selectionChanged(task, modelId);
}

bool ModelManager::isDownloading(const QString &modelId) const
{
    return m_downloads.contains(modelId);
}

ModelDownload *ModelManager::startDownload(const QString &modelId, QString *error)
{
    if (ModelDownload *existing = m_downloads.value(modelId))
        return existing;

    const ModelSpec *spec = m_registry.model(modelId);
    if (!spec) {
        if (error)
            *error = tr("No model is registered under the id \"%1\".").arg(modelId);
        return nullptr;
    }
    if (!spec->isDownloadable()) {
        if (error)
            *error = tr("%1 has no download URL. Use \"Locate file\" to point at a copy "
                        "you already have.")
                         .arg(spec->displayName);
        return nullptr;
    }

    const QString destination = QDir(downloadDirectory()).filePath(spec->fileName);
    auto *download = new ModelDownload(*spec, destination, m_network, this);
    m_downloads.insert(modelId, download);

    connect(download, &ModelDownload::finished, this,
            [this, modelId, download](bool ok, const QString &) {
                m_downloads.remove(modelId);
                if (ok)
                    emit availabilityChanged(modelId);
                download->deleteLater();
            });

    download->start();
    return download;
}
