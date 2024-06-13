#include "picannotate.h"
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
