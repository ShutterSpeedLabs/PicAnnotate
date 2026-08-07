#ifndef IMAGEFOLDERSOURCE_H
#define IMAGEFOLDERSOURCE_H

#include "framesource.h"

#include <QDir>
#include <QHash>
#include <QStringList>

class ImageFolderSource : public FrameSource
{
public:
    bool openFolder(const QString &path, QString *error);
    bool openSingleImage(const QString &filePath, QString *error);

    // Opens an explicit, ordered file list living under `path`. Dataset imports
    // use this so frame order matches the order the dataset declared.
    bool openFileList(const QString &path, const QStringList &fileNames, QString *error);

    static QStringList supportedNameFilters();

    int frameCount() const override;
    QImage frameAt(int index) const override;
    QString frameLabel(int index) const override;
    QSize frameSize(int index) const override;
    QString framePath(int index) const override;

private:
    QDir m_directory;
    QStringList m_fileList;
    mutable QHash<int, QSize> m_sizeCache;
};

#endif // IMAGEFOLDERSOURCE_H
