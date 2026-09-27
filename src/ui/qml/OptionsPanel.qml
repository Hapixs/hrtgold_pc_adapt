import QtQuick
import QtQuick.Controls

FocusScope {
    id: panel
    property int volume: 100
    property bool smoothScaling: false
    property bool fullscreen: false
    property bool reopenLastGame: false
    property int selectedOption: 0
    readonly property bool optionFocusVisible: visible && selectedOption >= 0 && selectedOption <= 4
    property alias volumeControl: volumeSlider
    property alias smoothScalingControl: scalingSwitch
    property alias closeControl: closeButton
    signal optionValueRequested(int index, var value)
    signal closeRequested()
    signal keyRequested(int key, bool pressed, bool repeated)
    readonly property real canvasScale: Math.min(width / 1920, height / 1080)

    Keys.onPressed: function(event) {
        keyRequested(event.key, true, event.isAutoRepeat)
        event.accepted = true
    }
    Keys.onReleased: function(event) {
        keyRequested(event.key, false, event.isAutoRepeat)
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
        Rectangle {
            x: panel.selectedOption === 4 ? 1158 : 425
            y: [392, 482, 572, 662, 798][panel.selectedOption]
            width: panel.selectedOption === 4 ? 325 : 1070
            height: 75
            radius: 17
            color: "#314e73"
            border.color: "#a5d9ed"
            border.width: 2
            visible: panel.optionFocusVisible
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
            focusPolicy: Qt.NoFocus
            Accessible.name: "Volume"
            Accessible.focused: panel.selectedOption === 0
            onMoved: panel.optionValueRequested(0, Math.round(value))
        }
        Text { x: 1380; y: 407; text: Math.round(volumeSlider.value) + "%"; color: "#d7eaff"; font.pixelSize: 26 }

        Text { x: 440; y: 498; text: "Lissage de l’image"; color: "#f4f9ff"; font.pixelSize: 29 }
        Switch {
            id: scalingSwitch
            x: 1330; y: 486
            checked: panel.smoothScaling
            focusPolicy: Qt.NoFocus
            Accessible.name: "Lissage de l’image"
            Accessible.focused: panel.selectedOption === 1
            onToggled: panel.optionValueRequested(1, checked)
        }
        Text { x: 440; y: 588; text: "Plein écran"; color: "#f4f9ff"; font.pixelSize: 29 }
        Switch {
            id: fullscreenSwitch
            x: 1330; y: 576
            checked: panel.fullscreen
            focusPolicy: Qt.NoFocus
            Accessible.name: "Plein écran"
            Accessible.focused: panel.selectedOption === 2
            onToggled: panel.optionValueRequested(2, checked)
        }
        Text { x: 440; y: 678; text: "Rouvrir le dernier jeu"; color: "#f4f9ff"; font.pixelSize: 29 }
        Switch {
            id: reopenSwitch
            x: 1330; y: 666
            checked: panel.reopenLastGame
            focusPolicy: Qt.NoFocus
            Accessible.name: "Rouvrir le dernier jeu"
            Accessible.focused: panel.selectedOption === 3
            onToggled: panel.optionValueRequested(3, checked)
        }
        Rectangle {
            id: closeButton
            x: 1170; y: 804; width: 300; height: 70
            radius: 19
            color: panel.selectedOption === 4 ? "#b9e6f1" : "#91cadf"
            border.color: panel.selectedOption === 4 ? "#ffffff" : "#5d9bb7"
            border.width: panel.selectedOption === 4 ? 4 : 2
            Accessible.role: Accessible.Button
            Accessible.name: "Retour au menu"
            Accessible.focused: panel.selectedOption === 4
            Text {
                anchors.centerIn: parent
                text: "Retour au menu"
                color: "#13304a"
                font.pixelSize: 25
                font.weight: Font.DemiBold
            }
            MouseArea { anchors.fill: parent; onClicked: panel.closeRequested() }
        }
    }
}
