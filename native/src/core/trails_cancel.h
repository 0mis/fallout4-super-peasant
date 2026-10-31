#pragma once
// OWNER: STEP 7.5 fix lane 3 (REVIEW 4, "the Weather-node drift").  Pure C++ (no game headers, desktop-testable:
// tests/test_s75fix3.inc).
//
// What the engine does (RE of Fallout4.exe 1.11.240, fo4re.py 2026-10-19 14:30, re\out\20261019_143008_*):
//   DrawWorld::Begin (rva 0x21F0AC0; Main::DrawWorld_And_UI <- Main::Swap <- Main::OnIdle, the MAIN thread, once per
//   rendered frame, after the game update) writes the Sky root's and the Weather root's local.translate = the world
//   translate of DrawWorld's camera (DrawWorld::SetCamera(WorldRootNode[0x28]) in Main::DrawWorld_PreRender =
//   RE::Main::WorldRootCamera()) and runs NiAVObject::Update on both: a full downward pass. NiNode::UpdateDownwardPass
//   (rva 0x16BEFE0) updates EVERY child (no selective-update or other flag skips one), so our trail root under the
//   Weather node is recomputed there: root.world = camera + root.local. No other code moves the Weather node per frame
//   (Precipitation::Update / Sky::Update only attach / detach / update their own effects).
// So the root's local must cancel the camera AT THAT RENDER. Step 7.5 cancelled the Weather node's world at our pump =
// the camera of the PREVIOUS render: every drawn frame both ribbons were shifted by one frame of camera motion (the
// stat line's drift= 0.1-5 u in fights, 20-320 u on camera jumps). trails.cancel 1 cancels the camera NOW (the camera
// the next DrawWorld::Begin copies, when it is final at our last pump); if it is not final yet the answer equals the
// old one (the Weather node sits on the previous render's camera), so it is never worse.
// DriftMeter measures it in game: at the first pump after a render (the renderer's frame counter moved) the drawn
// frame's offset is |parent world now - the value we cancelled|, for the method in use and for the other one (A/B in
// one run).
#include "core/math.h"

#include <algorithm>
#include <cstdint>

namespace sp::trails
{
	using sp::math::Vec3;

	// a_mode 1 + a camera that passed its load check: cancel the camera now; else the parent's world now (step 7.5)
	inline Vec3 CancelTarget(int a_mode, bool a_camOk, const Vec3& a_parentNow, const Vec3& a_camNow, bool* a_usedCam = nullptr)
	{
		const bool cam = a_mode == 1 && a_camOk && sp::math::IsFinite(a_camNow);
		if (a_usedCam) *a_usedCam = cam;
		return cam ? a_camNow : a_parentNow;
	}

	struct DriftMeter
	{
		bool          have{ false };
		Vec3          used{}, alt{};          // what the last pump cancelled (the method in use / the other one)
		std::uint32_t frameCtr{ 0 };          // the renderer frame counter at that pump
		double        maxUsed{ 0 }, maxAlt{ 0 };  // the stat window's largest drawn offset (u)
		std::uint64_t frames{ 0 }, over{ 0 };     // drawn frames measured / of them with an offset > the log threshold
		double        lastUsed{ -1 }, lastAlt{ -1 };

		void Reset() { have = false; }
		void ResetWindow()
		{
			maxUsed = maxAlt = 0;
			frames = over = 0;
		}
		// one pump, BEFORE this pump's own cancel. a_parentNow = the parent's world now. Returns true when a render ran
		// since the last pump (lastUsed / lastAlt = the drawn frame's offsets; else both -1). a_shown: the ribbons were
		// drawn that frame (only those frames count in the window).
		bool Measure(const Vec3& a_parentNow, std::uint32_t a_frameCtr, bool a_shown, double a_overU)
		{
			lastUsed = lastAlt = -1;
			if (!have || a_frameCtr == frameCtr || !sp::math::IsFinite(a_parentNow)) return false;
			lastUsed = sp::math::Length(a_parentNow - used);
			lastAlt = sp::math::Length(a_parentNow - alt);
			if (a_shown) {
				++frames;
				maxUsed = std::max(maxUsed, lastUsed);
				maxAlt = std::max(maxAlt, lastAlt);
				if (lastUsed > a_overU) ++over;
			}
			return true;
		}
		// after this pump's cancel: remember what was cancelled (both methods) and the frame counter
		void Note(const Vec3& a_used, const Vec3& a_alt, std::uint32_t a_frameCtr)
		{
			used = a_used;
			alt = a_alt;
			frameCtr = a_frameCtr;
			have = sp::math::IsFinite(a_used) && sp::math::IsFinite(a_alt);
		}
	};
}
