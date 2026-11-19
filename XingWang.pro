#-------------------------------------------------------------------------------
# 星网转台数据解析与可视化软件
#   Qt 6.11.0 + QMake + QCustomPlot 2.1.1 (C++17, MinGW 64bit)
#
#   构建：
#     mkdir build && cd build
#     qmake ..\XingWang.pro -spec win32-g++ "CONFIG+=release"
#     mingw32-make -j8
#-------------------------------------------------------------------------------

QT += core gui widgets printsupport
CONFIG += c++17
CONFIG -= console
TEMPLATE = app
TARGET = XingWangTurntable

DEFINES += APP_VERSION=\\\"1.0.0\\\"

INCLUDEPATH += $$PWD/src \
               $$PWD/src/core \
               $$PWD/src/ui \
               $$PWD/thirdparty/qcustomplot

#-------------------------------------------------------------------------------
# MinGW 编译兼容设置（必需）
#   qcustomplot.cpp 是 ~130 万字符的 amalgamation 单文件，Debug 模式下会生成
#   3 万+ 个节区（.text/.debug_*），触发 GNU as 报错：
#       as.exe: qcustomplot.o: too many sections (32997)
#       Fatal error: can't write ... to section .text of qcustomplot.o: 'file too big'
#   必须启用 big-obj（COFF 大对象格式）才能编译通过。
#-------------------------------------------------------------------------------
QMAKE_CXXFLAGS += -Wa,-mbig-obj

# Debug 下同时关闭变量跟踪，显著降低 cc1plus 的内存占用（避免 out of memory）
CONFIG(debug, debug|release) {
    QMAKE_CXXFLAGS_DEBUG += -fno-var-tracking -fno-var-tracking-assignments
}

#-------------------------------------------------------------------------------
# 输出目录
#   ⚠ 必须使用 $$OUT_PWD（当前构建目录）而不是 $$PWD（源码目录）：
#     用绝对路径指定 OBJECTS_DIR/DESTDIR 会让所有影子构建目录共用同一批目标文件，
#     一旦某个构建目录被清理（Qt Creator「清理项目」/命令行 rm obj），另一个构建
#     目录就会链接失败：ld: cannot find obj/release/xxx.o
#   现在：每个构建目录各自拥有 bin/release 与 bin/debug 产物与目标文件。
#-------------------------------------------------------------------------------
CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/bin/debug
} else {
    DESTDIR = $$OUT_PWD/bin/release
}

SOURCES += \
    src/main.cpp \
    src/core/protocol.cpp \
    src/core/framedata.cpp \
    src/core/hexfilereader.cpp \
    src/core/frameparser.cpp \
    src/core/datamanager.cpp \
    src/core/appsettings.cpp \
    src/ui/plotwidget.cpp \
    src/ui/curvepanel.cpp \
    src/ui/flowlayout.cpp \
    src/ui/configdialog.cpp \
    src/ui/mainwindow.cpp \
    thirdparty/qcustomplot/qcustomplot.cpp

HEADERS += \
    src/core/protocol.h \
    src/core/framedata.h \
    src/core/hexfilereader.h \
    src/core/frameparser.h \
    src/core/datamanager.h \
    src/core/appsettings.h \
    src/ui/plotwidget.h \
    src/ui/curvepanel.h \
    src/ui/flowlayout.h \
    src/ui/configdialog.h \
    src/ui/mainwindow.h \
    thirdparty/qcustomplot/qcustomplot.h
