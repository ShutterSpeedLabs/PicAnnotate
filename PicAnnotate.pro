QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    picannotate.cpp \
    ai/autoannotator.cpp \
    ai/clipclassifier.cpp \
    ai/clipcontroller.cpp \
    ai/cliptokenizer.cpp \
    ai/imageblob.cpp \
    ai/inferencetypes.cpp \
    ai/modeldownload.cpp \
    ai/modelmanager.cpp \
    ai/modelregistry.cpp \
    ai/modelspec.cpp \
    ai/onnxsession.cpp \
    ai/predictioncontroller.cpp \
    ai/predictionstore.cpp \
    ai/samcontroller.cpp \
    ai/samsegmenter.cpp \
    ai/tensor.cpp \
    ai/yolodetector.cpp \
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
    io/datasetbuilder.cpp \
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
    ui/datasetwizard.cpp \
    ui/exportdatasetdialog.cpp \
    ui/framenavigator.cpp \
    ui/importdatasetdialog.cpp \
    ui/labelpanel.cpp \
    ui/modelmanagerdialog.cpp \
    ui/multipointshapeitem.cpp \
    ui/predictionpanel.cpp \
    ui/recentpaths.cpp \
    ui/rectshapeitem.cpp \
    ui/trackpanel.cpp \
    ui/vertexhandle.cpp

HEADERS += \
    picannotate.h \
    ai/autoannotator.h \
    ai/clipclassifier.h \
    ai/clipcontroller.h \
    ai/cliptokenizer.h \
    ai/imageblob.h \
    ai/inferencetypes.h \
    ai/modeldownload.h \
    ai/modelmanager.h \
    ai/modelregistry.h \
    ai/modelspec.h \
    ai/onnxsession.h \
    ai/prediction.h \
    ai/predictioncontroller.h \
    ai/predictionstore.h \
    ai/samcontroller.h \
    ai/samsegmenter.h \
    ai/tensor.h \
    ai/yolodetector.h \
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
    io/datasetbuilder.h \
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
    ui/datasetwizard.h \
    ui/exportdatasetdialog.h \
    ui/framenavigator.h \
    ui/importdatasetdialog.h \
    ui/labelpanel.h \
    ui/modelmanagerdialog.h \
    ui/multipointshapeitem.h \
    ui/predictionpanel.h \
    ui/recentpaths.h \
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

# ---------------------------------------------------------------------------
# ONNX Runtime — optional.
#
# SAM and CLIP need it: their graphs use dynamic shapes and ops (GridSample,
# ScatterND) that OpenCV's DNN module does not implement. YOLO does not, and
# keeps running through cv::dnn either way.
#
# The dependency is optional on purpose. Without it the project still builds and
# every ORT-backed feature reports why it is unavailable instead of vanishing —
# a fresh clone compiles before anyone has downloaded a 30MB SDK.
#
# Point it at an unpacked onnxruntime-win-x64-<version> release, either by
# setting an ONNXRUNTIME_DIR environment variable or by passing it to qmake:
#     qmake ONNXRUNTIME_DIR=C:/onnxruntime
# See docs/AI_SETUP.md.
# ---------------------------------------------------------------------------
isEmpty(ONNXRUNTIME_DIR): ONNXRUNTIME_DIR = $$(ONNXRUNTIME_DIR)
isEmpty(ONNXRUNTIME_DIR): ONNXRUNTIME_DIR = C:/onnxruntime

exists($$ONNXRUNTIME_DIR/include/onnxruntime_cxx_api.h) {
    message("ONNX Runtime found at $$ONNXRUNTIME_DIR — SAM and CLIP enabled.")
    DEFINES += HAVE_ONNXRUNTIME
    INCLUDEPATH += $$ONNXRUNTIME_DIR/include
    LIBS += -L$$ONNXRUNTIME_DIR/lib -lonnxruntime

    # Both providers ship in separate ORT builds. Detect rather than assume:
    # asking for DirectML against a CPU-only SDK is a link error, not a warning.
    exists($$ONNXRUNTIME_DIR/include/dml_provider_factory.h) {
        message("  DirectML provider available.")
        DEFINES += HAVE_ORT_DIRECTML
    }
    exists($$ONNXRUNTIME_DIR/lib/onnxruntime_providers_cuda.lib) {
        message("  CUDA provider available.")
        DEFINES += HAVE_ORT_CUDA
    }
} else {
    message("ONNX Runtime not found at $$ONNXRUNTIME_DIR — building without SAM/CLIP.")
    message("  See docs/AI_SETUP.md to enable them.")
}

# ---------------------------------------------------------------------------
# Runtime files that have to sit next to the executable.
#
# The exe lands in a debug/release subfolder under OUT_PWD, so target that
# directly. Each command needs its own line: qmake joins QMAKE_POST_LINK with
# spaces, so without the separator the second copy becomes arguments to the first.
# ---------------------------------------------------------------------------
CONFIG(debug, debug|release) {
    APP_DEST_DIR = $$OUT_PWD/debug
} else {
    APP_DEST_DIR = $$OUT_PWD/release
}
MODELS_DEST_DIR = $$APP_DEST_DIR/models

# The NanoTrack pair the tracker needs, plus anything else dropped in models/.
QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$shell_path($$PWD/models) $$shell_path($$MODELS_DEST_DIR) $$escape_expand(\\n\\t)

win32:contains(DEFINES, HAVE_ONNXRUNTIME) {
    ORT_RUNTIME_DLL = $$ONNXRUNTIME_DIR/lib/onnxruntime.dll
    exists($$ORT_RUNTIME_DLL) {
        QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$ORT_RUNTIME_DLL) $$shell_path($$APP_DEST_DIR) $$escape_expand(\\n\\t)
    }
    # DirectML builds need DirectML.dll alongside; loading fails at startup without it.
    ORT_DML_DLL = $$ONNXRUNTIME_DIR/lib/DirectML.dll
    exists($$ORT_DML_DLL) {
        QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$ORT_DML_DLL) $$shell_path($$APP_DEST_DIR) $$escape_expand(\\n\\t)
    }
}

