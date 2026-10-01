import QtQuick
import QtQuick.Shapes

Item {
    id: tile
    property string title: ""
    property int iconIndex: 0
    property bool selected: false
    property bool unavailable: false
    readonly property bool focusRingVisible: selected && visible
    signal activated()

    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: unavailable ? "Disponible dans une prochaine étape" : title
    Accessible.focused: selected

    Rectangle {
        anchors.fill: parent
        radius: 30
        color: selected ? "#283d67" : "#203357"
        gradient: Gradient {
            GradientStop { position: 0; color: tile.selected ? "#35527d" : "#2a426a" }
            GradientStop { position: 1; color: tile.selected ? "#17294b" : "#192c4d" }
        }
        border.color: selected ? "#a8d4ff" : "#547095"
        border.width: selected ? 3 : 1
    }
    Rectangle {
        id: focusRing
        objectName: "focusRing"
        anchors.fill: parent
        anchors.margins: -7
        radius: 37
        color: "transparent"
        border.color: "#cdeaff"
        border.width: 4
        visible: tile.focusRingVisible
    }
    Shape {
        x: 32
        y: 33
        width: 74
        height: 74
        opacity: unavailable ? 0.72 : 1
        ShapePath {
            strokeColor: "#b8e4ff"
            strokeWidth: 4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            startX: 8; startY: 18
            PathLine { x: 58; y: 18 }
            PathLine { x: 58; y: 58 }
            PathLine { x: 8; y: 58 }
            PathLine { x: 8; y: 18 }
            PathMove { x: 22; y: 37 }
            PathLine { x: 45; y: 37 }
        }
    }
    Rectangle {
        x: 70
        y: 63
        width: 12
        height: 12
        radius: 6
        color: "#b8e4ff"
    }
    Text {
        x: 32
        y: 132
        width: parent.width - 64
        text: tile.title
        color: "#f5f9ff"
        font.pixelSize: 35
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    Text {
        x: 32
        y: 180
        text: tile.unavailable ? "BIENTÔT" : "OUVRIR"
        color: tile.unavailable ? "#a8bbd4" : "#aee4cf"
        font.pixelSize: 16
        font.letterSpacing: 3
        font.weight: Font.Bold
    }
    MouseArea {
        anchors.fill: parent
        onClicked: tile.activated()
    }
}
