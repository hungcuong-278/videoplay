TEMPLATE = app
TARGET = keyframeIndexTool
QT += core gui concurrent
CONFIG += c++11 link_pkgconfig console
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

VMP = ../../src

INCLUDEPATH += \
    $$VMP \
    $$VMP/index

HEADERS += \
    $$VMP/index/nalParser.hpp \
    $$VMP/index/keyframeIndex.hpp

SOURCES += \
    keyframeIndexTool.cpp \
    $$VMP/index/nalParser.cpp \
    $$VMP/index/keyframeIndex.cpp
