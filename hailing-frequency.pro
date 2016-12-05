QT += core network
QT -= gui

CONFIG += c++11
QMAKE_LFLAGS += -static

TARGET = hailing-frequency
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    relay.cpp

HEADERS += \
    relay.h

DISTFILES += \
    LICENSE \
    README.md \
    TODO.md \
    index.html

html_files.files += index.html
html_files.path = $$OUT_PWD

INSTALLS += html_files

INCLUDEPATH += "../getoptpp"
SOURCES += "../getoptpp/src/getopt_pp.cpp"
