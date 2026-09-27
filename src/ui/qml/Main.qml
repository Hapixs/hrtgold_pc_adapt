import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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

    FileDialog {
        id: romDialog
        title: "Choisir une ROM HeartGold"
        nameFilters: ["Jeux Nintendo DS (*.nds)"]
        onAccepted: uiState.loadRomUrl(romDialog.selectedFile)
    }
    Connections {
        target: uiState
        function onRomSelectionRequested() { romDialog.open() }
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
