import QtQuick
import QtTest
import "../../src/ui/qml"

TestCase {
    id: testCase
    name: "OptionsPanel"
    when: windowShown
    width: 1920
    height: 1080
    visible: true

    OptionsPanel {
        id: panel
        anchors.fill: parent
        visible: true
        volume: 65
        smoothScaling: false
        fullscreen: false
        reopenLastGame: false
    }
    SignalSpy { id: optionSpy; target: panel; signalName: "optionRequested" }
    SignalSpy { id: closeSpy; target: panel; signalName: "closeRequested" }

    function init() {
        panel.forceActiveFocus()
        optionSpy.clear()
        closeSpy.clear()
    }

    function test_controls_present_values_and_request_changes() {
        compare(panel.volumeControl.value, 65)
        compare(panel.smoothScalingControl.checked, false)
        mouseClick(panel.smoothScalingControl)
        compare(optionSpy.signalArguments[0][0], "smoothScaling")
        compare(optionSpy.signalArguments[0][1], true)
    }

    function test_back_requests_close() {
        keyClick(Qt.Key_Escape)
        compare(closeSpy.count, 1)
    }

    function test_visible_close_control_requests_close() {
        verify(panel.closeControl.visible)
        mouseClick(panel.closeControl)
        compare(closeSpy.count, 1)
    }
}
