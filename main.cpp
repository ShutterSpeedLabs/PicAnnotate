#include "picannotate.h"
#include "ui/theme.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

/**
+ * Main function that initializes the application, sets up the translator for internationalization,
+ * creates the main application window, and starts the event loop.
+ *
+ * @param argc number of command line arguments
+ * @param argv array of command line arguments
+ *
+ * @return exit code indicating the termination status of the program
+ *
+ * @throws None
+ */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // QSettings keys off these, and the main window stores its panel layout there.
    QApplication::setOrganizationName("ShutterSpeedLabs");
    QApplication::setApplicationName("PicAnnotate");

    // Before any widget exists, so nothing is created with the default palette.
    Theme::apply(a);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "PicAnnotate_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    PicAnnotate w;
    w.show();
    return a.exec();
}
