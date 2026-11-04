# Third-Party Licenses

This project incorporates or was inspired by the following:

## AMD Display Library (ADL) SDK
- License: MIT for the bundled `adl-sdk/include/` headers, as stated in each header.
- Copyright (c) 2016–2022 Advanced Micro Devices, Inc.
- Preserve the full notices in these headers and `licenses/AMD-ADL-MIT.txt`.
- AMD driver runtime DLLs are loaded from the installed driver and are not bundled.

## nlohmann/json (standalone Linux CLI)
- The standalone Linux CLI uses this parser; the desktop app and its settings importer use Qt JSON.
- License: MIT (bundled version 3.12.0); full text: `licenses/nlohmann-json-MIT.txt`.
- Source: https://github.com/nlohmann/json/tree/v3.12.0
- Preserve embedded attribution: Niels Lohmann, Björn Hoehrmann, Florian Loitsch,
  the Abseil Authors, and Hedley/Evan Nemerson (CC0-1.0 in upstream documentation).
- Version-specific terms: https://github.com/nlohmann/json/blob/v3.12.0/docs/mkdocs/docs/home/license.md

## amildahl/amdddc-windows
- Upstream has no named project license. This entry preserves historical credit;
  its implementation is not shipped in version 1.1.0.
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
- The Windows portable app statically links Qt Widgets, GUI, Core, Concurrent and Network. Linux uses the distribution’s shared Qt libraries.
- Qt is available under commercial and open-source licenses; see https://www.qt.io/licensing/ and https://doc.qt.io/qt-6/licenses.html .
- Qt source is available at the pinned official URL in packaging/windows/qt-sources.json and release notes. The Windows EXE embeds license/attribution and rebuild instructions, exportable with --licenses. The standalone Linux CLI does not depend on Qt. The retired Windows frontend is no longer built.

## KDE Frameworks KGlobalAccel (optional KDE shortcuts)
- Dynamically linked when available; no source is vendored.
- Configurable global shortcuts for KDE Plasma: https://api.kde.org/kglobalaccel.html .
- Preserve applicable KDE Frameworks license notices when packaging this dependency.

## Microsoft C++ runtime (Windows)
- Portable builds statically link the Microsoft C++ runtime from the Visual Studio toolchain. Copyright Microsoft Corporation.
- This component retains Microsoft’s terms; it is not relicensed under the project AGPL. See [Microsoft runtime terms](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/) and [redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).
- Runtime updates require rebuilding the portable executable with the updated toolchain; no separate runtime installer is bundled.

The project AGPL license does not relicense any of these dependencies.
The Linux binary links distribution-provided Qt/KDE; those runtimes are not bundled.

## Windows release inventory

See [Windows redistribution audit](docs/windows-licensing.md) for the inspected
Windows artifact, exact DLL inventory, upstream permission evidence and remaining
Qt/Mesa/Microsoft notice/source/runtime packaging work. That audit describes the earlier candidate; the completed 1.1.0 bundle is documented
in [Windows release setup](docs/windows-release.md).
