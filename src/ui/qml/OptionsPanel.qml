import QtQuick
import QtQuick.Controls

FocusScope {
    id: panel
    property int volume: 100
    property bool smoothScaling: false
    property bool fullscreen: false
    property bool reopenLastGame: false
    property alias volumeControl: volumeSlider
    property alias smoothScalingControl: scalingSwitch
    property alias closeControl: closeButton
    signal optionRequested(string name, var value)
    signal closeRequested()
    readonly property real canvasScale: Math.min(width / 1920, height / 1080)

    Keys.onEscapePressed: function(event) {
        closeRequested()
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: "#bb071426" }
    Item {
        width: 1920; height: 1080
        scale: panel.canvasScale
        transformOrigin: Item.TopLeft
        x: (panel.width - width * scale) / 2
        y: (panel.height - height * scale) / 2

        Rectangle {
            x: 365; y: 155; width: 1190; height: 770
            radius: 36
            color: "#203657"
            border.color: "#86bad5"
            border.width: 2
        }
        Text {
            x: 440; y: 210
            text: "OPTIONS"
            color: "#e9f6ff"
            font.pixelSize: 45
            font.weight: Font.Bold
            font.letterSpacing: 6
        }
        Text {
            x: 440; y: 292
            text: "Ajustez l’affichage et le son"
            color: "#b8d2e8"
            font.pixelSize: 25
        }
        Rectangle { x: 440; y: 361; width: 1040; height: 2; color: "#608cb1" }

        Text { x: 440; y: 407; text: "Volume"; color: "#f4f9ff"; font.pixelSize: 29 }
        Slider {
            id: volumeSlider
            x: 908; y: 398; width: 460; height: 54
            from: 0; to: 100; stepSize: 1
            value: panel.volume
            Accessible.name: "Volume"
            onMoved: panel.optionRequested("volume", Math.round(value))
        }
        Text { x: 1380; y: 407; text: Math.round(volumeSlider.value) + "%"; color: "#d7eaff"; font.pixelSize: 26 }

        Text { x: 440; y: 498; text: "Lissage de l’image"; color: "#f4f9ff"; font.pixelSize: 29 }
        Switch {
            id: scalingSwitch
            x: 1330; y: 486
            checked: panel.smoothScaling
            Accessible.name: "Lissage de l’image"
            onToggled: panel.optionRequested("smoothScaling", checked)
        }
        Text { x: 440; y: 588; text: "Plein écran"; color: "#f4f9ff"; font.pixelSize: 29 }
        Switch {
            id: fullscreenSwitch
            x: 1330; y: 576
            checked: panel.fullscreen
            Accessible.name: "Plein écran"
            onToggled: panel.optionRequested("fullscreen", checked)
        }
        Text { x: 440; y: 678; text: "Rouvrir le dernier jeu"; color: "#f4f9ff"; font.pixelSize: 29 }
        Switch {
            id: reopenSwitch
            x: 1330; y: 666
            checked: panel.reopenLastGame
            Accessible.name: "Rouvrir le dernier jeu"
            onToggled: panel.optionRequested("reopenLastGame", checked)
        }
        Rectangle {
            id: closeButton
            x: 1170; y: 804; width: 300; height: 70
            radius: 19
            color: activeFocus ? "#b9e6f1" : "#91cadf"
            border.color: activeFocus ? "#ffffff" : "#5d9bb7"
            border.width: activeFocus ? 4 : 2
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Retour au menu"
            Accessible.focused: activeFocus
            Text {
                anchors.centerIn: parent
                text: "Retour au menu"
                color: "#13304a"
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }
            MouseArea { anchors.fill: parent; onClicked: panel.closeRequested() }
            Keys.onReturnPressed: panel.closeRequested()
            Keys.onSpacePressed: panel.closeRequested()
        }
    }
}
