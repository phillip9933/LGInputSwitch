# Windows redistribution audit — 27 October 2026

**1.1.0 update:** This is the historical audit of the earlier candidate. The current
release removes the inherited AMD implementation and unused CLI, adds complete
application/Qt source and notices, supplies the signed Microsoft runtime installer,
and omits the optional Mesa/D3D compiler binaries. The Windows loader/packet path
is tested with fake DLLs. See [current release setup](windows-release.md).

This audit covers source at `a7268c1a79d95d3399f7818fd4263fe7e669c2b3`, the
legacy Visual Studio/CMake targets, and the actual modern Windows artifact from
[CI run 40345943184](https://github.com/phillip9933/LGInputSwitch/actions/runs/40345943184).
It records evidence and packaging requirements; it is not a legal clearance for
unexamined code or future dependencies. No upstream messages were sent.

## Upstream permission evidence changes the earlier assessment

The upstream tree at `amildahl/amdddc-windows` commit
`6a091856836365996ba7a2caa7c32bcf6c519906` has no explicit license file, and the
reviewed README/source files do not state a project license. However,
[upstream issue #3](https://github.com/amildahl/amdddc-windows/issues/3)
contains the maintainer's original explanation that LGInputSwitch wraps that code
and asks whether changes are needed because a license was missing. In
[the owner's reply on 19 November 2025](https://github.com/amildahl/amdddc-windows/issues/3#issuecomment-3554891167),
amildahl acknowledges the reuse, expresses no objection and thanks the maintainer.

That is affirmative written permission context. The previous statement that no
permission evidence was available was incomplete. Preserve the reply's URL,
author, date and context. It does **not** name MIT, AGPL, a version, sublicensing
terms or the scope of downstream rights. Do not present it as a standard
open-source license or automatically apply LGInputSwitch's new AGPL choice to
upstream-owned changes. Clarifying an explicit compatible grant would remove
that remaining ambiguity; replacing the affected upstream-specific implementation
is an alternative. A formal license file is not the only possible evidence of
permission, so absence of such a file alone is not proof of a prohibited use.

The upstream README credits
[AMD's DDCBlockAccess sample](https://github.com/GPUOpen-LibrariesAndSDKs/display-library/blob/master/Sample/DDCBlockAccess/DDCBlockAccessDlg.cpp).
Shared sample ancestry does not establish the license of every downstream change.
The local `amdddc/adl.cpp`, `adl.h`, `amdddc_core.cpp` and unused original CLI files
need that provenance distinction. The Visual Studio project also compiles inherited
`amdddc/settings.cpp`, whereas CMake omits that unused CLI parser.

## Required components and terms

| Component | Where used | Terms and distribution action |
| --- | --- | --- |
| LGInputSwitch project code | Both Windows frontends | AGPL-3.0-or-later for new project revisions. Include LICENSE.md, copyright/no-warranty information, exact corresponding application source and build scripts. Preserve separate third-party notices. Earlier MIT releases retain their original terms. |
| Bundled AMD ADL headers | Both Windows frontends | MIT, as stated in all three bundled headers. Preserve the AMD copyright and permission/disclaimer text in `licenses/AMD-ADL-MIT.txt`. No paid ADL SDK license is identified for these headers. |
| AMD driver DLLs | Loaded from the installed driver | They are not in the artifact. Require the user's AMD driver; do not add driver DLLs to the release under the headers' MIT license. |
| nlohmann/json 3.12.0 | Both Windows frontends | MIT; include `licenses/nlohmann-json-MIT.txt`, retain the notices in external/json.hpp and its embedded-component attribution. The upstream v3.12.0 docs identify the Hoehrmann decoder and Loitsch conversion code as MIT and Hedley as CC0-1.0. The header also credits the Abseil Authors under MIT. |
| Qt 6.8.3 libraries/plugins | Modern frontend only | Use the available LGPLv3 route, keep dynamic linking, supply LGPLv3 and GPLv3 texts, prominent Qt attribution, matching Qt source/build information and relevant third-party notices. Allow replacement/relinking and debugging of modified LGPL libraries. No commercial Qt purchase is inherently required when complying with the open-source route. |
| Mesa/LLVM software OpenGL | Modern deployed artifact | `opengl32sw.dll` is an additional third-party binary, not merely another Qt LGPL DLL. Preserve the notices for its exact build or omit this optional renderer after validating deployment without it. |
| Microsoft D3D compiler | Modern deployed artifact | `D3Dcompiler_47.dll` is governed by applicable Windows SDK redistribution terms, not AGPL/LGPL. Verify the exact redistributable origin/terms or omit it after deployment validation. |
| Microsoft C/C++ runtime | Static in legacy; DLL imports in modern | Follow the license and REDIST list for the actual Visual Studio toolchain. Use release redistributables only. The modern package needs an official VC++ runtime prerequisite/installer or permitted app-local runtime files. Do not distribute debug_nonredist files or relabel Microsoft code as AGPL. |

Sources: [bundled ADL header](../adl-sdk/include/adl_sdk.h),
[nlohmann v3.12.0 license](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT),
[embedded-code terms for that version](https://github.com/nlohmann/json/blob/v3.12.0/docs/mkdocs/docs/home/license.md),
[Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations),
[Qt LGPLv3 text](https://doc.qt.io/qt-6.8/lgpl.html),
[Visual Studio 2022 REDIST list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution),
[Windows SDK terms](https://learn.microsoft.com/en-us/legal/windows-sdk/license).
The runner uses windows-latest; record its actual Visual Studio installation and
license/redistributable version rather than assuming the image will always use
Visual Studio 2022.

## Actual modern Windows artifact findings

Artifact `12662060384`, `LGInputSwitch-Desktop-Windows-experimental`, belongs to the
revision above. Its ZIP SHA256 is
`4a451eead1f3f8b4690cc9894495f60f2043c5bfab972f742bfd054d9d845a3c`.
[The extracted file inventory](evidence/windows-artifact-a7268c1.json) records
sizes and SHA256 hashes of all 19 application/dependency files. Inspection was
static on Linux; none of the Windows executables or DLLs was executed.

The deployed module set is broader than the CMake link declarations: Core, Gui,
Widgets, **Network and Svg** are present, together with image/icon, Windows platform,
style, touch, network-information and Schannel/certificate plugins. The renderer
contains strings identifying Mesa 11.2.2 and LLVM 3.6.2. Qt's attribution page also
identifies Mesa, Khronos and Boost-licensed material; the exact prebuilt's LLVM
and other embedded notices still need to be collected. Version strings are
inventory evidence, not a complete provenance/license certificate.

The ZIP contains no LICENSE/COPYRIGHT/third-party notice files, Qt license texts,
application source archive or Qt corresponding source. It contains no VC++ runtime
DLLs or redistributable installer. PE imports from Qt6Core.dll include
MSVCP140.dll, MSVCP140_1.dll, VCRUNTIME140.dll and VCRUNTIME140_1.dll. A developer
runner already having those DLLs does not prove that an extracted app works on a
fresh Windows installation.

Do not infer OpenSSL obligations from the presence of QtNetwork: this artifact
contains Schannel backends, with no OpenSSL DLLs. KDE and ddcutil are Linux-only
here. Intel IGCL and NVIDIA NVAPI are future backends and are not shipped.

Version-specific sources:
[Qt third-party licensing](https://doc.qt.io/qt-6.8/licenses-used-in-qt.html),
[Qt Windows deployment](https://doc.qt.io/qt-6.8/windows-deployment.html),
[software OpenGL behavior](https://doc.qt.io/qt-6.8/windows-graphics.html),
[Mesa attribution](https://doc.qt.io/qt-6.8/qt-attribution-llvmpipe.html).
The online 6.8 docs now describe a newer patch; use the exact **6.8.3** source
archives/attributions for this artifact, not newer libraries' copyright inventories.

## Release work remaining

For the legacy executable, package AGPL application source/build scripts and the
AMD/json notices, and record the upstream permission context and how its scope
is resolved. Its release build uses /MT and does not redistribute Qt. This audit
does not replace the outstanding real Windows/hardware acceptance checks.

For the modern executable, first decide a minimal required deployment. The
current raster Widgets app has no explicit OpenGL/Qt Quick feature; evaluate
windeployqt's documented `--no-opengl-sw` and `--no-system-d3d-compiler` options,
then run clean-machine tests before dropping those files. Do not silently remove
plugins or libraries based on filename alone.

For whatever remains, ship project notices and exact application source, Qt
LGPLv3/GPLv3 texts and attribution, all matching module/third-party notices,
corresponding dependency source/build information, and the chosen Microsoft
runtime installation instructions/terms. Preserve DLL replaceability. Generate a
final manifest and checksums from the final bundle, then validate installation on
a machine without developer Qt/Visual Studio paths. windeployqt copies dependencies;
it is not proof that a license-complete release was assembled.

Substantial Codex assistance was used for this audit. It changes documentation
and evidence only; no runtime deployment behavior or public release was changed.
