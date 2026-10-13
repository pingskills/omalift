QT += core gui qml quick quickcontrols2 concurrent network

CONFIG += c++17 release
TARGET = omalift
TEMPLATE = app

# ONNX Runtime runs the any-background models. ONNXRUNTIME_PREFIX points at a
# copy somewhere other than /usr, for building without installing it.
isEmpty(ONNXRUNTIME_PREFIX): ONNXRUNTIME_PREFIX = /usr
INCLUDEPATH += $$ONNXRUNTIME_PREFIX/include/onnxruntime
LIBS += -L$$ONNXRUNTIME_PREFIX/lib -lonnxruntime

HEADERS += \
    src/key.h \
    src/matte.h \
    src/download.h \
    src/liftprovider.h \
    src/backend.h

SOURCES += \
    src/main.cpp \
    src/key.cpp \
    src/matte.cpp \
    src/download.cpp \
    src/liftprovider.cpp \
    src/backend.cpp

RESOURCES += src/resources.qrc
