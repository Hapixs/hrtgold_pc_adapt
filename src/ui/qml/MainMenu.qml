import QtQuick
import "components"

FocusScope {
    id: menu
    property int selectedIndex: 0
    property var tileNames: []
    property string statusMessage: ""
    property string confirmHint: ""
    property string backHint: ""
    property string menuHint: ""
    readonly property int tileCount: tiles.count
    readonly property real canvasScale: Math.min(width / 1920, height / 1080)
    signal tileActivated(int index)
    signal resumeRequested()

    function tileAt(index) { return tiles.itemAt(index) }
    function move(horizontal, vertical) {
        const row = Math.floor(selectedIndex / 4)
        const column = selectedIndex % 4
        selectedIndex = ((row + vertical + 2) % 2) * 4 + ((column + horizontal + 4) % 4)
    }

    Keys.onPressed: function(event) {
        switch (event.key) {
        case Qt.Key_Left: case Qt.Key_A: move(-1, 0); break
        case Qt.Key_Right: case Qt.Key_D: move(1, 0); break
        case Qt.Key_Up: case Qt.Key_W: move(0, -1); break
        case Qt.Key_Down: case Qt.Key_S: move(0, 1); break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_E: tileActivated(selectedIndex); break
        case Qt.Key_Escape: resumeRequested(); break
        default: event.accepted = false; return
        }
        event.accepted = true
    }

    Item {
        id: canvas
        width: 1920
        height: 1080
        scale: menu.canvasScale
        transformOrigin: Item.TopLeft
        x: (menu.width - width * scale) / 2
        y: (menu.height - height * scale) / 2

        Rectangle { x: 76; y: 64; width: 1768; height: 166; radius: 22; color: "#b3132743" }
        Rectangle { x: 76; y: 892; width: 1768; height: 113; radius: 22; color: "#b3132743" }

        Text {
            x: 96; y: 90
            text: "MENU PRINCIPAL"
            color: "#e9f4ff"
            font.pixelSize: 24
            font.weight: Font.Bold
            font.letterSpacing: 7
        }
        Text {
            x: 96; y: 133
            text: "Votre aventure, à votre rythme"
            color: "#b8cee8"
            font.pixelSize: 46
            font.weight: Font.DemiBold
        }
        Rectangle { x: 96; y: 224; width: 1728; height: 2; color: "#6695b5"; opacity: 0.7 }

        Grid {
            id: grid
            x: 104; y: 278
            width: 1712
            columns: 4
            columnSpacing: 24
            rowSpacing: 26
            Repeater {
                id: tiles
                model: 8
                MenuTile {
                    required property int index
                    width: (grid.width - 3 * grid.columnSpacing) / 4
                    height: 224
                    title: menu.tileNames.length > index ? menu.tileNames[index] : ""
                    iconIndex: index
                    selected: menu.selectedIndex === index
                    unavailable: index < 6
                    onActivated: {
                        menu.selectedIndex = index
                        menu.tileActivated(index)
                    }
                }
            }
        }

        Text {
            x: 96; y: 813
            width: 1728
            height: 52
            color: "#ffe1a6"
            text: menu.statusMessage
            font.pixelSize: 25
            elide: Text.ElideRight
        }
        Rectangle { x: 96; y: 899; width: 1728; height: 2; color: "#6695b5"; opacity: 0.7 }
        Row {
            x: 96; y: 936
            spacing: 60
            Repeater {
                model: [menu.confirmHint, menu.backHint, menu.menuHint]
                Text {
                    required property string modelData
                    text: modelData
                    color: "#e4efff"
                    font.pixelSize: 23
                }
            }
        }
    }
}
