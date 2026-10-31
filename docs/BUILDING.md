# Building from source

Windows x64 is the supported platform. The tested game target is Fallout 4 1.11.240 with F4SE 0.7.9 and its matching Address Library database. The native layout and addresses are version-specific. Other runtimes, VR, consoles, and the Microsoft Store version are not supported by this release.

## Native plugin and desktop tests

Install Visual Studio 2026 Build Tools (the tested toolchain) with the C++ workload and Windows SDK, and Xmake 3.0 or newer. The source of the pinned CommonLibF4 libraries and spdlog is included under deps. Xmake resolves its build package for spdlog 1.16.0; no game files are required to compile the native plugin or pure desktop tests.

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Native.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Native.ps1 -Target sp_tests
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Native.ps1 -Target scan_tests
Set-Location native
& .\build\windows\x64\releasedbg\sp_tests.exe
& .\build\windows\x64\releasedbg\scan_tests.exe
```

Keep the `cost_probe` option disabled for player packages. The native output is native/build/windows/x64/releasedbg/SuperPeasantNative.dll. No build command installs or launches the game.

## Papyrus

Obtain Caprica, the Fallout 4 vanilla script sources/Institute_Papyrus_Flags.flg from your own Creation Kit installation, and the F4SE 0.7.9 source imports separately. Python 3.11+ is required for the assembly check. Pass actual local paths:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-Papyrus.ps1 `
  -Caprica 'C:\Tools\Caprica\Caprica.exe' `
  -VanillaSources 'D:\Papyrus\Vanilla' `
  -F4seSources 'D:\F4SE\Data\Scripts\Source' `
  -Python python
```

The script compiles the nine canonical runtime sources in native/papyrus and native/papyrus_scripts/SuperPeasant in strict mode, writes PEX and assembly into artifacts/papyrus, and checks for variable clobbers. Test Runner/Golden sources are available for development but are never part of the player package. Vanilla imports precede F4SE imports deliberately.

## ESP game plugin

The declarative source is native/papyrus_scripts/contract.json. The stdlib-only tools under esp read the user's own Fallout4.esm and construct the mod's records. No base-game ESM or index is distributed. From the repository root:

```powershell
python -B -I esp/vanilla_index.py --contract native/papyrus_scripts/contract.json `
  --esm 'D:\SteamLibrary\steamapps\common\Fallout 4\Data\Fallout4.esm' `
  --out artifacts/vanilla_index.json
python -B -I esp/build_esp.py --contract native/papyrus_scripts/contract.json `
  --esm 'D:\SteamLibrary\steamapps\common\Fallout 4\Data\Fallout4.esm' `
  --index artifacts/vanilla_index.json --out-main artifacts/Data/SuperPeasant.esp `
  --report artifacts/esp-build.json --only-main
```

The contract's descriptive installation paths are placeholders, not instructions to write to those paths. The builder has explicit input/output arguments. See the public release manifest for artifact hashes and the validation notes for what was actually tested.

## Numerical model inputs

The editable numerical compatibility input is native/tools/tabs_constants.json. Run gen_tabs_constants.py and gen_body_model.py from native/ with that input to regenerate their headers. The authored cape bind input is assets/cape_bind.json; use `python -B -I native/tools/gen_cape_model.py assets/cape_bind.json native/src/core/cape_model.gen.h` from the repository root. These files are included; no extracted game mesh is needed to regenerate the numerical headers.

## Assemble a player archive

The credited model/texture/audio payload is included in assets/Data. The editable fitted/costumed Blender files and local authoring code are in assets/model. Those assets retain the terms in ASSET_LICENSE.md. To assemble a Data-root ZIP from rebuilt native/Papyrus/ESP outputs:

```powershell
python -B -I scripts/Make-Mod-Zip.py `
  --dll native/build/windows/x64/releasedbg/SuperPeasantNative.dll `
  --scripts artifacts/papyrus/runtime/Scripts `
  --esp artifacts/Data/SuperPeasant.esp `
  --out release/SuperPeasant-rebuilt.zip
```

Rebuilt artifacts have their own hashes and need their own runtime validation. The shipped public binary additionally has the reviewed privacy transformations described in BINARY_METADATA.md. No compiler/tool/game binaries or personal save are included in the source archive.
