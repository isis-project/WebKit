# -------------------------------------------------------------------
# Root project file for tools
#
# See 'Tools/qmake/README' for an overview of the build system
# -------------------------------------------------------------------

TEMPLATE = subdirs
CONFIG += ordered

load(features)

BUILD_TOOLS=1
contains(DEFINES, PALM_DEVICE):!contains(DEFINES, MACHINE_DESKTOP) {
    BUILD_TOOLS=0
}

equals(BUILD_TOOLS, 1) {
    exists($$PWD/QtTestBrowser/QtTestBrowser.pro): SUBDIRS += QtTestBrowser/QtTestBrowser.pro
    exists($$PWD/DumpRenderTree/qt/DumpRenderTree.pro): SUBDIRS += DumpRenderTree/qt/DumpRenderTree.pro
    exists($$PWD/DumpRenderTree/qt/ImageDiff.pro): SUBDIRS += DumpRenderTree/qt/ImageDiff.pro
}

!no_webkit2 {
    SUBDIRS += MiniBrowser/qt/MiniBrowser.pro \
               WebKitTestRunner/WebKitTestRunner.pro
}

!win32:contains(DEFINES, ENABLE_NETSCAPE_PLUGIN_API=1) {
equals(BUILD_TOOLS, 1) {
    SUBDIRS += DumpRenderTree/qt/TestNetscapePlugin/TestNetscapePlugin.pro
}
}

OTHER_FILES = \
    Scripts/* \
    qmake/README \
    qmake/configure.pro \
    qmake/sync.profile \
    qmake/config.tests/README \
    qmake/config.tests/fontconfig/* \
    qmake/mkspecs/modules/* \
    qmake/mkspecs/features/*.prf \
    qmake/mkspecs/features/mac/*.prf \
    qmake/mkspecs/features/unix/*.prf \
    qmake/mkspecs/features/win32/*.prf
