# Super Peasant — player guide

Public preview for **Fallout 4 on Windows, runtime 1.11.240**, tested with Steam. Other executable versions and Fallout 4 VR are unsupported by this download.

The ZIP contains the mod only. Its top level represents the game's **Data** folder: `SuperPeasant.esp`, `F4SE`, `Scripts`, `Meshes`, `Textures`, and `Sound`. No developer installer is required.

## Requirements

- **F4SE 0.7.9** for Fallout 4 1.11.240: use the [F4SE team's instructions](https://www.nexusmods.com/fallout4/mods/42147). The [official site](https://f4se.silverlock.org/) confirms this version pairing.
- **Address Library for F4SE Plugins**, including `Data\F4SE\Plugins\version-1-11-240-0.bin`: get the matching version or All In One from the [author's files page](https://www.nexusmods.com/fallout4/mods/47327?tab=files).
- **Microsoft Visual C++ v14 Redistributable, x64**: install or repair it from [Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) if needed. The mod's DLL uses these libraries; many PCs already have them.

Check `Fallout4.exe → Properties → Details` for the game version. Dependencies are separate downloads. The ESP has `Fallout4.esm` as its only master.

Keep a pre-install save backup, including its matching `.f4se` co-save if present. A separate mod-manager profile is useful for trying a preview.

## Recommended: mod manager

1. Close the game and install the requirements.
2. Install this ZIP from an archive in Vortex or Mod Organizer 2. The detected Data content should contain `SuperPeasant.esp` directly beside the folders listed above.
3. Enable the mod and **SuperPeasant.esp**. Deploy changes if using Vortex. Enable loose files for the active profile using the Archive settings below if necessary.
4. Launch **F4SE through the manager**. MO2 must launch it through MO2 so its virtual files are visible.

Keep the complete mod together. Do not mix versions, compact its FormIDs, or flag this ESP as ESL for this preview.

## Manual installation

1. Close the game. Open the folder containing `Fallout4.exe`; Steam's installed-files browser can locate it on any drive.
2. Back up files this mod would overwrite, outside the game folder. Extract this mod's contents into **`<Fallout 4 folder>\Data`**. Check for `Data\SuperPeasant.esp` and `Data\F4SE\Plugins\SuperPeasantNative.dll`.
3. Merge these settings into your active `Documents\My Games\Fallout4\Fallout4Custom.ini`, preserving other settings. Documents may be redirected to OneDrive. With a mod manager, edit its active profile's INI instead.

```ini
[Archive]
bInvalidateOlderFiles=1
sResourceDataDirsFinal=

[Launcher]
bEnableFileSelection=1
```

4. Add `*SuperPeasant.esp` once to `%LOCALAPPDATA%\Fallout4\Plugins.txt`, preserving other entries. If the file does not exist, launch Fallout 4 normally to its main menu once and exit first. Mod-manager users should enable the plugin through their manager instead.
5. Launch `f4se_loader.exe` beside `Fallout4.exe`. Follow F4SE's own installation directions for its game-folder files; this mod's ZIP belongs in Data.

For a manual-install desktop shortcut, target your actual `f4se_loader.exe` and set **Start in** to the same game folder. For MO2, create the shortcut through its configured F4SE entry so the correct profile is used. Vortex provides an F4SE dashboard tool.

## First use

In a new game, finish the opening and leave Vault 111 first. He joins once the world is ready. Two **Peasant Whistles** appear under **Pip-Boy → Inventory → Aid**; use one to call him and add it to Favorites. Whistles replenish after use.

Activate him to choose **Follow me**, **Wait here**, **Come here**, **Send away**, or the carry/put-down commands. Send away is reversible with the whistle. The console alternative `cqf SP_Main Whistle` calls the same action.

Experimental extra prop pushing is off in the included settings. Back up `Data\F4SE\Plugins\SuperPeasantNative.ini` before changing advanced options.

## Update or remove

For an update, close the game and replace the complete mod, preserving a copy of your custom INI. **Do not use Remove mod (prepare) just to update or temporarily dismiss him.**

To prepare a used save for removal:

1. Put the player down if he is carrying you.
2. Choose **Remove mod (prepare)** and confirm **Remove him now**, or enter `cqf SP_Main PrepareUninstall` in the console.
3. Wait for **“Super Peasant removed. Save now, then uninstall the mod.”** Carry cleanup may defer completion.
4. Make a **new** manual save, keep the pre-install backup, and quit to the desktop.
5. Remove the mod through your manager. For manual installation, remove only its installed files listed in `MANUAL-FILE-LIST.txt` / `FILES.sha256.json`, restoring pre-install backups where appropriate, and remove only its plugin activation line.

Keep saves, co-saves and dependencies used by other mods. Do not remove shared folders or undo shared loose-file settings. If another mod changed an overlapping file, resolve that ownership before removing it.

Preparation is permanent in that save; reinstalling files will not make the whistle revive him there. It stops owned quests/effects but cannot guarantee a clean mid-playthrough save. **Returning to a pre-install save is the safest removal route.**

## Troubleshooting

- Confirm the exact runtime/dependency versions. Do not rename old DLLs or address databases to imitate a compatible version.
- `getf4seversion` in the game console confirms F4SE itself is running; it does not prove this plugin loaded.
- Check plugin activation, Data-level layout, loose-file settings and the manager's F4SE launch entry.
- Missing `MSVCP140` / `VCRUNTIME140` errors require Microsoft's x64 runtime, not DLL-download websites.
- Relevant loader logs normally live in the active `Documents\My Games\Fallout4\F4SE` folder. Report the game/mod versions and loader error; personal saves are not needed by default.

This preview has not been tested with every mod load order. Keep backups and report reproducible conflicts.
