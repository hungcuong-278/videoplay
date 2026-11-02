TEMPLATE = app
TARGET = countIndexed
QT += core gui concurrent
CONFIG += c++11 link_pkgconfig console
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

VMP = ../../src

INCLUDEPATH += \
    $$VMP \
    $$VMP/player \
    $$VMP/index \
    $$VMP/metadata

HEADERS += \
    $$VMP/index/nalParser.hpp \
    $$VMP/index/keyframeIndex.hpp \
    $$VMP/metadata/barcodeReader.hpp \
    $$VMP/player/videoPlayer.hpp

SOURCES += \
    countIndexed.cpp \
    $$VMP/index/nalParser.cpp \
    $$VMP/index/keyframeIndex.cpp \
    $$VMP/player/videoPlayer.cpp \
    $$VMP/metadata/barcodeReader.cpp
