QT += quick
CONFIG += c++11 link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

VMP = ../../src

INCLUDEPATH += \
    $$VMP \
    $$VMP/player \
    $$VMP/index \
    $$VMP/metadata

HEADERS += \
    $$VMP/player/videoPlayer.hpp \
    $$VMP/metadata/barcodeReader.hpp

SOURCES += \
    lat.cpp \
    $$VMP/player/videoPlayer.cpp \
    $$VMP/metadata/barcodeReader.cpp
