#pragma once
// ROUND5 B (FEEL): compile switch for the TABS feel recorder (fo4/feel_recorder.cpp, work\feel\FO4_RECORDER.md).
// 1 = TEST build only (the recorder + its runtime hooks are compiled in; still off unless [Tabs] feel.enable = 1).
// 0 = SHIP build: no recorder code at all. Shipped packages are always built with 0.
#define SP_FEEL 0
