import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Dialogs 1.3
import VideoTool 1.0

ApplicationWindow {
    id: root
    width: 1280
    height: 820
    visible: true
    color: "black"
    title: videoPlayer.source === "" ? "Video Metadata Player"
                                     : videoPlayer.source

    // Row of the CSV matching the frame on screen. Re-evaluated when the
    // frame changes or when a new table is loaded (revision).
    property var row: metadata.revision >= 0
                      ? metadata.rowInfo(videoPlayer.frameId) : ({})

    // Formats milliseconds as mm:ss.zzz
    function formatTime(ms) {
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        var milli = ms % 1000
        return (min < 10 ? "0" : "") + min + ":"
               + (sec < 10 ? "0" : "") + sec + "."
               + ("00" + milli).slice(-3)
    }

    function padId(id) {
        return ("000000" + id).slice(-7)
    }

    readonly property int jumpMs: 5000
    readonly property var rates: [0.25, 0.5, 1.0, 1.5, 2.0, 4.0]

    FileDialog {
        id: openDialog
        title: "Open video"
        selectMultiple: false
        nameFilters: ["Video files (*.mp4 *.mov *.m4v *.mkv *.webm *.ts "
                      + "*.m2ts *.mpg *.vob *.avi *.flv *.ogv *.h264 "
                      + "*.h265)", "All files (*)"]
        onAccepted: {
            videoPlayer.open(fileUrl)
            metadata.loadForVideo(fileUrl)
        }
    }

    FileDialog {
        id: csvDialog
        title: "Open metadata CSV"
        selectMultiple: false
        nameFilters: ["CSV files (*.csv)", "All files (*)"]
        onAccepted: metadata.load(fileUrl)
    }

    Shortcut {
        sequence: "Space"
        onActivated: videoPlayer.togglePlay()
    }

    Shortcut {
        sequence: "Left"
        onActivated: videoPlayer.seekRelative(-jumpMs)
    }

    Shortcut {
        sequence: "Right"
        onActivated: videoPlayer.seekRelative(jumpMs)
    }

    Shortcut {
        sequence: ","
        onActivated: videoPlayer.stepBackward()
    }

    Shortcut {
        sequence: "."
        onActivated: videoPlayer.stepForward()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The frame view shows the picture as a texture of the scene
        // graph and does not paint a background of its own, so the black
        // background is a rectangle behind it.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: "black"
            }

            FrameView {
                id: frameView
                anchors.fill: parent
                player: videoPlayer

                Label {
                    anchors.centerIn: parent
                    visible: videoPlayer.errorString !== ""
                             || videoPlayer.source === ""
                    color: videoPlayer.errorString !== "" ? "tomato" : "gray"
                    font.pixelSize: 20
                    text: videoPlayer.errorString !== ""
                          ? videoPlayer.errorString
                          : "Open a video to start"
                }
            }
        }

        Pane {
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                // Seek bar. While dragging, fast keyframe previews (one
                // decoded picture each, tens of ms, whatever the length of
                // the video); on release, an accurate seek to the exact
                // frame. Requests made while one runs are merged.
                Slider {
                    id: seekBar
                    Layout.fillWidth: true
                    focusPolicy: Qt.NoFocus
                    enabled: videoPlayer.seekable
                    from: 0
                    to: Math.max(1, videoPlayer.duration)
                    live: true
                    onMoved: videoPlayer.seek(value, false)
                    onPressedChanged: {
                        if (!pressed)
                            videoPlayer.seek(value, true)
                    }

                    Binding {
                        target: seekBar
                        property: "value"
                        value: Math.max(0, videoPlayer.frameTime)
                        when: !seekBar.pressed
                    }
                }

                // State of the keyframe index. The index is built in the
                // background and makes the accurate seeks of a file
                // without a keyframe table much faster.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: keyframes.scanning || keyframes.complete

                    ProgressBar {
                        id: indexBar
                        Layout.preferredWidth: 140
                        from: 0
                        to: 100
                        value: keyframes.progress
                        visible: keyframes.scanning
                    }

                    Label {
                        font.family: "monospace"
                        font.pixelSize: 11
                        color: "gray"
                        text: {
                            if (keyframes.scanning)
                                return "Indexing " + keyframes.progress + "%"
                                       + "   keyframes "
                                       + keyframes.keyframeCount
                            if (keyframes.errorString !== "")
                                return "Index error: " + keyframes.errorString
                            return "Index ready   keyframes "
                                   + keyframes.keyframeCount
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: "Open"
                        focusPolicy: Qt.NoFocus
                        onClicked: openDialog.open()
                    }

                    Button {
                        text: "<< 5s"
                        focusPolicy: Qt.NoFocus
                        enabled: videoPlayer.seekable
                        onClicked: videoPlayer.seekRelative(-jumpMs)
                    }

                    Button {
                        text: "< frame"
                        focusPolicy: Qt.NoFocus
                        enabled: videoPlayer.seekable
                        onClicked: videoPlayer.stepBackward()
                    }

                    Button {
                        enabled: videoPlayer.source !== ""
                        focusPolicy: Qt.NoFocus
                        text: videoPlayer.playing ? "Pause" : "Play"
                        onClicked: videoPlayer.togglePlay()
                    }

                    Button {
                        text: "frame >"
                        focusPolicy: Qt.NoFocus
                        enabled: videoPlayer.source !== ""
                        onClicked: videoPlayer.stepForward()
                    }

                    Button {
                        text: "5s >>"
                        focusPolicy: Qt.NoFocus
                        enabled: videoPlayer.seekable
                        onClicked: videoPlayer.seekRelative(jumpMs)
                    }

                    ComboBox {
                        id: rateBox
                        focusPolicy: Qt.NoFocus
                        implicitWidth: 90
                        model: rates.map(function(r) { return r + "x" })
                        currentIndex: rates.indexOf(1.0)
                        onActivated: videoPlayer.setRate(rates[index])
                    }

                    Label {
                        font.family: "monospace"
                        // Time of the frame on screen, from its timestamp;
                        // while dragging, the time under the handle.
                        text: formatTime(seekBar.pressed
                                         ? Math.round(seekBar.value)
                                         : Math.max(0, videoPlayer.frameTime))
                              + " / " + formatTime(videoPlayer.duration)
                    }

                    Label {
                        font.family: "monospace"
                        font.bold: true
                        color: videoPlayer.frameId < 0 ? "tomato"
                                                       : palette.text
                        text: videoPlayer.frameId < 0
                              ? "ID: ---------"
                              : "ID: " + padId(videoPlayer.frameId)
                    }

                    Item { Layout.fillWidth: true }

                    // Switch the keyframe index on or off, to compare the
                    // behaviour of the player with and without it.
                    CheckBox {
                        id: indexBox
                        focusPolicy: Qt.NoFocus
                        checked: videoPlayer.useKeyframeIndex
                        enabled: keyframes.keyframeCount > 0
                        text: "Use index"
                        onToggled: videoPlayer.useKeyframeIndex = checked
                    }

                    Label {
                        font.family: "monospace"
                        text: "no ID: " + videoPlayer.unreadableFrames
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Button {
                        text: "Open CSV"
                        onClicked: csvDialog.open()
                    }

                    // Match state of the frame on screen
                    Label {
                        font.family: "monospace"
                        font.bold: true
                        color: !metadata.loaded || videoPlayer.frameId < 0
                               ? "gray"
                               : (row.found ? "seagreen" : "tomato")
                        text: {
                            if (metadata.loading)
                                return "CSV: loading..."
                            if (!metadata.loaded)
                                return metadata.errorString !== ""
                                       ? "CSV: " + metadata.errorString
                                       : "CSV: not loaded"
                            if (videoPlayer.frameId < 0)
                                return "NO ID ON FRAME"
                            if (!row.found)
                                return "NO CSV ROW FOR ID "
                                       + padId(videoPlayer.frameId)
                            return "MATCH  row " + (row.row + 1)
                                   + "  pan " + row.pan
                                   + "  tilt " + row.tilt
                                   + "  mode " + row.mode
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        font.family: "monospace"
                        visible: metadata.loaded
                        text: "CSV rows " + metadata.rowCount
                              + " (ids " + metadata.firstId + "-"
                              + metadata.lastId + ")   matched "
                              + metadata.matchedFrames + "   no row "
                              + metadata.missingRowFrames
                    }
                }
            }
        }
    }
}
