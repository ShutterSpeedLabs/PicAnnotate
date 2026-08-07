#include "imagefoldersource.h"

#include <QFileInfo>
#include <QImageReader>

QStringList ImageFolderSource::supportedNameFilters()
{
    return {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tif", "*.tiff", "*.webp"};
}

bool ImageFolderSource::openFolder(const QString &path, QString *error)
{
    QDir dir(path);
    if (!dir.exists()) {
        if (error)
            *error = "Folder does not exist: " + path;
        return false;
    }

    dir.setNameFilters(supportedNameFilters());
    dir.setFilter(QDir::Files);

    const QStringList files = dir.entryList(QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        if (error)
            *error = "No supported image files found in: " + path;
        return false;
    }

    m_directory = dir;
    m_fileList = files;
    m_sizeCache.clear();
    return true;
}

bool ImageFolderSource::openSingleImage(const QString &filePath, QString *error)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        if (error)
            *error = "File does not exist: " + filePath;
        return false;
    }

    m_directory = info.absoluteDir();
    m_fileList = {info.fileName()};
    m_sizeCache.clear();
    return true;
}

bool ImageFolderSource::openFileList(const QString &path, const QStringList &fileNames, QString *error)
{
    QDir dir(path);
    if (!dir.exists()) {
        if (error)
            *error = "Image folder does not exist: " + path;
        return false;
    }

    QStringList present;
    present.reserve(fileNames.size());
    for (const QString &name : fileNames) {
        if (dir.exists(name))
            present.append(name);
    }

    if (present.isEmpty()) {
        if (error)
            *error = "None of the dataset's images were found in: " + path;
        return false;
    }

    m_directory = dir;
    m_fileList = present;
    m_sizeCache.clear();
    return true;
}

int ImageFolderSource::frameCount() const
{
    return m_fileList.size();
}

QImage ImageFolderSource::frameAt(int index) const
{
    if (index < 0 || index >= m_fileList.size())
        return QImage();
    return QImage(m_directory.absoluteFilePath(m_fileList.at(index)));
}

QString ImageFolderSource::frameLabel(int index) const
{
    if (index < 0 || index >= m_fileList.size())
        return QString();
    return m_fileList.at(index);
}

QSize ImageFolderSource::frameSize(int index) const
{
    if (index < 0 || index >= m_fileList.size())
        return QSize();

    const auto cached = m_sizeCache.constFind(index);
    if (cached != m_sizeCache.constEnd())
        return cached.value();

    // QImageReader reads only the header, so sizing a whole folder for export
    // does not decode a single pixel.
    QImageReader reader(m_directory.absoluteFilePath(m_fileList.at(index)));
    const QSize size = reader.size();
    m_sizeCache.insert(index, size);
    return size;
}

QString ImageFolderSource::framePath(int index) const
{
    if (index < 0 || index >= m_fileList.size())
        return QString();
    return m_directory.absoluteFilePath(m_fileList.at(index));
}
