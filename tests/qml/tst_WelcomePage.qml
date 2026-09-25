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
    function test_choose_control_is_accessible_and_requests_picker() {
        verify(page.chooseControl.visible)
        verify(page.chooseControl.Accessible.name.length > 0)
        mouseClick(page.chooseControl)
        compare(chooseSpy.count, 1)
    }
}
