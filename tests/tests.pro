QT += core gui qml quick quickcontrols2 concurrent network testlib
CONFIG += c++17 testcase
TARGET = omalift_tests
TEMPLATE = app

INCLUDEPATH += ../src

isEmpty(ONNXRUNTIME_PREFIX): ONNXRUNTIME_PREFIX = /usr
INCLUDEPATH += $$ONNXRUNTIME_PREFIX/include/onnxruntime
LIBS += -L$$ONNXRUNTIME_PREFIX/lib -lonnxruntime

HEADERS += \
    ../src/key.h \
    ../src/matte.h \
    ../src/download.h \
    ../src/liftprovider.h \
    ../src/backend.h

SOURCES += \
    tests.cpp \
    ../src/key.cpp \
    ../src/matte.cpp \
    ../src/download.cpp \
    ../src/liftprovider.cpp \
    ../src/backend.cpp

RESOURCES += ../src/resources.qrc
