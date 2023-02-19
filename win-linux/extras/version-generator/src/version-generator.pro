QT += core
QT -= gui

TARGET  = version-gen
CONFIG += c++11 console
CONFIG -= app_bundle
CONFIG  -= debug_and_release debug_and_release_target

TEMPLATE = app

SOURCES += \
        main.cpp

core_release:DESTDIR = $$DESTDIR/build
core_debug:DESTDIR = $$DESTDIR/build/debug

LIBS += -luser32

OBJECTS_DIR = $$DESTDIR/obj
MOC_DIR = $$DESTDIR/moc
RCC_DIR = $$DESTDIR/rcc
