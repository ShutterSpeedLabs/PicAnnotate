QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    datasetfn.cpp \
    globalVar.cpp \
    main.cpp \
    picannotate.cpp \
    yaml_cpp.cpp

HEADERS += \
    datasetfn.h \
    globalVar.h \
    picannotate.h \
    yaml_cpp.h

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

INCLUDEPATH += /usr/local/include/opencv4
LIBS += -L/usr/local/lib -lopencv_core -lopencv_imgproc -lopencv_highgui -lyaml-cpp
