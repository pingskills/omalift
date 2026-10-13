QT += core gui qml quick quickcontrols2 concurrent

CONFIG += c++17 release
TARGET = omalift
TEMPLATE = app

HEADERS += \
    src/key.h \
    src/liftprovider.h \
    src/backend.h

SOURCES += \
    src/main.cpp \
    src/key.cpp \
    src/liftprovider.cpp \
    src/backend.cpp

RESOURCES += src/resources.qrc
