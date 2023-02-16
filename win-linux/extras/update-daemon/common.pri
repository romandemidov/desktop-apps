
QT      += core

TARGET  = update-daemon
CONFIG  += c++11 console
CONFIG  -= app_bundle
CONFIG  -= debug_and_release debug_and_release_target

TEMPLATE = app

CORE_ROOT_DIR = $$PWD/../../../../core

CONFIG += core_no_dst
include($$CORE_ROOT_DIR/Common/base.pri)

ADD_DEPENDENCY(kernel)

INCLUDEPATH += $$PWD/src

HEADERS += $$PWD/src/version.h \
           $$PWD/src/utils.h

SOURCES += $$PWD/src/main.cpp \
           $$PWD/src/utils.cpp


ENV_PRODUCT_VERSION = $$(PRODUCT_VERSION)
!isEmpty(ENV_PRODUCT_VERSION) {
    FULL_PRODUCT_VERSION = $${ENV_PRODUCT_VERSION}.$$(BUILD_NUMBER)
    DEFINES += VER_PRODUCT_VERSION=$$FULL_PRODUCT_VERSION \
               VER_PRODUCT_VERSION_COMMAS=$$replace(FULL_PRODUCT_VERSION, \., ",")
}

RC_FILE = $$PWD/version.rc

contains(QMAKE_TARGET.arch, x86_64):{
    QMAKE_LFLAGS_WINDOWS = /SUBSYSTEM:WINDOWS,5.02
} else {
    QMAKE_LFLAGS_WINDOWS = /SUBSYSTEM:WINDOWS,5.01
}

core_release:DESTDIR = $$DESTDIR/build
core_debug:DESTDIR = $$DESTDIR/build/debug

!isEmpty(OO_BUILD_BRANDING) {
    DESTDIR = $$DESTDIR/$$OO_BUILD_BRANDING
}

DESTDIR = $$DESTDIR/$$CORE_BUILDS_PLATFORM_PREFIX

build_xp {
    DESTDIR = $$DESTDIR/xp
    DEFINES += __OS_WIN_XP
}

LIBS += -luser32


OBJECTS_DIR = $$DESTDIR/obj
MOC_DIR = $$DESTDIR/moc
RCC_DIR = $$DESTDIR/rcc
