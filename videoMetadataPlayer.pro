TEMPLATE = app
TARGET = videoMetadataPlayer
QT += quick concurrent
CONFIG += c++11 link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

# The sources are grouped by module. Every module keeps its own folder so
# the tree stays readable, and the include path makes the headers of the
# other modules reachable.
INCLUDEPATH += \
    $$PWD/src \
    $$PWD/src/player \
    $$PWD/src/index \
    $$PWD/src/metadata \
    $$PWD/src/cutter \
    $$PWD/src/views

HEADERS += \
    src/player/videoPlayer.hpp \
    src/player/frameView.hpp \
    src/index/nalParser.hpp \
    src/index/keyframeIndex.hpp \
    src/metadata/barcodeReader.hpp \
    src/metadata/metadataTable.hpp \
    src/metadata/metadataStore.hpp \
    src/cutter/videoCutter.hpp \
    src/cutter/cutController.hpp \
    src/views/panTiltGraph.hpp

SOURCES += \
    src/main.cpp \
    src/player/videoPlayer.cpp \
    src/player/frameView.cpp \
    src/index/nalParser.cpp \
    src/index/keyframeIndex.cpp \
    src/metadata/barcodeReader.cpp \
    src/metadata/metadataStore.cpp \
    src/cutter/videoCutter.cpp \
    src/cutter/cutController.cpp \
    src/views/panTiltGraph.cpp

RESOURCES += qml.qrc
