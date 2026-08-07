#ifndef FORMATREGISTRY_H
#define FORMATREGISTRY_H

#include "datasetformat.h"

#include <QVector>

// The single place new dataset formats get registered. Everything else — the
// import and export dialogs included — discovers formats through here, so adding
// Pascal VOC or Label Studio JSON later means one new entry and no UI changes.
namespace FormatRegistry {

QVector<const IDatasetFormat *> allFormats();
QVector<const IDatasetFormat *> exportFormats();
QVector<const IDatasetFormat *> importFormats();

const IDatasetFormat *formatById(const QString &id);

} // namespace FormatRegistry

#endif // FORMATREGISTRY_H
