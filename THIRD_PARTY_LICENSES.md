# Third-Party Licenses

This project incorporates or was inspired by the following:

## AMD Display Library (ADL) SDK
- License: MIT for the bundled `adl-sdk/include/` headers, as stated in each header.
- Copyright (c) 2016–2022 Advanced Micro Devices, Inc.
- Preserve the full notices in these headers and `licenses/AMD-ADL-MIT.txt`.
- AMD driver runtime DLLs are loaded from the installed driver and are not bundled.

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

## KDE Frameworks KGlobalAccel (optional KDE shortcuts)
- Dynamically linked when available; no source is vendored.
- Configurable global shortcuts for KDE Plasma: https://api.kde.org/kglobalaccel.html .
- Preserve applicable KDE Frameworks license notices when packaging this dependency.

The project AGPL license does not relicense any of these dependencies.
The Linux binary links distribution-provided Qt/KDE; those runtimes are not bundled.
