QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    picannotate.cpp \
    core/annoshape.cpp \
    core/annotationcommands.cpp \
    core/annotationcontroller.cpp \
    core/frameannotations.cpp \
    core/imagefoldersource.cpp \
    core/keypointtemplate.cpp \
    core/labelclass.cpp \
    core/labelschema.cpp \
    core/project.cpp \
    core/skeletontemplate.cpp \
    core/tracktimeline.cpp \
    core/videoframesource.cpp \
    io/cocoformat.cpp \
    io/datasetformat.cpp \
    io/datasetimporter.cpp \
    io/formatregistry.cpp \
    io/frameexporter.cpp \
    io/iogeometry.cpp \
    io/yoloformat.cpp \
    trackers/cvimageconvert.cpp \
    trackers/objecttracker.cpp \
    trackers/trackcontroller.cpp \
    ui/annotationgraphicsview.cpp \
    ui/theme.cpp \
    ui/exportdatasetdialog.cpp \
    ui/framenavigator.cpp \
    ui/importdatasetdialog.cpp \
    ui/labelpanel.cpp \
    ui/multipointshapeitem.cpp \
    ui/rectshapeitem.cpp \
    ui/trackpanel.cpp \
    ui/vertexhandle.cpp

HEADERS += \
    picannotate.h \
    core/annoshape.h \
    core/annotationcommands.h \
    core/annotationcontroller.h \
    core/frameannotations.h \
    core/framesource.h \
    core/imagefoldersource.h \
    core/keypointtemplate.h \
    core/labelclass.h \
    core/labelschema.h \
    core/project.h \
    core/skeletontemplate.h \
    core/tracktimeline.h \
    core/videoframesource.h \
    io/cocoformat.h \
    io/datasetformat.h \
    io/datasetimporter.h \
    io/formatregistry.h \
    io/frameexporter.h \
    io/iogeometry.h \
    io/yoloformat.h \
    trackers/cvimageconvert.h \
    trackers/objecttracker.h \
    trackers/trackcontroller.h \
    ui/annotationgraphicsview.h \
    ui/theme.h \
    ui/exportdatasetdialog.h \
    ui/framenavigator.h \
    ui/importdatasetdialog.h \
    ui/labelpanel.h \
    ui/multipointshapeitem.h \
    ui/rectshapeitem.h \
    ui/trackpanel.h \
    ui/vertexhandle.h

RESOURCES += \
    resources.qrc

win32: LIBS += -ldwmapi

FORMS += \
    picannotate.ui

TRANSLATIONS += \
    PicAnnotate_en_IN.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

#INCLUDEPATH += /usr/local/include/opencv4
INCLUDEPATH += C:\OpenCV\build\include

#LIBS += -L/usr/local/lib -lopencv_core -lopencv_imgproc -lopencv_highgui
LIBS += -LC:\OpenCV\build\x64\vc16\lib
CONFIG(debug, debug|release) {
    LIBS += -lopencv_world490d
} else {
    LIBS += -lopencv_world490
}

# Copy the NanoTrack model files next to the built executable so
# TrackController can find them at <app_dir>/models/ at runtime.
# The exe lands in a debug/release subfolder under OUT_PWD, so target that directly.
CONFIG(debug, debug|release) {
    MODELS_DEST_DIR = $$OUT_PWD/debug/models
} else {
    MODELS_DEST_DIR = $$OUT_PWD/release/models
}
QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$shell_path($$PWD/models) $$shell_path($$MODELS_DEST_DIR)

