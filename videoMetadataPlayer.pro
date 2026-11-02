TEMPLATE = app
TARGET = videoMetadataPlayer
QT += quick concurrent
CONFIG += c++11 link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0

HEADERS += \
    videoPlayer.hpp \
    frameView.hpp \
    barcodeReader.hpp \
    metadataTable.hpp \
    metadataStore.hpp

SOURCES += \
    main.cpp \
    videoPlayer.cpp \
    frameView.cpp \
    barcodeReader.cpp \
    metadataStore.cpp

RESOURCES += qml.qrc
