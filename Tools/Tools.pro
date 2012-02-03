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
    SUBDIRS += MiniBrowser/qt/MiniBrowser.pro
    linux-g++*: SUBDIRS += WebKitTestRunner/WebKitTestRunner.pro
}

!win32:contains(DEFINES, ENABLE_NETSCAPE_PLUGIN_API=1) {
equals(BUILD_TOOLS, 1) {
    SUBDIRS += DumpRenderTree/qt/TestNetscapePlugin/TestNetscapePlugin.pro
}
}
