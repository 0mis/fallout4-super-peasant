# Public binary metadata

The player download uses the tested native implementation and Papyrus instructions with private build-path metadata removed.

1. `scripts/Sanitize-Metadata.py` parses FO4 PEX3.9 and the DLL's PE debug directory. It rewrites the three PEX source/user/machine header strings, the one native-script description that contains a source path, and the RSDS/PDB filename field. The complete Papyrus executable tails, string-index uses and every non-whitelisted DLL byte are compared before and after. `docs/BINARY_METADATA.json` records the checks.
2. `scripts/Sanitize-Diagnostics.py` exposes a separately reviewed, hash-pinned transformation for19 `std::source_location` diagnostic filename fields in non-executable `.rdata`. It replaces each absolute source path with its existing basename and NUL padding. Addresses, executable bytes, PE layout, other data and string-table indices do not move. `docs/DIAGNOSTIC_METADATA.json` records the additional comparison. Diagnostic log filenames become shorter; no gameplay instructions change.

The second transformation accepts only its exact pinned input. It deliberately refuses other builds rather than guessing which binary strings are diagnostic-only. The code and public receipts are included for audit/reproduction. The diagnostic helper imports the metadata parser next to it.

The public native DLL hash is `b9dc1c9e4e8c767366d51ae4393171c9d516b4ee9df6627550e20af82be34f4a`. The earlier local tested DLL's hash is `13c6f7162c6c9c023280b92571c510d4f4ee27e205ae398364a49d034615ca54`. They have the same executable section bytes. Updated full-file hashes for all45 installed mod files are in `docs/RELEASE_FILES.json` and the player's `FILES.sha256.json`.

The local installation was not replaced merely to remove publication metadata. The source, public package, private build and prior rollback remain distinguishable by their recorded hashes.
