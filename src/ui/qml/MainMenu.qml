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
    signal keyRequested(int key, bool pressed, bool repeated)

    function tileAt(index) { return tiles.itemAt(index) }
    Keys.onPressed: function(event) {
        keyRequested(event.key, true, event.isAutoRepeat)
        event.accepted = true
    }
    Keys.onReleased: function(event) {
        keyRequested(event.key, false, event.isAutoRepeat)
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
                    onActivated: menu.tileActivated(index)
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
