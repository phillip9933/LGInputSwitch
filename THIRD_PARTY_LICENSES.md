# Third-Party Licenses

This project incorporates or was inspired by the following:

## AMD Display Library (ADL) SDK
- License: MIT for the bundled `adl-sdk/include/` headers, as stated in each header.
- Copyright (c) 2016–2022 Advanced Micro Devices, Inc.
- Preserve the full notices in these headers and `licenses/AMD-ADL-MIT.txt`.
- AMD driver runtime DLLs are loaded from the installed driver and are not bundled.

## nlohmann/json
- License: MIT (bundled version 3.12.0); full text: `licenses/nlohmann-json-MIT.txt`.
- Source: https://github.com/nlohmann/json/tree/v3.12.0
- Preserve embedded attribution: Niels Lohmann, Björn Hoehrmann, Florian Loitsch,
  the Abseil Authors, and Hedley/Evan Nemerson (CC0-1.0 in upstream documentation).
- Version-specific terms: https://github.com/nlohmann/json/blob/v3.12.0/docs/mkdocs/docs/home/license.md

## amildahl/amdddc-windows
- License: No explicit license file was found at time of writing.
- Source: https://github.com/amildahl/amdddc-windows
- Historical source/inspiration for the original Windows version.
- Version 1.1.0 removes the inherited implementation and unused CLI; the current
  backend and protocol encoder are newly written against documented APIs/data.
- Written no-objection evidence: the owner replied to the maintainer’s reuse notice
  on 19 November 2025: https://github.com/amildahl/amdddc-windows/issues/3#issuecomment-3554891167 .
- The reply is retained as historical permission context, not a named MIT/AGPL grant.
  No license is assigned to upstream-owned code by this project.

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

## Windows release inventory

See [Windows redistribution audit](docs/windows-licensing.md) for the inspected
Windows artifact, exact DLL inventory, upstream permission evidence and remaining
Qt/Mesa/Microsoft notice/source/runtime packaging work. That audit describes the earlier candidate; the completed 1.1.0 bundle is documented
in [Windows release setup](docs/windows-release.md).
