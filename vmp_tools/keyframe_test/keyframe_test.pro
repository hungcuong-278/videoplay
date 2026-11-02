TEMPLATE = app
TARGET = keyframeIndexTool
QT += core gui concurrent
CONFIG += c++11 link_pkgconfig console
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

VMP = ..

INCLUDEPATH += $$VMP

HEADERS += \
    $$VMP/nalParser.hpp \
    $$VMP/keyframeIndex.hpp

SOURCES += \
    keyframeIndexTool.cpp \
    $$VMP/nalParser.cpp \
    $$VMP/keyframeIndex.cpp
