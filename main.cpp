#include "picannotate.h"
#include "ui/theme.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QtGlobal>

#include <opencv2/core/utils/logger.hpp>

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
    // OpenCV logs at INFO by default here, so every plugin probe and every ONNX
    // node it parses lands in the console and buries the application's own
    // output. Warnings and above are the level worth reading. OPENCV_LOG_LEVEL
    // is OpenCV's own override, so anyone who set it deliberately keeps it.
    if (qEnvironmentVariableIsEmpty("OPENCV_LOG_LEVEL"))
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

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
