TEMPLATE = app
TARGET = countIndexed
QT += core gui concurrent
CONFIG += c++11 link_pkgconfig console
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

VMP = ..

INCLUDEPATH += $$VMP

HEADERS += \
    $$VMP/nalParser.hpp \
    $$VMP/keyframeIndex.hpp \
    $$VMP/barcodeReader.hpp \
    $$VMP/videoPlayer.hpp

SOURCES += \
    countIndexed.cpp \
    $$VMP/nalParser.cpp \
    $$VMP/keyframeIndex.cpp \
    $$VMP/videoPlayer.cpp \
    $$VMP/barcodeReader.cpp
