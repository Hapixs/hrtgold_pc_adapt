import QtQuick
import QtQuick.Controls
import HeartGoldPC 1.0

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    minimumWidth: 960
    minimumHeight: 540
    visible: true
    title: "HeartGold PC Remastered"
    color: "#0a1425"
    visibility: uiState.fullscreen ? Window.FullScreen : Window.Windowed

    Item {
        id: scene
        anchors.fill: parent
        focus: true

        Rectangle { anchors.fill: parent; color: "#0b1728" }
        GameSurface {
            id: gameSurface
            objectName: "gameSurface"
            anchors.fill: parent
            visible: !uiState.welcomeVisible
            smooth: uiState.smoothScaling
        }
        Rectangle {
            anchors.fill: parent
            color: "#000000"
            opacity: 0.55
            visible: uiState.menuVisible
        }
        WelcomePage {
            id: welcomePage
            anchors.fill: parent
            visible: uiState.welcomeVisible
            statusMessage: uiState.statusMessage
            onChooseRequested: uiState.chooseRom()
            onKeyRequested: function(key, pressed, repeated) { uiState.handleKey(key, pressed, repeated) }
        }
        MainMenu {
            id: mainMenu
            anchors.fill: parent
            visible: uiState.menuVisible
            tileNames: uiState.tileNames
            statusMessage: uiState.statusMessage
            confirmHint: uiState.footerConfirm
            backHint: uiState.footerBack
            menuHint: uiState.footerMenu
            selectedIndex: uiState.selectedTile
            onKeyRequested: function(key, pressed, repeated) { uiState.handleKey(key, pressed, repeated) }
            onTileActivated: function(index) {
                uiState.setSelectedTile(index)
                uiState.activateSelected()
            }
        }
        OptionsPanel {
            id: optionsPanel
            anchors.fill: parent
            visible: uiState.optionsOpen
            volume: uiState.volume
            smoothScaling: uiState.smoothScaling
            fullscreen: uiState.fullscreen
            reopenLastGame: uiState.reopenLastGame
            selectedOption: uiState.selectedOption
            onOptionValueRequested: function(index, value) { uiState.setOptionValue(index, value) }
            onCloseRequested: uiState.closeOptions()
            onKeyRequested: function(key, pressed, repeated) { uiState.handleKey(key, pressed, repeated) }
        }

        Keys.onPressed: function(event) {
            uiState.handleKey(event.key, true, event.isAutoRepeat)
            event.accepted = true
        }
        Keys.onReleased: function(event) {
            uiState.handleKey(event.key, false, event.isAutoRepeat)
            event.accepted = true
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 20
        width: Math.min(parent.width - 40, 850)
        height: audioBanner.implicitHeight + 28
        radius: 12
        color: "#243e5c"
        visible: uiState.bannerMessage.length > 0
        Text {
            id: audioBanner
            anchors.centerIn: parent
            width: parent.width - 32
            text: uiState.bannerMessage
            color: "#ffe0a2"
            font.pixelSize: 18
            wrapMode: Text.WordWrap
        }
    }
    Dialog {
        id: saveError
        anchors.centerIn: parent
        width: Math.min(window.width - 64, 620)
        title: "Sauvegarde interrompue"
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        visible: uiState.blockingError
        onOpened: saveAcknowledge.forceActiveFocus()
        contentItem: Label {
            text: uiState.statusMessage
            wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            Button {
                id: saveAcknowledge
                text: "Compris"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            onAccepted: uiState.dismissError()
        }
    }
    Connections {
        target: uiState
        function onSessionStateChanged() {
            if (uiState.menuVisible) mainMenu.forceActiveFocus()
            else if (uiState.welcomeVisible) welcomePage.forceActiveFocus()
            else scene.forceActiveFocus()
        }
        function onOptionsOpenChanged() {
            if (uiState.optionsOpen) optionsPanel.forceActiveFocus()
            else if (uiState.menuVisible) mainMenu.forceActiveFocus()
        }
    }
    Component.onCompleted: {
        if (uiState.menuVisible) mainMenu.forceActiveFocus()
        else if (uiState.welcomeVisible) welcomePage.forceActiveFocus()
        else scene.forceActiveFocus()
    }
}
