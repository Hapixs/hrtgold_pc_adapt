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
    SignalSpy { id: optionSpy; target: panel; signalName: "optionValueRequested" }
    SignalSpy { id: closeSpy; target: panel; signalName: "closeRequested" }
    SignalSpy { id: keySpy; target: panel; signalName: "keyRequested" }

    function init() {
        panel.forceActiveFocus()
        optionSpy.clear()
        closeSpy.clear()
        keySpy.clear()
    }

    function test_controls_present_values_and_request_changes() {
        compare(panel.volumeControl.value, 65)
        compare(panel.smoothScalingControl.checked, false)
        mouseClick(panel.smoothScalingControl)
        compare(optionSpy.signalArguments[0][0], 1)
        compare(optionSpy.signalArguments[0][1], true)
    }

    function test_background_mute_control_requests_change() {
        const control = findChild(panel, "muteInBackgroundControl")
        verify(control !== null)
        compare(control.checked, false)
        panel.selectedOption = 4
        verify(panel.optionFocusVisible)
        mouseClick(control)
        compare(optionSpy.count, 1)
        compare(optionSpy.signalArguments[0][0], 4)
        compare(optionSpy.signalArguments[0][1], true)
    }

    function test_back_is_forwarded_without_local_close() {
        keyClick(Qt.Key_Escape)
        compare(closeSpy.count, 0)
        compare(keySpy.signalArguments[0][0], Qt.Key_Escape)
    }

    function test_selected_option_is_visual_only() {
        panel.selectedOption = 2
        verify(panel.optionFocusVisible)
        keyClick(Qt.Key_Down)
        compare(panel.selectedOption, 2)
        compare(keySpy.signalArguments[0][0], Qt.Key_Down)
    }

    function test_visible_close_control_requests_close() {
        verify(panel.closeControl.visible)
        mouseClick(panel.closeControl)
        compare(closeSpy.count, 1)
    }
}
