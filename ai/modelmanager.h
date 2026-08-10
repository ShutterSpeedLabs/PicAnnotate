#ifndef MODELMANAGER_H
#define MODELMANAGER_H

#include "modelregistry.h"
#include "modelspec.h"

#include <QObject>
#include <QString>
#include <QStringList>

class ModelDownload;
class QNetworkAccessManager;

// Everything about model *files*: where they are, whether they are there yet,
// how to fetch one, and which of them the user has picked for each job.
//
// Application-wide because the choice of detector is a property of the session
// rather than of one dialog — the canvas, the batch runner and the ingest wizard
// all have to agree on which model is in play.
class ModelManager : public QObject
{
    Q_OBJECT
public:
    static ModelManager &instance();

    ModelRegistry &registry() { return m_registry; }
    const ModelRegistry &registry() const { return m_registry; }

    // ---- locating files -----------------------------------------------------

    // Absolute path of a model's file, or an empty string when it is not on disk
    // anywhere the manager looks.
    QString localPath(const QString &modelId) const;
    bool isAvailable(const QString &modelId) const;

    // Directories searched, in priority order, for the Model Manager to show.
    QStringList searchPaths() const;

    // Where downloads land: a writable per-user location, so the app does not
    // need to be installed somewhere writable to fetch a model.
    static QString downloadDirectory();

    // Pins a model to a file the user chose by hand. Takes priority over both
    // search directories, which is how a custom re-export shadows a stock model.
    void setUserPath(const QString &modelId, const QString &absolutePath);
    QString userPath(const QString &modelId) const;
    void clearUserPath(const QString &modelId);

    // ---- what this build can actually run -----------------------------------

    // False when the app was built without ONNX Runtime, in which case models
    // needing it are listed but cannot be loaded. The UI uses this to explain
    // why rather than failing at load time with something cryptic.
    static bool onnxRuntimeAvailable();
    static QString onnxRuntimeVersion();

    // A model is usable when its file is present *and* its runtime is in this
    // build. Both halves are reported separately so the reason can be shown.
    bool isUsable(const QString &modelId, QString *reason = nullptr) const;

    ExecutionProvider preferredProvider() const;
    void setPreferredProvider(ExecutionProvider provider);

    // ---- the model chosen for each job --------------------------------------

    // Id of the model selected for a task, falling back to the first usable
    // catalogue entry for that task when nothing has been chosen yet.
    QString selectedModelId(ModelTask task) const;
    const ModelSpec *selectedModel(ModelTask task) const;
    void setSelectedModelId(ModelTask task, const QString &modelId);

    // ---- downloading --------------------------------------------------------

    // Starts fetching a model into downloadDirectory(). The returned object is
    // owned by the manager and deletes itself once finished; nothing if the
    // model is unknown or has no download URL (see `error`).
    ModelDownload *startDownload(const QString &modelId, QString *error);
    bool isDownloading(const QString &modelId) const;

signals:
    // A model's file appeared, moved or was removed.
    void availabilityChanged(const QString &modelId);

    // The model chosen for a task changed; engines drop their loaded session.
    void selectionChanged(ModelTask task, const QString &modelId);

    void preferredProviderChanged(ExecutionProvider provider);

private:
    ModelManager();

    QString settingsKeyForPath(const QString &modelId) const;
    QString settingsKeyForSelection(ModelTask task) const;

    ModelRegistry m_registry;
    QNetworkAccessManager *m_network = nullptr;
    QHash<QString, ModelDownload *> m_downloads;
};

#endif // MODELMANAGER_H
