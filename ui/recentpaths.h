#ifndef RECENTPATHS_H
#define RECENTPATHS_H

#include <QString>

// Remembers the folder each file dialog was last pointed at, so the next one
// opens where the user left off instead of at the home directory.
//
// Every dialog family gets its own key — locating a model file should not send
// the video picker off to the models folder — but a key that has never been
// used falls back to the most recent folder from any of them. The result is
// that the very first dialog of a session starts wherever the last one ended,
// and after that each family tracks its own place.
//
// Values live in QSettings, so they survive across runs.
namespace RecentPaths {

inline constexpr char Images[]      = "images";
inline constexpr char Videos[]      = "videos";
inline constexpr char Annotations[] = "annotations";
inline constexpr char Projects[]    = "projects";
inline constexpr char Datasets[]    = "datasets";
inline constexpr char Exports[]     = "exports";
inline constexpr char Models[]      = "models";

// Directory a dialog for `key` should open at, once the user has used that
// dialog before.
//
// Without a `fallback` the answer is the last folder used anywhere, then the
// home directory. Passing a `fallback` means the caller has a better default
// than a guess from another category — model files live in the download
// folder, not wherever the last video came from — so it is used instead of
// that cross-category guess.
QString dir(const char *key, const QString &fallback = QString());

// Records where `path` lives. Accepts whatever the dialog returned: a file for
// the open/save dialogs, a directory for the folder pickers.
void remember(const char *key, const QString &path);

} // namespace RecentPaths

#endif // RECENTPATHS_H
