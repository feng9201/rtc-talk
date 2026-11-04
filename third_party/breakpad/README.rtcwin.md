# Bundled Breakpad client

Source: user-built `D:\google\breakpad_sdk`, Breakpad commit `9a2ad675`,
x64 MSVC v143. Original build metadata and SDK usage are retained alongside this
file. Paths in those original records describe provenance, not build dependencies.

Bundled: the SDK header closure and `exception_handler.lib`,
`crash_generation_client.lib`, `common.lib` for Release (/MT) and Debug (/MTd).
Upload libraries, upload tools, server library, and the SDK DLL are not bundled.
All project build paths resolve relative to this repository.

The /MT `rtcwin_crash_handler.dll` owns Breakpad objects and exposes a C ABI to
the /MD Qt executable. No C++ objects or allocators cross that boundary.
Copyright and redistribution terms are in the SDK headers; see LICENSE.
