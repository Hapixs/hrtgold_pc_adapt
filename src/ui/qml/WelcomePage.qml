import QtQuick

FocusScope {
    id: welcome
    property string statusMessage: ""
    property alias chooseControl: chooseButton
    signal chooseRequested()
    readonly property real canvasScale: Math.min(width / 1920, height / 1080)

    Rectangle { anchors.fill: parent; color: "#132743" }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#244969" }
            GradientStop { position: 1; color: "#102541" }
        }
    }
    Item {
        width: 1920; height: 1080
        scale: welcome.canvasScale
        transformOrigin: Item.TopLeft
        x: (welcome.width - width * scale) / 2
        y: (welcome.height - height * scale) / 2

        Rectangle {
            x: 96; y: 110; width: 120; height: 8; radius: 4; color: "#9ed9e9"
        }
        Text {
            x: 96; y: 185
            text: "HEARTGOLD PC REMASTERED"
            color: "#e8f8ff"
            font.pixelSize: 62
            font.weight: Font.Bold
            font.letterSpacing: 5
        }
        Text {
            x: 96; y: 300
            text: "Bienvenue dans votre aventure sur PC"
            color: "#c8ddea"
            font.pixelSize: 34
        }
        Rectangle {
            x: 96; y: 450; width: 1728; height: 365
            radius: 36; color: "#243e5c"
            border.color: "#6ea0b7"; border.width: 2
        }
        Text {
            x: 155; y: 517
            text: "Prêt à jouer ?"
            color: "#f4fbff"
            font.pixelSize: 47
            font.weight: Font.DemiBold
        }
        Text {
            x: 155; y: 603
            text: "Choisissez votre fichier local Pokémon HeartGold (.nds)."
            color: "#c7dcea"
            font.pixelSize: 27
        }
        Rectangle {
            id: chooseButton
            x: 155; y: 687; width: 455; height: 82
            radius: 22
            color: activeFocus ? "#b9e6f1" : "#91cadf"
            border.color: activeFocus ? "#ffffff" : "#5d9bb7"
            border.width: activeFocus ? 4 : 2
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Choisir un jeu HeartGold"
            Accessible.focused: activeFocus
            Text {
                anchors.centerIn: parent
                text: "Choisir un jeu"
                color: "#14304b"
                font.pixelSize: 29
                font.weight: Font.DemiBold
            }
            MouseArea { anchors.fill: parent; onClicked: welcome.chooseRequested() }
            Keys.onReturnPressed: welcome.chooseRequested()
            Keys.onSpacePressed: welcome.chooseRequested()
        }
        Text {
            x: 96; y: 855; width: 1728; height: 100
            text: welcome.statusMessage
            color: "#ffe0a2"
            font.pixelSize: 27
            wrapMode: Text.WordWrap
        }
    }
}
