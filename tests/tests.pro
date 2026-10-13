QT += core gui qml quick quickcontrols2 concurrent testlib
CONFIG += c++17 testcase
TARGET = omalift_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/key.h \
    ../src/liftprovider.h \
    ../src/backend.h

SOURCES += \
    tests.cpp \
    ../src/key.cpp \
    ../src/liftprovider.cpp \
    ../src/backend.cpp

RESOURCES += ../src/resources.qrc
