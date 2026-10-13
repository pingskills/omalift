import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1000
    height: 760
    minimumWidth: 560
    minimumHeight: 480
    visible: true
    title: backend.hasImage ? "omalift — " + fileName(backend.source) : "omalift"

    readonly property color accent: backend.themeAccent
    readonly property color accentForeground: backend.themeAccentForeground
    readonly property bool ready: backend.ready
    readonly property bool keying: backend.mode === "screen"
    // What the cutout is shown against. A cutout that looks clean on the
    // checkerboard can still carry a fringe that only shows on a flat colour.
    readonly property var backdrops: ["Checkerboard", "White", "Black", "Pink"]
    readonly property var backdropColors: ["transparent", "#ffffff", "#000000", "#ff00c8"]
    property int backdrop: 0
    property bool showOriginal: false
    property bool helpVisible: false

    Material.theme: Material.Dark
    Material.accent: win.accent
    color: "#0e0e10"

    function fileName(url) {
        var s = url.toString();
        return s === "" ? "" : decodeURIComponent(s.substring(s.lastIndexOf('/') + 1));
    }
    function openPhoto() {
        openDialog.open();
    }
    function savePhoto() {
        if (!win.ready || backend.busy)
            return;
        saveDialog.selectedFile = backend.suggestedSaveUrl();
        saveDialog.open();
    }
    function nextBackdrop() {
        backdrop = (backdrop + 1) % backdrops.length;
    }
    // Moves one corner of the crop, given as a fraction of the photo, and
    // keeps the box from collapsing or turning inside out.
    function moveCorner(right, bottom, x, y) {
        var smallest = 0.05;
        var c = backend.crop;
        var l = c.x, t = c.y, r = c.x + c.width, b = c.y + c.height;
        x = Math.max(0, Math.min(1, x));
        y = Math.max(0, Math.min(1, y));
        if (right)
            r = Math.max(l + smallest, x);
        else
            l = Math.min(r - smallest, x);
        if (bottom)
            b = Math.max(t + smallest, y);
        else
            t = Math.min(b - smallest, y);
        backend.crop = Qt.rect(l, t, r - l, b - t);
    }

    FileDialog {
        id: openDialog
        title: "Open a green screen photo"
        nameFilters: ["Photos (*.jpg *.jpeg *.png *.webp *.bmp *.tif *.tiff)", "All files (*)"]
        onAccepted: backend.load(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: "Save the cutout"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["PNG image (*.png)"]
        onAccepted: backend.save(selectedFile)
    }

    Shortcut { sequences: [StandardKey.Open]; onActivated: win.openPhoto() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: win.savePhoto() }
    Shortcut { sequence: "1"; enabled: backend.hasImage; onActivated: backend.mode = "screen" }
    Shortcut { sequence: "2"; enabled: backend.hasImage; onActivated: backend.mode = "fast" }
    Shortcut { sequence: "3"; enabled: backend.hasImage; onActivated: backend.mode = "best" }
    Shortcut { sequence: "B"; enabled: win.ready; onActivated: win.nextBackdrop() }
    Shortcut { sequence: "R"; enabled: win.ready; onActivated: backend.reset() }
    Shortcut { sequence: "?"; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; onActivated: win.close() }

    // Space shows the untouched photo for as long as it is held.
    Item {
        focus: true
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
                win.showOriginal = true;
                event.accepted = true;
            }
        }
        Keys.onReleased: (event) => {
            if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
                win.showOriginal = false;
                event.accepted = true;
            }
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls && drop.urls.length > 0)
                backend.load(drop.urls[0]);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Before a photo is open.
            ColumnLayout {
                anchors.centerIn: parent
                visible: !backend.hasImage
                spacing: 14

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Drop a photo here"
                    font.pixelSize: 24
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "The background is removed and the person saved as a PNG cutout."
                    opacity: 0.6
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Open photo"
                    Material.background: win.accent
                    Material.foreground: win.accentForeground
                    onClicked: win.openPhoto()
                }
            }

            // Exactly the area the photo is painted in, so the crop box and
            // its handles can be laid out in the photo's own proportions.
            Item {
                id: frame
                visible: backend.hasImage
                readonly property real fit: backend.hasImage
                    ? Math.min(stage.width / backend.imageSize.width, stage.height / backend.imageSize.height)
                    : 0
                width: backend.imageSize.width * fit
                height: backend.imageSize.height * fit
                anchors.centerIn: parent

                readonly property bool lifted: win.ready && !win.showOriginal

                Canvas {
                    id: checker
                    anchors.fill: parent
                    visible: frame.lifted && win.backdrop === 0
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d");
                        var cell = 12;
                        ctx.fillStyle = "#3a3a40";
                        ctx.fillRect(0, 0, width, height);
                        ctx.fillStyle = "#2a2a2e";
                        for (var y = 0; y * cell < height; y++)
                            for (var x = y % 2; x * cell < width; x += 2)
                                ctx.fillRect(x * cell, y * cell, cell, cell);
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    visible: frame.lifted && win.backdrop !== 0
                    color: win.backdropColors[win.backdrop]
                }
                Image {
                    anchors.fill: parent
                    cache: false
                    mipmap: true
                    source: backend.hasImage
                        ? "image://lift/" + (frame.lifted ? "lifted" : "original") + "/" + backend.revision
                        : ""
                }

                // Everything outside the crop is dimmed; it is not saved.
                Item {
                    id: cropBox
                    visible: win.ready
                    x: backend.crop.x * frame.width
                    y: backend.crop.y * frame.height
                    width: backend.crop.width * frame.width
                    height: backend.crop.height * frame.height
                }
                Repeater {
                    model: win.ready ? [
                        { x: 0, y: 0, w: frame.width, h: cropBox.y },
                        { x: 0, y: cropBox.y + cropBox.height, w: frame.width, h: frame.height - cropBox.y - cropBox.height },
                        { x: 0, y: cropBox.y, w: cropBox.x, h: cropBox.height },
                        { x: cropBox.x + cropBox.width, y: cropBox.y, w: frame.width - cropBox.x - cropBox.width, h: cropBox.height }
                    ] : []
                    Rectangle {
                        x: modelData.x
                        y: modelData.y
                        width: Math.max(0, modelData.w)
                        height: Math.max(0, modelData.h)
                        color: "#b00e0e10"
                    }
                }
                Rectangle {
                    visible: win.ready
                    x: cropBox.x
                    y: cropBox.y
                    width: cropBox.width
                    height: cropBox.height
                    color: "transparent"
                    border.color: win.accent
                    border.width: 1
                }
                Repeater {
                    model: win.ready ? 4 : 0
                    Rectangle {
                        id: grip
                        readonly property bool onRight: index % 2 === 1
                        readonly property bool onBottom: index >= 2
                        width: 16
                        height: 16
                        radius: 8
                        color: "#ffffff"
                        border.color: win.accent
                        border.width: 2
                        x: cropBox.x + (onRight ? cropBox.width : 0) - width / 2
                        y: cropBox.y + (onBottom ? cropBox.height : 0) - height / 2

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -10
                            cursorShape: grip.onRight === grip.onBottom ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                            onPositionChanged: (mouse) => {
                                var at = mapToItem(frame, mouse.x, mouse.y);
                                win.moveCorner(grip.onRight, grip.onBottom, at.x / frame.width, at.y / frame.height);
                            }
                        }
                    }
                }
            }
        }

        // How the background is removed.
        RowLayout {
            Layout.fillWidth: true
            visible: backend.hasImage
            spacing: 8

            component ModeButton: Button {
                property string mode
                readonly property bool chosen: backend.mode === mode
                Material.elevation: 0
                Material.background: chosen ? win.accent : "#26262b"
                Material.foreground: chosen ? win.accentForeground : "#ffffff"
                onClicked: backend.mode = mode
            }

            ModeButton { text: "Green screen"; mode: "screen" }
            ModeButton { text: "Fast"; mode: "fast" }
            ModeButton { text: "Best"; mode: "best" }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                opacity: 0.6
                text: win.keying ? "Removes a green backdrop, instantly."
                    : backend.mode === "fast" ? "Any background. The quicker model."
                    : "Any background. Slower, with finer edges."
            }
        }

        // What a model still needs before there is a cutout to show.
        RowLayout {
            Layout.fillWidth: true
            visible: backend.hasImage && !win.keying && !win.ready
            spacing: 12

            Label {
                Layout.fillWidth: true
                visible: !backend.modelInstalled && !backend.downloading
                wrapMode: Text.WordWrap
                text: "This needs its model downloaded once (" + backend.modelSize + ")."
            }
            Button {
                visible: !backend.modelInstalled && !backend.downloading
                text: "Download"
                Material.background: win.accent
                Material.foreground: win.accentForeground
                onClicked: backend.downloadModel()
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: backend.downloading
                value: backend.downloadProgress
            }
            Button {
                visible: backend.downloading
                text: "Cancel"
                flat: true
                onClicked: backend.cancelDownload()
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: backend.working
                indeterminate: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: win.ready && win.keying
            spacing: 20

            component Knob: ColumnLayout {
                property alias label: caption.text
                property alias value: slider.value
                signal moved(real value)
                Layout.fillWidth: true
                spacing: 0
                Label {
                    id: caption
                    opacity: 0.7
                    font.pixelSize: 12
                }
                Slider {
                    id: slider
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    onMoved: parent.moved(value)
                }
            }

            Knob {
                label: "Strength"
                value: backend.strength
                onMoved: (value) => backend.strength = value
            }
            Knob {
                label: "Edge softness"
                value: backend.softness
                onMoved: (value) => backend.softness = value
            }
            Knob {
                label: "Remove green tint"
                value: backend.despill
                onMoved: (value) => backend.despill = value
            }
        }

        Label {
            Layout.fillWidth: true
            visible: backend.hasImage
            elide: Text.ElideMiddle
            text: backend.status
            opacity: 0.7
        }

        // Kept short enough to fit a window tiled into half a screen.
        RowLayout {
            Layout.fillWidth: true
            visible: backend.hasImage
            spacing: 8

            Button {
                text: "Open"
                flat: true
                onClicked: win.openPhoto()
            }
            Button {
                text: win.backdrops[win.backdrop]
                flat: true
                visible: win.ready
                ToolTip.visible: hovered
                ToolTip.text: "What the cutout is shown against (B)"
                onClicked: win.nextBackdrop()
            }
            Button {
                text: "Original"
                flat: true
                visible: win.ready
                ToolTip.visible: hovered
                ToolTip.text: "Hold to see the original photo (Space)"
                onPressedChanged: win.showOriginal = pressed
            }
            Button {
                text: "Reset"
                flat: true
                visible: win.ready
                onClicked: backend.reset()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Save cutout"
                visible: win.ready
                enabled: !backend.busy
                Material.background: win.accent
                Material.foreground: win.accentForeground
                onClicked: win.savePhoto()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: win.helpVisible
        color: "#e60e0e10"

        MouseArea {
            anchors.fill: parent
            onClicked: win.helpVisible = false
        }
        Label {
            anchors.centerIn: parent
            lineHeight: 1.5
            font.pixelSize: 15
            font.family: "monospace"
            text: "Ctrl+O   Open a photo\n"
                + "Ctrl+S   Save the cutout\n"
                + "1 2 3    Green screen, Fast, Best\n"
                + "Space    Hold to see the original photo\n"
                + "B        Change the backdrop\n"
                + "R        Reset the sliders and the crop\n"
                + "Q        Quit\n"
                + "?        Show or hide this"
        }
    }
}
