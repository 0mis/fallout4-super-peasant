# Attribution and source

The Modded Code under EXCEPTIONS is Bethesda's Fallout4; the Modding Library is F4SE.

The original mod implementation is provided under GPL-3.0-or-later, with the additional permissions in EXCEPTIONS for linking to the modded game and modding libraries. This does not license the game, third-party characters, trademarks, or separately installed tools. Upstream notices remain in their source directories.

The corresponding native library source is included:

| Dependency | Pinned source | License |
|---|---|---|
| CommonLibF4 template | libxse/commonlibf4-template, e70c24ab6d1b12478a9fc09ff99aedb73d1f394a | GPL-3.0-or-later and its EXCEPTIONS |
| CommonLibF4 | libxse/commonlibf4, 16cff6870d92d0018e25c971a7bbd42d91f97871 | GPL-3.0-or-later and its EXCEPTIONS |
| CommonLib Shared | See the pinned source and original notices in deps/commonlibf4-template/lib/commonlibf4/lib/commonlib-shared | GPL-3.0-or-later and its EXCEPTIONS |
| spdlog 1.16.0 | deps/spdlog-1.16.0 | MIT; includes bundled dependency notices |

[CommonLibF4 license and exception explanation](https://github.com/libxse/commonlibf4/blob/16cff6870d92d0018e25c971a7bbd42d91f97871/README.md#license).

F4SE, Address Library, Fallout 4, the Creation Kit/Papyrus vanilla imports, Caprica, Xmake, Blender and PyNifly are external requirements. Their binaries and proprietary game files are not included. Download them from their own authors and follow their terms. PyNifly is used only when exporting the model; the model generator source is included.

Some physics/gameplay constants and numerical compatibility tables were measured or documented while studying Totally Accurate Battle Simulator. Their original provenance comments remain in the source. This project does not claim ownership of Landfall's game, characters, extracted art/audio, or decompiled game source. The model/audio are included on the maintainer's confirmation of redistribution permission and retain separate asset terms; see ASSET_LICENSE.md. No decompiled C# game files are included.

This is an unofficial fan project, unaffiliated with Bethesda, Landfall, or the One Punch Man rights holders.
