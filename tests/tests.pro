#-------------------------------------------------------------------------------
# 解析核心单元测试工程（Qt Test）
#
#   mkdir ..\build-tests\core && cd ..\build-tests\core
#   qmake ..\..\..\tests\tests.pro -spec win32-g++ "CONFIG+=release"
#   mingw32-make -j8
#   ..\tests\xw_tests.exe
#
#   可用环境变量 XW_SAMPLE 指定测试数据文件路径，默认自动查找 sample 目录。
#   注：源文件一律使用 $$PWD 绝对路径，避免影子构建时 qmake 生成的相对依赖出错。
#-------------------------------------------------------------------------------

QT += core gui testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = xw_tests

SRC = $$PWD/../src

INCLUDEPATH += $$SRC \
               $$SRC/core

CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/bin/debug
} else {
    DESTDIR = $$OUT_PWD/bin/release
}

SOURCES += \
    $$PWD/test_parser.cpp \
    $$SRC/core/protocol.cpp \
    $$SRC/core/framedata.cpp \
    $$SRC/core/hexfilereader.cpp \
    $$SRC/core/frameparser.cpp \
    $$SRC/core/datamanager.cpp \
    $$SRC/core/appsettings.cpp

HEADERS += \
    $$SRC/core/protocol.h \
    $$SRC/core/framedata.h \
    $$SRC/core/hexfilereader.h \
    $$SRC/core/frameparser.h \
    $$SRC/core/datamanager.h \
    $$SRC/core/appsettings.h
