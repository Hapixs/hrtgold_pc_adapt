import QtQuick
import QtTest
import "../../src/ui/qml"

TestCase {
    id: testCase
    name: "WelcomePage"
    when: windowShown
    width: 1280
    height: 720
    visible: true
    WelcomePage { id: page; anchors.fill: parent }
    SignalSpy { id: chooseSpy; target: page; signalName: "chooseRequested" }
    SignalSpy { id: keySpy; target: page; signalName: "keyRequested" }
    function test_choose_control_is_accessible_and_requests_picker() {
        verify(page.chooseControl.visible)
        verify(page.chooseControl.Accessible.name.length > 0)
        mouseClick(page.chooseControl)
        compare(chooseSpy.count, 1)
    }
    function test_keyboard_confirm_is_forwarded_to_bridge() {
        chooseSpy.clear()
        page.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(chooseSpy.count, 0)
        compare(keySpy.signalArguments[0][0], Qt.Key_Return)
    }
}
