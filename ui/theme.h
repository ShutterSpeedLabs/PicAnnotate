#ifndef THEME_H
#define THEME_H

#include <QColor>

class QApplication;
class QWidget;

// Application-wide look. Two things have to agree for a Qt app to look coherent:
// the palette (used by widgets and dialogs Qt draws itself) and the stylesheet
// (used for everything selector-based). Theme::apply() sets both from one source
// of truth.
namespace Theme {

// Chrome colours. Deliberately desaturated: annotation shapes are coloured by
// label class, so the UI must not compete with them.
namespace Color {
inline QColor canvas()  { return QColor(0x0E, 0x10, 0x14); }
inline QColor base()    { return QColor(0x16, 0x18, 0x1D); }
inline QColor panel()   { return QColor(0x1C, 0x1F, 0x26); }
inline QColor raised()  { return QColor(0x23, 0x27, 0x2F); }
inline QColor hover()   { return QColor(0x2C, 0x31, 0x3A); }
inline QColor border()  { return QColor(0x31, 0x37, 0x42); }
inline QColor text()    { return QColor(0xE4, 0xE7, 0xEC); }
inline QColor muted()   { return QColor(0x9A, 0xA3, 0xAF); }
inline QColor accent()  { return QColor(0x4F, 0x8C, 0xFF); }
} // namespace Color

void apply(QApplication &app);

// Tints the native window frame to match on Windows 11, so the title bar does
// not keep the system accent colour and clash with the theme. No-op elsewhere.
void applyWindowChrome(QWidget *window);

} // namespace Theme

#endif // THEME_H
