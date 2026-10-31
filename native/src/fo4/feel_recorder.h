#pragma once
// OWNER: FEEL.  Per-frame "feel" recorder for the TABS -> Fallout 4 side-by-side comparison
// (work\feel\FO4_RECORDER.md; comparator work\feel\tools\compare.py).
//
// Off unless [Tabs] feel.enable = 1. While a scenario runs ([Tabs] feel.scenario = N > 0, set by the ini or at the
// exact release moment by Papyrus: SuperPeasantNative.SetTunable("feel.scenario", N)), it writes FEEL v1 records
// as log lines "SPN|FEEL|..." into SuperPeasantNative.log (v2_run copies every SPN| line into the run's spn.txt):
//   M,<key>,<value>   meta (game, scenario, desc, units, up, upm, s, arm_len_l/r, hz)
//   HF,... / F,...    frame header / one frame every 1/feel.hz s (metres, Z up, t = s since the scenario started)
//   HE,... / E,...    event header / events: swing, hit, launch (victim's peak velocity 0-0.35 s after a hit), kill
//   END,S<N>
// Reads only public state: runtime::GetSnapshot() (sim), rendered bones (fo4::GetBoneWorldPos), one Pick per frame.
// Writes nothing to the game. THREADING: game thread (called from runtime::GameTick, under its tick lock).
//
// Tunables ([Tabs], all optional): feel.enable 0 | feel.scenario 0 | feel.always 0 (record continuously as "LIVE")
//   | feel.hz 60 (10..120) | feel.maxSeconds 90 | feel.groundRay 1 | feel.launchWindow 0.35 | feel.killWindow 4

#include <cstdint>

namespace sp::feel
{
	// Required: once per game frame, at the end of sp::runtime::GameTick (after Publish), a_dt = that frame's dt.
	void Tick(std::uint64_t a_frameNo, float a_dt);

	// Optional exact event hooks (fist side + exact victim). Without them the recorder derives swing / hit events from
	// the snapshot counters (victim = the snapshot target at that frame, fist = -1).
	void OnSwing(int a_fist, std::uint32_t a_target);
	void OnHit(int a_fist, std::uint32_t a_victim, double a_k, double a_flingSpeedU, double a_opmScale = 1.0);  // STEP 7.6 FIX: a_opmScale = punch.launchScale on a One Punch Man killing punch (the launch event is divided by it: FEEL compares the TABS base launch; the real one is the launchraw event)

	// Optional: kPreLoadGame / kNewGame. Ends a running recording ("END") and forgets victims.
	void Reset();
}
