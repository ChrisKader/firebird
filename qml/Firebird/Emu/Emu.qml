pragma Singleton
import QtQuick 6.0

QtObject {
    // Settings/state mirrored from the C++ bridge. These stubs satisfy qmllint when the
    // native backend is not available in design-time.
    property bool gdbEnabled: true
    property int gdbPort: 3333
    property bool rdbEnabled: true
    property int rdbPort: 3334
    property bool debugOnStart: false
    property bool debugOnWarn: true
    property bool printOnWarn: true
    property bool autostart: true
    property bool running: false
    property bool isRunning: running
    property bool leftHanded: false
    property bool darkTheme: true
    property bool suspendOnClose: true
    property bool turboMode: false
    property real speed: 0.0
    property int defaultKit: 0
    property var kits: []
    property string usbdir: "/ndless"
    property int mobileX: -1
    property int mobileY: -1
    property int mobileWidth: -1
    property int mobileHeight: -1
    property string version: "1.6"

    function useDefaultKit() { return true; }
    function kitIndexForID(id) { return 0; }
    function isMobile() { return true; }
    function setPaused(paused) { }
    function suspend() { toastMessage("Suspend"); }
    function resume() { toast.showMessage("Resume"); }
    function reset() { toastMessage("Reset"); }
    function restart() { toastMessage("Restart"); return true; }
    function saveFlash() { return true; }
    function getSnapshotPath() { return ""; }
    function getBoot1Path() { return ""; }
    function getFlashPath() { return ""; }
    function dir(path) { return path || "/"; }
    function toLocalFile(url) { return url; }
    function basename(path) { return path; }
    function registerToast(toastref) { toast = toastref; }
    function registerNButton(keymap_id, buttonref) {}
    function setCurrentKit(id) { return true; }
    function createFlash(path) { return false; }
    function manufDescription(path) { return ""; }
    function componentDescription(path, expected) { return ""; }
    function osDescription(path) { return ""; }
    function sendFile(url, dirPath) { }
    function sendExitPTT() { }
    function switchUIMode() { }
    function setTurboMode(enabled) { turboMode = enabled; }
    function dirExists() { return true; }
    function fileExists() { return true; }
    function setTouchpadState(x, y, down, contact) { touchpadStateChanged(x, y, down, contact); }
    function setButtonState(keymap_id, down) { buttonStateChanged(keymap_id, down); }

    signal toastMessage(string msg)
    signal touchpadStateChanged(real x, real y, bool down, bool contact)
    signal buttonStateChanged(int id, bool state)
    signal emuSuspended(bool success)
    signal turboModeChanged()
}
