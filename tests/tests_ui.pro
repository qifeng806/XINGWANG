#-------------------------------------------------------------------------------
# 界面集成测试工程（Qt Test + Qt Widgets）
#
#   mkdir ..\build-tests\ui && cd ..\build-tests\ui
#   qmake ..\..\..\tests\tests_ui.pro -spec win32-g++ "CONFIG+=release"
#   mingw32-make -j8
#   ..\bin\xw_ui_tests.exe -platform offscreen     (无显示环境)
#   ..\bin\xw_ui_tests.exe                         (带显示环境)
#
#   注：本工程直接编译 qcustomplot.cpp，必须带 -Wa,-mbig-obj（见下）。
#-------------------------------------------------------------------------------

QT += core gui widgets printsupport testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = xw_ui_tests

SRC = $$PWD/../src
QCP = $$PWD/../thirdparty/qcustomplot

INCLUDEPATH += $$SRC \
               $$SRC/core \
               $$SRC/ui \
               $$QCP

# qcustomplot.cpp 为超大单文件，MinGW 需要 big-obj 才能编译（同主工程）
QMAKE_CXXFLAGS += -Wa,-mbig-obj

CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/bin/debug
} else {
    DESTDIR = $$OUT_PWD/bin/release
}

SOURCES += \
    $$PWD/test_ui.cpp \
    $$SRC/core/protocol.cpp \
    $$SRC/core/framedata.cpp \
    $$SRC/core/hexfilereader.cpp \
    $$SRC/core/frameparser.cpp \
    $$SRC/core/datamanager.cpp \
    $$SRC/core/appsettings.cpp \
    $$SRC/ui/plotwidget.cpp \
    $$SRC/ui/curvepanel.cpp \
    $$SRC/ui/flowlayout.cpp \
    $$SRC/ui/configdialog.cpp \
    $$SRC/ui/mainwindow.cpp \
    $$QCP/qcustomplot.cpp

HEADERS += \
    $$SRC/core/protocol.h \
    $$SRC/core/framedata.h \
    $$SRC/core/hexfilereader.h \
    $$SRC/core/frameparser.h \
    $$SRC/core/datamanager.h \
    $$SRC/core/appsettings.h \
    $$SRC/ui/plotwidget.h \
    $$SRC/ui/curvepanel.h \
    $$SRC/ui/flowlayout.h \
    $$SRC/ui/configdialog.h \
    $$SRC/ui/mainwindow.h \
    $$QCP/qcustomplot.h
