# Nightzim BoomMod X - source and current build

Work-in-progress tree of the volumetric line (see [BoomModX.md](../BoomModX.md) for how it works and [CHANGELOG.md](../CHANGELOG.md) for what changed).

- `addon/` - ReShade add-on (`addon.cpp`, `matcap.inc`, `sound.inc`, `nzshared.h`), the shader `NightzimBoom.hlsl` and `bc7/` (bc7enc by Richard Geldreich, Jr., MIT / public domain).
- `hook/` - DCS Lua hook (`nightzim_boom_hook.lua`) and its helper DLL source (`nzboom_lua.cpp`).
- `installer/` - installer / manager (`manager.cpp`, resources).
- `bin/` - current test build (add-on, helper DLL, installer, default ini). The explosion volumes (`*.nzv`, about 1 GB) are only in the release zip.

Build (MinGW-w64, cross-compiled):

```
x86_64-w64-mingw32-g++ -std=c++17 -O2 -shared -static -I<reshade>/include addon.cpp bc7/bc7enc.cpp -o NightzimBoom.addon64 -ld3d11 -lwinmm
x86_64-w64-mingw32-windres app.rc -O coff -o app.res
x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -mwindows -static manager.cpp app.res -o BoomModX_Instalador.exe -lbcrypt -lshell32 -lole32 -lcomctl32 -luuid -lversion -lgdiplus -lshlwapi -ldwmapi -luxtheme -lgdi32
```

The build in `bin/` is newer than the X v1.0b release and still in testing.
