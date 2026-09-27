import QtQuick
import QtTest
import "../../src/ui/qml"

TestCase {
    id: testCase
    name: "MainMenu"
    when: windowShown
    width: 1920
    height: 1080
    visible: true

    MainMenu {
        id: menu
        anchors.fill: parent
        visible: true
        tileNames: ["Pokédex", "Équipe", "Sac", "Dresseur", "Pokégear", "Carte", "Options", "Reprendre"]
    }
    SignalSpy { id: activatedSpy; target: menu; signalName: "tileActivated" }
    SignalSpy { id: keySpy; target: menu; signalName: "keyRequested" }

    function init() {
        menu.selectedIndex = 0
        menu.forceActiveFocus()
        activatedSpy.clear()
        keySpy.clear()
    }

    function test_keys_forward_to_bridge_without_local_navigation() {
        compare(menu.selectedIndex, 0)
        keyClick(Qt.Key_Right)
        compare(menu.selectedIndex, 0)
        compare(keySpy.signalArguments[0][0], Qt.Key_Right)
        compare(keySpy.signalArguments[0][1], true)
        keyClick(Qt.Key_Return)
        compare(menu.selectedIndex, 0)
        compare(activatedSpy.count, 0)
    }

    function test_back_is_forwarded_to_bridge() {
        keyClick(Qt.Key_Escape)
        compare(keySpy.signalArguments[0][0], Qt.Key_Escape)
        compare(keySpy.signalArguments[0][1], true)
    }

    function test_pointer_activation_reports_tile_without_mutating_selection() {
        mouseClick(menu.tileAt(6))
        compare(activatedSpy.signalArguments[0][0], 6)
        compare(menu.selectedIndex, 0)
    }

    function test_tiles_have_accessible_names_and_unavailable_descriptions() {
        compare(menu.tileCount, 8)
        for (let index = 0; index < 8; ++index) {
            let tile = menu.tileAt(index)
            verify(tile !== null)
            verify(tile.Accessible.name.length > 0)
            if (index < 6) verify(tile.Accessible.description.length > 0)
            menu.selectedIndex = index
            verify(tile.focusRingVisible, "index=" + index + " selected=" + tile.selected + " visible=" + tile.visible)
        }
    }

    function test_layout_stays_inside_safe_area() {
        for (const size of [[1280, 720], [1920, 1080], [3840, 2160]]) {
            testCase.width = size[0]
            testCase.height = size[1]
            wait(0)
            for (let index = 0; index < 8; ++index) {
                let tile = menu.tileAt(index)
                let origin = tile.mapToItem(menu, 0, 0)
                verify(origin.x >= size[0] * 0.05)
                verify(origin.y >= size[1] * 0.05)
                verify(origin.x + tile.width * menu.canvasScale <= size[0] * 0.95)
                verify(origin.y + tile.height * menu.canvasScale <= size[1] * 0.95)
            }
        }
    }
}
