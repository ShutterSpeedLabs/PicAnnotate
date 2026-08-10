#include "recentpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

const char kGroup[] = "RecentPaths";
// Written alongside every category so a dialog opened for the first time still
// lands near the user's current work.
const char kLastUsed[] = "lastUsed";

// A remembered folder can be gone by the next run — removable drive, a cleared
// scratch directory, a renamed project. Handing a stale path to QFileDialog
// makes it fall back to the process working directory, which is worse than the
// answer the caller asked for, so treat missing as unset.
QString existingDir(const QSettings &settings, const char *name)
{
    const QString path = settings.value(QLatin1String(name)).toString();
    return QFileInfo(path).isDir() ? path : QString();
}

} // namespace

QString RecentPaths::dir(const char *key, const QString &fallback)
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    QString path = existingDir(settings, key);
    if (path.isEmpty() && fallback.isEmpty())
        path = existingDir(settings, kLastUsed);
    settings.endGroup();

    if (!path.isEmpty())
        return path;
    return fallback.isEmpty() ? QDir::homePath() : fallback;
}

void RecentPaths::remember(const char *key, const QString &path)
{
    if (path.isEmpty())
        return;

    // Save dialogs name a file that does not exist yet, so decide by shape
    // rather than by asking the filesystem what `path` is.
    const QFileInfo info(path);
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (folder.isEmpty())
        return;

    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QLatin1String(key), folder);
    settings.setValue(QLatin1String(kLastUsed), folder);
    settings.endGroup();
}
