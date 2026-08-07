#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>
#include <QWidget>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

namespace Theme {

void apply(QApplication &app)
{
    // Fusion first: the native Windows style ignores much of a stylesheet and
    // mixes its own colours in, which is what makes themed Qt apps look
    // half-painted. Fusion honours both the palette and the stylesheet.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, Color::base());
    palette.setColor(QPalette::WindowText, Color::text());
    palette.setColor(QPalette::Base, QColor(0x14, 0x17, 0x1C));
    palette.setColor(QPalette::AlternateBase, Color::panel());
    palette.setColor(QPalette::Text, Color::text());
    palette.setColor(QPalette::Button, Color::raised());
    palette.setColor(QPalette::ButtonText, Color::text());
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, Color::accent());
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, Color::raised());
    palette.setColor(QPalette::ToolTipText, Color::text());
    palette.setColor(QPalette::PlaceholderText, Color::muted());
    palette.setColor(QPalette::Link, Color::accent());
    palette.setColor(QPalette::Mid, Color::border());
    palette.setColor(QPalette::Dark, QColor(0x10, 0x12, 0x16));

    // Disabled states need explicit colours or Fusion derives muddy ones.
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5B, 0x62, 0x70));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5B, 0x62, 0x70));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5B, 0x62, 0x70));
    palette.setColor(QPalette::Disabled, QPalette::Base, QColor(0x1A, 0x1D, 0x23));

    app.setPalette(palette);

    QFile styleFile(QStringLiteral(":/theme/theme.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}

void applyWindowChrome(QWidget *window)
{
#ifdef Q_OS_WIN
    if (!window)
        return;

    // Forces creation of the native handle the DWM call needs.
    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (!handle)
        return;

    // Windows 11 (build 22000+) only. On older builds these attributes are
    // rejected and the title bar simply keeps its default look.
    const COLORREF caption = RGB(Color::base().red(), Color::base().green(), Color::base().blue());
    const COLORREF captionText = RGB(Color::text().red(), Color::text().green(), Color::text().blue());
    const COLORREF frameBorder = RGB(Color::border().red(), Color::border().green(), Color::border().blue());

    DwmSetWindowAttribute(handle, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(handle, 36 /* DWMWA_TEXT_COLOR */, &captionText, sizeof(captionText));
    DwmSetWindowAttribute(handle, 34 /* DWMWA_BORDER_COLOR */, &frameBorder, sizeof(frameBorder));

    const BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(handle, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                          &useDarkMode, sizeof(useDarkMode));
#else
    Q_UNUSED(window);
#endif
}

} // namespace Theme
