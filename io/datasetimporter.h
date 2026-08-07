#ifndef DATASETIMPORTER_H
#define DATASETIMPORTER_H

#include "datasetformat.h"

class Project;

// Bridges a format-agnostic ImportedDataset onto a live Project: reconciles the
// imported label ids with the project's own schema, and matches the dataset's
// image names to frame indices.
namespace DatasetImporter {

// Attempts to match against the currently open source first; if nothing lines up
// (or nothing is open), opens the dataset's own image folder. Merges the imported
// classes into the project schema by name, remapping label ids as it goes.
//
// With groupTracks set, annotations sharing a track id are folded into one track
// per object instead of landing as unrelated per-frame shapes.
IoReport apply(Project &project, const ImportedDataset &dataset, bool replaceExisting,
               bool groupTracks = true);

} // namespace DatasetImporter

#endif // DATASETIMPORTER_H
