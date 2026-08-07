#include "formatregistry.h"
#include "cocoformat.h"
#include "yoloformat.h"

#include <memory>

namespace {

const QVector<std::shared_ptr<IDatasetFormat>> &registeredFormats()
{
    static const QVector<std::shared_ptr<IDatasetFormat>> formats = {
        std::make_shared<CocoFormat>(),
        std::make_shared<YoloFormat>(),
    };
    return formats;
}

} // namespace

namespace FormatRegistry {

QVector<const IDatasetFormat *> allFormats()
{
    QVector<const IDatasetFormat *> result;
    for (const auto &format : registeredFormats())
        result.append(format.get());
    return result;
}

QVector<const IDatasetFormat *> exportFormats()
{
    QVector<const IDatasetFormat *> result;
    for (const auto &format : registeredFormats()) {
        if (format->supportsExport())
            result.append(format.get());
    }
    return result;
}

QVector<const IDatasetFormat *> importFormats()
{
    QVector<const IDatasetFormat *> result;
    for (const auto &format : registeredFormats()) {
        if (format->supportsImport())
            result.append(format.get());
    }
    return result;
}

const IDatasetFormat *formatById(const QString &id)
{
    for (const auto &format : registeredFormats()) {
        if (format->id() == id)
            return format.get();
    }
    return nullptr;
}

} // namespace FormatRegistry
