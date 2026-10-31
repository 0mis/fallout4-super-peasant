#pragma once
// OWNER: STEP 7.5 (TABS motion trails). Engine glue of core/trails (round6\steps\s7.5\HOOKS.md FINAL, CORE.md).
//
// Two ribbons (his left and right forearm = exactly the two TABS bracer TrailRenderers) as our own BSDynamicTriShape
// geometry under OUR root node, a child of the game's main scene root ShadowSceneNode[0] (never his body 3D: no fade,
// hit shaders, 3D resets or his root bound on them; culled by their own bounds indoors and out). Made ONCE per session
// on the main thread (kGameDataReady, or kPostLoadGame if look.trails was turned on later) with the engine's own
// default effect shader (unlit, opaque, vertex colours = the TABS gold -> white gradient baked per section), then only
// hidden / shown; never re-attached in play. Every P5 pump (outside the render window) the 2 x sections positions are
// rebuilt from his DRAWN forearm bones (ForeArm1 -> Hand at trails.emitAlong; placed on his root's final transform of
// the frame, so the head sits on the forearm the next render shows), packed as 4 x half relative to the head (the
// engine's dynamic format), copied under the shape's own spin lock (memcpy only), the shape moved to the head, its
// bound set, the root updated (downward pass only). No hook, no vtable patch: engine CALLS only, every function /
// slot / global checked at load (any mismatch: one SPN|TRAIL|chk FAIL line, trails off for the session, nothing
// else changes), each entry point behind its own SEH crash shield (fault: one SPN|TRAIL|E line + a bookmark /
// black-box dump, trails off for the session, the lock is released if held, the game keeps running).
// [Tabs] look.trails 1 = on, 0 = off (hidden; code default 0 = before step 7.5).
#include <cstdint>
#include <string>

namespace RE
{
	class Actor;
}

namespace sp::fo4::trails
{
	void OnGameDataReady();  // main thread: load checks + make (only when look.trails is on)
	void OnPostLoadGame();   // main thread: make, if look.trails was turned on after the start
	void OnLoad();           // kPreLoadGame / kNewGame (main thread, tick lock): hide + clear the history
	// P5, every pump (under the runtime tick lock, after skeleton::FrameUpdate). a_me = him, or nullptr while loading /
	// when there is no peasant (the ribbons are hidden). a_tickDt = this pump's game tick dt (the runtime's own clock,
	// hooks.cpp P5TaskBody 'live' / tickDt), or -1 when this pump is not a new tick (the frame's second pump, menus).
	void OnPump(RE::Actor* a_me, std::uint64_t a_frame, double a_tickDt);
	bool        Active();    // made, shown, no fault
	std::string Describe();  // one line (on / made / fault / shown / points / length / prediction / cost)
}
