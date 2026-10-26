# Third-Party Licenses

This project incorporates or was inspired by the following:

## AMD Display Library (ADL) SDK
- License: AMD SDK EULA (proprietary / not open source)
- Notes: ADL headers and libraries are owned by AMD and provided under their SDK EULA.
- We include/consume ADL for building and running this tool on AMD GPUs.

## nlohmann/json
- License: MIT
- Source: https://github.com/nlohmann/json
- License text: https://json.nlohmann.me/home/license/

## amildahl/amdddc-windows
- License: No explicit license file was found at time of writing.
- Source: https://github.com/amildahl/amdddc-windows
- Notes: Original CLI that demonstrated AMD + LG input switching over alternate I²C.
  This project rewraps the idea with a tray UI, hotkeys, and config.

## DDC/CI references (documentation only)
- ddcutil documentation/wiki
- General DDC/CI notes (e.g., VCP codes, timing)
- Used for reference; no source code copied.

## Qt 6 (optional desktop frontend)
- The modern desktop frontend dynamically links Qt Widgets, GUI, Core and Concurrent.
- Qt is available under commercial and open-source licenses; see https://www.qt.io/licensing/ and https://doc.qt.io/qt-6/licenses.html .
- Qt source is not vendored here. Include applicable Qt/runtime notices and satisfy redistribution obligations when packaging desktop builds. The legacy Windows executable and Linux CLI do not depend on Qt.
