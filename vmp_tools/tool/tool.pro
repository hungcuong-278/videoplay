QT += quick
CONFIG += c++11 link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
VMP = $$(HOME)/cuong_workspace/video_metadata_player
INCLUDEPATH += $$VMP
HEADERS += $$VMP/videoPlayer.hpp $$VMP/barcodeReader.hpp
SOURCES += lat.cpp $$VMP/videoPlayer.cpp $$VMP/barcodeReader.cpp
