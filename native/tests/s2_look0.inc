// OWNER: TESTS lane, round 6 step 2 (engine probe pack + the 5 RE bug fixes). Desktop acceptance cases, never shipped.
// Included by tests/s2_tests.cpp (standalone exe) and by tests/test_core.cpp (sp_tests). Needs CHECK / g_fail from the host.
//
//   S2-A  (a) hit sink reads HitData: a mock DoHitMe TESHitEvent (usesHitData, null raw target / cause) records a kind-0
//         hit; the old sink dropped it (U41). Closed loop A-L1 on the real scan rules: a scene raider's melee hits on the
//         player are band-D evidence the SAME frame and lift its soft vetoes; Danse's stray melee hits on the player are
//         recorded but never make Danse a target (USER RULE companions); the player's own melee hit on a settler is a
//         quarrel record (kind 3), never a target; one bullet arriving twice keeps one attacker.
//   S2-B  (b) the 3 per-use masks recomputed from re\scripts\collision\coll_matrix.json (tests/s2_coll_matrix.json copy):
//         sight = L_LOS row minus 5, 6, 7, 30, 32, 53; walls = sight + 3, 26, 27, 28; floor = walls + 31; layers 48-54 and
//         56 absent, 55 the only data-defined blocker; hits classified by the low 7 bits of the shape filter info. Closed loop
//         B-L1 (fake engine picks): picks per logical ray <= 1.1 with the masks (old path up to 5); trigger / spell /
//         detection volumes no longer block sight ("fake walls"), glass blocks him but not his sight, bodies never block.
//   S2-C  (c) refused picks: P0 rays (sweep guard, Wings) never accept a refusal; any other refused pick = blocked. Closed
//         loop C-L1: a dash at a wall with the frame's pick budget used up never reports a segment through the wall clear.
//   S2-D  (d) continuity events: a 0.24 s hitch at 6300 u/s with v_cmd known = no event; an outdoor cell change = no event;
//         a 2000 u MoveTo = exactly one event; worldspace / interior change and a flagged relocation = an event, our own
//         flagged jump never. Closed loop D-L1 on the REAL TABS body (dash chain, 30-240 fps +-20 % jitter, hitches, cell
//         borders): 0 false events; the scripted MoveTo / fast travel exactly 1 each.
//   S2-E  (e) freeze gate: a frozen clock gives 0 ticks. Closed loop E-L1 with the REAL tick::P6Clock and the REAL body: 3 s of
//         Main::freezeTime (P6 stops, the pump runs): 0 ticks, his sim bit-identical (old rule: the fallback kept ticking);
//         a pause menu 0 ticks; after the freeze no backlog; an unbound / unloaded peasant keeps the fallback (old behaviour).
//   S2-H  hook load checks: the 4 call sites (real bytes of Fallout4.exe 1.11.240, hook_sites.out) and the 4 VTABLE_Actor
//         slots pass; a wrong opcode, a re-pointed rel32 (another mod's detour) or a moved slot is refused (old path).
#include "s2_api.h"
#include "core/body.h"
#include "core/tickclock.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace s2_tests
{
	namespace O = s2oracle;
	namespace A = s2api;
	namespace scan = sp::fo4::scan;
	using sp::math::Vec3;

	struct S2Floor final : sp::body::IWorld
	{
		double y = 0;
		bool   Raycast(const Vec3& f, const Vec3& d, double maxDist, sp::body::RayHit& out) override
		{
			out = {};
			if (d.y >= 0 || f.y < y) return false;
			const double t = (f.y - y) / -d.y;
			if (t > maxDist) return false;
			out = { true, t, f + d * t, { 0, 1, 0 } };
			return t > 0;
		}
	};

	inline std::string HereDir()
	{
		std::string p = __FILE__;
		return p.substr(0, p.find_last_of("/\\"));
	}

	// ============================================================================================== S2-A (a) hit sink
	inline void HitSinkUnits()
	{
		O::Group g;
		g.player = 0x14;
		g.him = 0xFF000802;
		g.teammates[0] = 0x0005DE4D;  // a companion (Danse-like)
		g.nTeammates = 1;
		const std::uint32_t raider = 0xFF001234, settler = 0xFF0055AA;
		scan::HitRec r;

		// A1: the acceptance case: usesHitData, null raw target / cause -> a kind-0 hit with cause = the aggressor
		O::HitEv melee;
		melee.usesHitData = true;
		melee.hdTarget = g.player;
		melee.hdAggressor = raider;
		const bool a1 = A::ClassifyHit(melee, g, true, r);
		CHECK(a1 && r.kind == scan::HitKind::kPlayer && r.cause == raider && r.target == g.player && !r.fallback,
			"S2-A1 [%s] a DoHitMe event (usesHitData, null target) records a kind-0 hit from its aggressor (got %d kind %d cause %08X)", A::Impl(), a1,
			static_cast<int>(r.kind), r.cause);
		// A2: key off = the old sink: dropped (U41 reproduced: melee attackers of the player were invisible)
		CHECK(!A::ClassifyHit(melee, g, false, r), "S2-A2 [%s] look.hitSinkHitData 0 keeps the old behaviour (the DoHitMe event is dropped)", A::Impl());
		// A3: no HitData and null raw refs -> nothing invented
		O::HitEv empty;
		CHECK(!A::ClassifyHit(empty, g, true, r), "S2-A3 [%s] an event with no refs and no HitData is dropped", A::Impl());
		O::HitEv noHd = melee;
		noHd.usesHitData = false;
		CHECK(!A::ClassifyHit(noHd, g, true, r), "S2-A3 [%s] HitData fields are ignored unless usesHitData is set", A::Impl());
		// A4: the old-style event (raw refs set) is read as before, with HitData present too
		O::HitEv bullet;
		bullet.rawTarget = g.player;
		bullet.rawCause = raider;
		bullet.usesHitData = true;
		bullet.hdTarget = 0xDEAD;
		bullet.hdAggressor = 0xBEEF;
		CHECK(A::ClassifyHit(bullet, g, true, r) && r.cause == raider && r.target == g.player, "S2-A4 [%s] raw refs win over HitData (cause %08X)", A::Impl(), r.cause);
		CHECK(A::ClassifyHit(bullet, g, false, r) && r.cause == raider, "S2-A4 [%s] the old-style event is unchanged with the key off", A::Impl());
		// A5: the other kinds through HitData
		O::HitEv onHim = melee;
		onHim.hdTarget = g.him;
		CHECK(A::ClassifyHit(onHim, g, true, r) && r.kind == scan::HitKind::kHim, "S2-A5 [%s] melee on him = kind 1", A::Impl());
		O::HitEv onMate = melee;
		onMate.hdTarget = g.teammates[0];
		CHECK(A::ClassifyHit(onMate, g, true, r) && r.kind == scan::HitKind::kTeammate, "S2-A5 [%s] melee on a teammate = kind 2", A::Impl());
		O::HitEv byPlayer = melee;
		byPlayer.hdTarget = settler;
		byPlayer.hdAggressor = g.player;
		CHECK(A::ClassifyHit(byPlayer, g, true, r) && r.kind == scan::HitKind::kPlayerHit && r.target == settler, "S2-A5 [%s] the player's melee on an actor = kind 3 (quarrel rule)",
			A::Impl());
		O::HitEv playerOnHim = byPlayer;
		playerOnHim.hdTarget = g.him;
		CHECK(!A::ClassifyHit(playerOnHim, g, true, r) || r.kind == scan::HitKind::kHim,
			"S2-A5 [%s] the player hitting him is never a kind-3 quarrel record", A::Impl());
		// A6: degenerate events
		O::HitEv self = melee;
		self.hdAggressor = g.player;
		CHECK(!A::ClassifyHit(self, g, true, r), "S2-A6 [%s] aggressor == target is dropped", A::Impl());
		O::HitEv noAgg = melee;
		noAgg.hdAggressor = 0;
		CHECK(!A::ClassifyHit(noAgg, g, true, r), "S2-A6 [%s] a null aggressor is dropped", A::Impl());
		O::HitEv trap = melee;
		trap.causeIsActor = false;
		CHECK(!A::ClassifyHit(trap, g, true, r), "S2-A6 [%s] a non-actor cause (trap, mine) on the player is no attacker", A::Impl());

		// A-X differential (core vs oracle) on generated events
		if (A::kHaveCore) {
			std::mt19937 rng(41);
			const std::uint32_t ids[] = { 0, g.player, g.him, g.teammates[0], raider, settler };
			int diff = 0;
			for (int i = 0; i < 20000; ++i) {
				O::HitEv e;
				e.rawTarget = ids[rng() % 6];
				e.rawCause = ids[rng() % 6];
				e.usesHitData = rng() & 1;
				e.hdTarget = ids[rng() % 6];
				e.hdAggressor = ids[rng() % 6];
				e.causeIsActor = (rng() % 8) != 0;
				e.targetIsActor = (rng() % 8) != 0;
				const bool key = rng() & 1;
				scan::HitRec a, b;
				const bool ka = A::ClassifyHit(e, g, key, a), kb = O::Classify(e, g, key, b);
				if (ka != kb || (ka && (a.kind != b.kind || a.cause != b.cause || a.target != b.target))) ++diff;
			}
			CHECK(diff == 0, "S2-AX core ClassifyHit == oracle on 20000 generated events (%d differ)", diff);
		}
	}

	// A-L1: the closed loop (U41 + the user's companion rule). 120 fps, 8 s.
	struct LoopActor
	{
		scan::ActorFacts f;
		Vec3             pos{};
	};
	struct HitLoopOut
	{
		int    raiderHits{ 0 }, raiderEvidenceSameFrame{ 0 }, raiderAllowedFrames{ 0 }, firstHitFrame{ -1 }, firstAllowedFrame{ -1 };
		int    danseHits{ 0 }, danseAllowedFrames{ 0 }, danseRecords{ 0 };
		int    settlerAllowedFrames{ 0 }, quarrelRecords{ 0 };
		int    bulletAttackers{ 0 };
		double raiderLastHitPlayerAt{ -1e9 };
	};
	inline HitLoopOut HitLoop(bool a_key)
	{
		HitLoopOut o;
		O::Group g;
		g.player = 0x14;
		g.him = 0xFF000802;
		g.teammates[0] = 0x0005DE4D;
		g.nTeammates = 1;
		scan::Params p;  // the shipped HOTFIX defaults (defend rule on)
		scan::PlayerFacts pl;
		pl.valid = true;
		LoopActor raider, danse, settler;
		auto base = [](scan::ActorFacts& f, std::uint32_t id) {
			f.id = id;
			f.loaded = f.sameSpace = true;
			f.hostileKnown = true;
		};
		// a Combat Zone arena raider: hostile kind, in a scene and not in combat with the group (combat target someone else)
		base(raider.f, 0xFF001234);
		raider.f.hostile = raider.f.hostileKind = true;
		raider.f.scene = true;
		raider.f.inCombat = false;
		raider.f.combatKind = scan::CombatKind::kOther;
		raider.pos = { 700, 0, 0 };
		// Danse: a teammate companion fighting next to the player (his stray melee sometimes lands on the player)
		base(danse.f, g.teammates[0]);
		danse.f.teammate = danse.f.companion = true;
		danse.f.essential = true;
		danse.f.hostile = false;
		danse.f.inCombat = true;
		danse.f.combatKind = scan::CombatKind::kOther;
		danse.pos = { 120, 60, 0 };
		// a settler the player hits once with a melee weapon at t = 3 s
		base(settler.f, 0xFF0055AA);
		settler.f.settler = settler.f.settlerNpc = true;
		settler.f.hostile = false;
		settler.pos = { -150, 0, 0 };

		scan::HitRing      ring;
		scan::AttackerTable att;
		int                quarrels = 0;
		const double       dt = 1.0 / 120.0;
		double             nextRaiderSwing = 0, nextDanseStray = 1.3;
		for (int fr = 0; fr < 960; ++fr) {
			const double now = fr * dt;
			// ---- the world (engine threads): the raider walks to the player at 350 u/s and swings every 1.0 s within 100 u
			const double dx = -raider.pos.x;
			if (std::abs(dx) > 90) raider.pos.x += (dx > 0 ? 1 : -1) * std::min(350.0 * dt, std::abs(dx) - 90);
			auto sink = [&](const O::HitEv& e) {
				scan::HitRec r;
				if (A::ClassifyHit(e, g, a_key, r)) {
					r.stampUs = static_cast<std::int64_t>(now * 1e6);
					ring.Push(r);
				}
			};
			bool raiderHitThisFrame = false;
			if (std::abs(raider.pos.x) <= 100 && now >= nextRaiderSwing) {
				nextRaiderSwing = now + 1.0;
				O::HitEv e;  // Actor::DoHitMe -> SendHit(HitData&): raw refs null on purpose
				e.usesHitData = true;
				e.hdTarget = g.player;
				e.hdAggressor = raider.f.id;
				sink(e);
				++o.raiderHits;
				raiderHitThisFrame = true;
				if (o.firstHitFrame < 0) o.firstHitFrame = fr;
			}
			if (now >= nextDanseStray) {  // a stray friendly melee hit on the player (NOT a trigger, user rule)
				nextDanseStray = now + 2.5;
				O::HitEv e;
				e.usesHitData = true;
				e.hdTarget = g.player;
				e.hdAggressor = danse.f.id;
				sink(e);
				++o.danseHits;
			}
			if (fr == 360) {  // the player's own melee hit on the settler
				O::HitEv e;
				e.usesHitData = true;
				e.hdTarget = settler.f.id;
				e.hdAggressor = g.player;
				sink(e);
			}
			if (fr == 600) {  // one bullet on the player arrives twice: the old-style event + the DoHitMe copy
				O::HitEv a;
				a.rawTarget = g.player;
				a.rawCause = 0xFF00B011;
				O::HitEv b;
				b.usesHitData = true;
				b.hdTarget = g.player;
				b.hdAggressor = 0xFF00B011;
				sink(a);
				sink(b);
			}
			// ---- the pump (scan::DrainHits, as scan.cpp does it): ring -> attacker table -> facts
			scan::HitRec buf[scan::HitRing::kCap];
			const int    n = ring.Drain(buf, scan::HitRing::kCap);
			for (int i = 0; i < n; ++i) {
				const auto& r = buf[i];
				if (r.kind == scan::HitKind::kPlayerHit) {
					++quarrels;
					continue;
				}
				att.Note(r.cause, r.kind, now, 90.0);
				for (LoopActor* la : { &raider, &danse, &settler })
					if (const auto* ar = att.Get(la->f.id); ar && la->f.id == r.cause) {
						la->f.lastHitPlayerAt = ar->lastHitPlayerAt;
						la->f.lastHitMeAt = ar->lastHitMeAt;
						la->f.lastHitTeammateAt = ar->lastHitTeammateAt;
					}
				if (r.cause == danse.f.id) ++o.danseRecords;
			}
			// ---- the brain's view this frame
			auto allowed = [&](const scan::ActorFacts& f) { return scan::AllowedTiers(scan::FinalVeto(f, pl, p, now), p) != 0; };
			if (raiderHitThisFrame && scan::AttackingPlayer(raider.f, now, p)) ++o.raiderEvidenceSameFrame;
			if (allowed(raider.f)) {
				++o.raiderAllowedFrames;
				if (o.firstAllowedFrame < 0) o.firstAllowedFrame = fr;
			}
			if (allowed(danse.f)) ++o.danseAllowedFrames;
			if (allowed(settler.f)) ++o.settlerAllowedFrames;
		}
		o.quarrelRecords = quarrels;
		o.raiderLastHitPlayerAt = raider.f.lastHitPlayerAt;
		for (const auto& r : att.All())
			if (r.id == 0xFF00B011) ++o.bulletAttackers;
		return o;
	}

	inline void HitSinkLoop()
	{
		const HitLoopOut on = HitLoop(true), off = HitLoop(false);
		std::printf("info: S2-A-L1 [%s] key on: raider hits %d, same-frame evidence %d, first hit frame %d, first allowed frame %d, Danse stray %d (records %d) "
					"allowed %d, settler allowed %d, quarrels %d | key off: evidence %d, allowed %d\n",
			A::Impl(), on.raiderHits, on.raiderEvidenceSameFrame, on.firstHitFrame, on.firstAllowedFrame, on.danseHits, on.danseRecords, on.danseAllowedFrames,
			on.settlerAllowedFrames, on.quarrelRecords, off.raiderEvidenceSameFrame, off.raiderAllowedFrames);
		CHECK(on.raiderHits >= 5 && on.raiderEvidenceSameFrame == on.raiderHits, "S2-A-L1 [%s] every melee hit on the player is band-D evidence in the same frame (%d / %d)",
			A::Impl(), on.raiderEvidenceSameFrame, on.raiderHits);
		CHECK(on.firstAllowedFrame == on.firstHitFrame, "S2-A-L1 [%s] the scene raider's soft vetoes are lifted in the frame of its first melee hit (%d vs %d)", A::Impl(),
			on.firstAllowedFrame, on.firstHitFrame);
		CHECK(off.raiderEvidenceSameFrame == 0 && off.raiderAllowedFrames == 0,
			"S2-A-L1 [%s] the old sink reproduces U41: the melee raider is never evidence and never allowed (%d, %d)", A::Impl(), off.raiderEvidenceSameFrame,
			off.raiderAllowedFrames);
		CHECK(on.danseRecords == on.danseHits && on.danseAllowedFrames == 0,
			"S2-A-L1 [%s] USER RULE: Danse's stray melee hits on the player are recorded (%d / %d) but never make him a target (%d frames)", A::Impl(), on.danseRecords,
			on.danseHits, on.danseAllowedFrames);
		CHECK(on.settlerAllowedFrames == 0 && on.quarrelRecords == 1, "S2-A-L1 [%s] the player's melee hit on a settler is one quarrel record, never a target (%d, %d)",
			A::Impl(), on.quarrelRecords, on.settlerAllowedFrames);
		CHECK(on.bulletAttackers == 1, "S2-A-L1 [%s] one bullet seen twice (old-style + DoHitMe) is one attacker (%d)", A::Impl(), on.bulletAttackers);
	}

	// ============================================================================================== S2-B (b) masks
	struct Matrix
	{
		bool                     ok{ false };
		std::vector<int>         row[64];
		std::string              name[64];
		int                      n{ 0 };
	};
	inline Matrix ReadMatrix()
	{
		Matrix m;
		std::ifstream f(HereDir() + "/s2_coll_matrix.json", std::ios::binary);
		if (!f) return m;
		std::stringstream ss;
		ss << f.rdbuf();
		const std::string t = ss.str();
		for (int L = 0; L < 64; ++L) {
			const std::string key = "\"" + std::to_string(L) + "\": {";
			const auto        k = t.find(key);
			if (k == std::string::npos) continue;
			++m.n;
			const auto nm = t.find("\"name\": \"", k);
			if (nm != std::string::npos) m.name[L] = t.substr(nm + 9, t.find('"', nm + 9) - (nm + 9));
			const auto c = t.find("\"collidesIdx\": [", k);
			if (c == std::string::npos) continue;
			const auto e = t.find(']', c);
			std::string body = t.substr(c + 16, e - (c + 16));
			for (char& ch : body)
				if (ch == ',' || ch == '\n' || ch == '\r') ch = ' ';
			std::istringstream is(body);
			int                v;
			while (is >> v) m.row[L].push_back(v);
		}
		m.ok = m.n == 57;
		return m;
	}

	inline void MaskUnits()
	{
		const Matrix m = ReadMatrix();
		CHECK(m.ok && m.name[41] == "L_LOS" && m.name[55] == "L_CLUTTER_NOCUT", "S2-B0 coll_matrix copy read: %d layers, 41 = %s", m.n, m.name[41].c_str());
		std::uint64_t row41 = 0;
		for (int v : m.row[41]) row41 |= 1ull << v;
		std::uint64_t sight = row41;
		for (int v : { 5, 6, 7, 30, 32, 53 }) sight &= ~(1ull << v);
		// step-2 fixer (REVIEW F4): walls / floor come from HIS CAPSULE's row (30 L_CHARCONTROLLER): its solid layers (no
		// phantom kind: flags 1, 2, 3, 5; no weapon / spell 5, 7, water 11, trap 14, actor / bumper 30, 33, 52, 56, pick / camera /
		// LOS / projectile 38-46, trap trigger 54); walls = that minus the stair helper 31, floor = with it
		std::uint64_t solid30 = 0;
		for (int v : m.row[30]) solid30 |= 1ull << v;
		for (int v : { 5, 7, 11, 12, 14, 16, 22, 24, 30, 33, 38, 39, 40, 41, 43, 45, 46, 52, 54, 56 }) solid30 &= ~(1ull << v);
		const std::uint64_t walls = solid30 & ~(1ull << 31);
		const std::uint64_t floor = solid30 | (1ull << 31);
		CHECK(m.name[30] == "L_CHARCONTROLLER" && m.name[4] == "L_CLUTTER" && m.name[20] == "L_DEBRIS_LARGE" && m.name[35] == "L_COLLISIONBOX" && ((walls & sight) == sight) &&
				  ((walls >> 4) & 1) && ((walls >> 20) & 1) && ((walls >> 35) & 1),
			"S2-B1 walls mask recomputed from his capsule row 30 (L_CHARCONTROLLER): contains the sight layers + clutter 4, large debris 20, collision box 35 (%llx)",
			static_cast<unsigned long long>(walls));
		CHECK(m.name[3] == "L_TRANSPARENT" && m.name[27] == "L_INVISIBLE_WALL" && m.name[31] == "L_STAIRHELPER", "S2-B0 the added layers are glass / invisible wall / stair helper");
		const std::uint64_t ms = A::Mask(O::Use::kSight, true), mw = A::Mask(O::Use::kMove, true), mf = A::Mask(O::Use::kFloor, true);
		CHECK(sight == 0x80000020022607ull && ms == sight, "S2-B1 [%s] sight mask = L_LOS row minus 5,6,7,30,32,53 = %llx (code %llx)", A::Impl(),
			static_cast<unsigned long long>(sight), static_cast<unsigned long long>(ms));
		CHECK(walls == 0x8000083C12261Full && mw == walls, "S2-B1 [%s] walls mask = capsule row 30 solid minus 31 = %llx (code %llx)", A::Impl(), static_cast<unsigned long long>(walls),
			static_cast<unsigned long long>(mw));
		CHECK(floor == 0x800008BC12261Full && mf == floor, "S2-B1 [%s] floor / Wings mask = walls + 31 = %llx (code %llx)", A::Impl(), static_cast<unsigned long long>(floor),
			static_cast<unsigned long long>(mf));
		CHECK(A::Mask(O::Use::kSweep, true) == mw && A::Mask(O::Use::kWings, true) == mf, "S2-B1 [%s] the sweep guard uses the walls mask, Wings the floor mask", A::Impl());
		for (std::uint64_t mk : { ms, mw, mf }) {
			bool absent = true;
			for (int L : { 48, 49, 50, 51, 52, 53, 54, 56 }) absent = absent && !((mk >> L) & 1);
			CHECK(absent && ((mk >> 55) & 1), "S2-B2 [%s] layers 48-54 and 56 absent, 55 present in %llx", A::Impl(), static_cast<unsigned long long>(mk));
			CHECK(!((mk >> 8) & 1) && !((mk >> 30) & 1) && !((mk >> 32) & 1) && !((mk >> 33) & 1), "S2-B2 [%s] no actor layer blocks a pick (%llx)", A::Impl(),
				static_cast<unsigned long long>(mk));
		}
		CHECK(A::Mask(O::Use::kSight, false) == 0, "S2-B3 [%s] look.rayLayers 0 = no custom layers (the old path)", A::Impl());
		// classification by the shape filter info: the layer is the low 7 bits whatever the group / system bits say
		bool cls = true;
		for (std::uint32_t hi : { 0u, 0x00010000u, 0xABCD0000u, 0x00008000u, 0x7F00u })
			for (int L = 0; L < 64; ++L) {
				const std::uint32_t fi = hi | static_cast<std::uint32_t>(L);
				cls = cls && A::HitBlocks(O::Use::kSight, fi, true) == (((ms >> L) & 1) != 0);
			}
		CHECK(cls, "S2-B3 [%s] a hit blocks by the layer in the low 7 bits of the shape filter info only", A::Impl());
	}

	// B-L1: the engine pick on a fake world (FO4_COLLISION_RE §1.3). Each ray crosses 0-6 objects of random layers.
	// Old path: filter info 0 = group 0 = the world filter passes EVERY layer, so the engine reports the first object of
	// any layer; the plugin re-casts 2 u past layers outside its old block table (which wrongly held 48-62), up to 5
	// times, exhaustion = blocked. New path: his filter info + the custom mask: the engine reports only layers in the
	// mask, plus a group-0 body of any layer (HOOKS.md D-b: the custom filter hits group 0 before the mask; 5 % of the
	// odd layers here); the plugin classifies by the shape layer and re-casts past those.
	struct PickStats
	{
		long   rays{ 0 }, picks{ 0 }, blocked{ 0 }, exhausted{ 0 }, fakeWalls{ 0 }, bodyBlocks{ 0 }, wrong{ 0 };
		double PerRay() const { return rays ? static_cast<double>(picks) / rays : 0; }
	};
	inline PickStats PickLoop(bool a_key, O::Use a_use, std::uint64_t a_seed, int a_rays)
	{
		constexpr std::uint64_t kDD = 0x7FFFull << 48;  // the old plugin's block tables (fo4/rays.h before step 2)
		constexpr std::uint64_t oldSight = O::L(0) | O::L(1) | O::L(2) | O::L(9) | O::L(10) | O::L(13) | O::L(17) | O::L(29) | kDD;
		constexpr std::uint64_t oldMove = oldSight | O::L(3) | O::L(27);
		const std::uint64_t     oldBlock = a_use == O::Use::kSight ? oldSight : oldMove;
		const std::uint64_t     truth = a_use == O::Use::kSight ? O::kSight : O::kWalls;  // what really stops sight / a body
		// layers met along real FO4 rays (outdoor + indoor): statics, props, clutter, trees, glass, actors, triggers, debris
		const int pool[] = { 1, 1, 1, 2, 4, 4, 9, 10, 13, 3, 26, 28, 27, 8, 30, 32, 33, 12, 48, 49, 51, 52, 53, 54, 56, 19, 20, 25, 5, 6, 11, 22, 24, 55, 29 };
		std::mt19937_64 rng(a_seed);
		PickStats s;
		for (int i = 0; i < a_rays; ++i) {
			const int        nObj = static_cast<int>(rng() % 7);
			std::vector<int> objs;
			std::vector<int> grp0;
			for (int k = 0; k < nObj; ++k) {
				objs.push_back(pool[rng() % (sizeof(pool) / sizeof(pool[0]))]);
				grp0.push_back((rng() % 20) == 0);
			}
			bool truthBlocked = false;
			for (int L : objs) truthBlocked = truthBlocked || ((truth >> L) & 1);
			++s.rays;
			bool        blocked = false;
			std::size_t at = 0;
			int         recasts = 0;
			const std::uint64_t mk = A::Mask(a_use, a_key);
			for (;;) {
				++s.picks;
				if (a_key) {
					while (at < objs.size() && !((mk >> objs[at]) & 1) && !grp0[at]) ++at;  // the custom filter
				}
				if (at >= objs.size()) break;  // clear
				const bool blocks = a_key ? A::HitBlocks(a_use, static_cast<std::uint32_t>(objs[at]) | 0x12340000u, true) : (((oldBlock >> objs[at]) & 1) != 0);
				if (blocks) {
					blocked = true;
					break;
				}
				if (++recasts > 5) {  // maxRecasts: exhaustion counts as blocked
					blocked = true;
					++s.exhausted;
					break;
				}
				++at;  // re-cast 2 u past the passed hit
			}
			s.blocked += blocked;
			if (blocked != truthBlocked) ++s.wrong;
			bool fake = false, body = false;
			for (int L : objs) {
				fake = fake || (L >= 48 && L <= 54) || L == 56;
				body = body || L == 8 || L == 30 || L == 32 || L == 33;
			}
			if (blocked && !truthBlocked && fake) ++s.fakeWalls;
			if (blocked && !truthBlocked && body) ++s.bodyBlocks;
		}
		return s;
	}

	inline void PickLoops()
	{
		for (O::Use u : { O::Use::kSight, O::Use::kMove }) {
			const char*     nm = u == O::Use::kSight ? "sight" : "walls";
			const PickStats nw = PickLoop(true, u, 7, 20000), od = PickLoop(false, u, 7, 20000);
			std::printf("info: S2-B-L1 [%s] %s rays: picks/ray %.3f (old %.3f), wrong answers %ld (old %ld: fake walls %ld, bodies %ld, exhausted %ld)\n", A::Impl(), nm,
				nw.PerRay(), od.PerRay(), nw.wrong, od.wrong, od.fakeWalls, od.bodyBlocks, od.exhausted);
			CHECK(nw.PerRay() <= 1.1, "S2-B-L1 [%s] %s: picks per logical ray %.3f <= 1.1 (old path %.3f)", A::Impl(), nm, nw.PerRay(), od.PerRay());
			CHECK(nw.wrong == 0, "S2-B-L1 [%s] %s: every ray answers like the layer table (%ld wrong; old path %ld)", A::Impl(), nm, nw.wrong, od.wrong);
			CHECK(od.wrong > 0 && od.fakeWalls > 0, "S2-B-L1 the old path reproduces the RE bug (rays stop on trigger volumes: %ld)", od.fakeWalls);
		}
		// glass: blocks his body, never his sight
		CHECK(!A::HitBlocks(O::Use::kSight, 3, true) && A::HitBlocks(O::Use::kMove, 3, true) && A::HitBlocks(O::Use::kSweep, 27, true) &&
				  !A::HitBlocks(O::Use::kSight, 27, true),
			"S2-B-L1 [%s] glass / invisible walls stop his body, not his sight", A::Impl());
		CHECK(A::HitBlocks(O::Use::kFloor, 31, true) && !A::HitBlocks(O::Use::kMove, 31, true), "S2-B-L1 [%s] stair helpers hold the Wings / floor ray only", A::Impl());
		CHECK(A::HitBlocks(O::Use::kSight, 9, true) && A::HitBlocks(O::Use::kSight, 1, true), "S2-B-L1 [%s] trees and statics block sight", A::Impl());
	}

	// ============================================================================================== S2-C (c) refused picks
	inline void RefusedUnits()
	{
		CHECK(!A::AllowFailedPicks(O::Use::kSweep, true) && !A::AllowFailedPicks(O::Use::kWings, true), "S2-C1 [%s] P0 rays (sweep guard, Wings) never accept a refusal",
			A::Impl());
		CHECK(A::AllowFailedPicks(O::Use::kSight, true) && A::AllowFailedPicks(O::Use::kMove, true) && A::AllowFailedPicks(O::Use::kFloor, true),
			"S2-C1 [%s] other rays may be refused (then they count as blocked)", A::Impl());
		CHECK(A::AllowFailedPicks(O::Use::kSweep, false) && A::AllowFailedPicks(O::Use::kWings, false), "S2-C1 [%s] look.pickFailedIsHit 0 = the old pick data", A::Impl());
		CHECK(A::RayBlocked(false, true, true) && !A::RayBlocked(false, true, false), "S2-C2 [%s] a refused pick is blocked (old: clear)", A::Impl());
		CHECK(!A::RayBlocked(false, false, true) && A::RayBlocked(true, false, true) && A::RayBlocked(true, true, true), "S2-C2 [%s] real hits and real misses unchanged",
			A::Impl());
	}

	// C-L1: he dashes along x toward a wall at x = 1200 u. Each frame the glue asks: sweep guard (P0, the next 1/60 s of the
	// dash), a move-clear segment 300 u ahead (P1), 3 sight rays (P4). The engine refuses every pick that allows refusal
	// once 6 picks ran this frame (the pick time budget, MOT §3.4).
	struct WallOut
	{
		int throughClear{ 0 }, refusedP0{ 0 }, refused{ 0 }, segs{ 0 };
	};
	inline WallOut WallLoop(bool a_key)
	{
		WallOut o;
		const double wallX = 1200;
		double       x = 0;
		for (int fr = 0; fr < 240 && x < wallX - 20; ++fr) {
			int picks = 0;
			auto pick = [&](O::Use u, double x0, double x1) {
				++picks;
				const bool allow = A::AllowFailedPicks(u, a_key);
				const bool refused = allow && picks > 6;
				if (refused) {
					++o.refused;
					return A::RayBlocked(false, true, a_key);
				}
				if (!allow && picks > 6) ++o.refusedP0;  // must stay 0: the engine runs it
				const bool hit = x0 < wallX && x1 >= wallX;
				return A::RayBlocked(hit, false, a_key);
			};
			for (int k = 0; k < 3; ++k) pick(O::Use::kSight, x, x + 2000);  // sight round robin first (P4) uses the budget
			for (int k = 0; k < 4; ++k) pick(O::Use::kFloor, x, x);          // floor / headroom probes
			const bool moveBlocked = pick(O::Use::kMove, x, x + 300);
			++o.segs;
			if (!moveBlocked && x + 300 >= wallX) ++o.throughClear;
			const bool sweepBlocked = pick(O::Use::kSweep, x, x + 4000.0 / 60.0);
			if (!sweepBlocked && x + 4000.0 / 60.0 >= wallX) ++o.throughClear;
			x = std::min(x + 4000.0 / 120.0, wallX - 20);
		}
		return o;
	}
	inline void RefusedLoop()
	{
		const WallOut on = WallLoop(true), off = WallLoop(false);
		std::printf("info: S2-C-L1 [%s] wall dash: through-wall 'clear' answers %d (old %d), refused picks %d, P0 picks past the budget (run anyway) %d\n", A::Impl(), on.throughClear,
			off.throughClear, on.refused, on.refusedP0);
		CHECK(on.throughClear == 0, "S2-C-L1 [%s] with the pick budget used up no segment through the wall is reported clear (%d)", A::Impl(), on.throughClear);
		CHECK(off.throughClear > 0, "S2-C-L1 the old path reproduces the bug (refusal read as clear: %d)", off.throughClear);
		CHECK(on.refused > 0, "S2-C-L1 refused picks happened and were counted (%d)", on.refused);
	}

	// ============================================================================================== S2-D (d) continuity
	inline O::JumpIn Tick(const Vec3& p0, const Vec3& p1, const Vec3& v, double T)
	{
		O::JumpIn j;
		j.prev = p0;
		j.pos = p1;
		j.vCmd = v;
		j.T = T;
		j.prevWorld = j.world = 0x3C;  // Commonwealth
		j.prevCell = j.cell = 0x1001;
		return j;
	}
	inline void JumpUnits()
	{
		// D1: the acceptance case: a 0.24 s hitch at 6300 u/s, commanded speed known
		const Vec3 v{ 6300, 0, 0 };
		O::JumpIn  h = Tick({ 0, 0, 0 }, { 6300 * 0.24, 0, 0 }, v, 0.24);
		CHECK(!A::JumpEvent(h, true), "S2-D1 [%s] a 0.24 s hitch at 6300 u/s (1512 u) is no continuity event", A::Impl());
		CHECK(A::JumpEvent(h, false), "S2-D1 [%s] the old 1500 u rule fires on it (the RE bug reproduced)", A::Impl());
		O::JumpIn h2 = Tick({ 0, 0, 0 }, { 6300 * 0.24 + 120, 80, -60 }, v, 0.24);  // + a contact push
		CHECK(!A::JumpEvent(h2, true), "S2-D1 [%s] the same hitch plus a 150 u contact push is no event", A::Impl());
		// D2: outdoor cell change at speed: the engine re-files him (MoveRefToNewSpace), never moves him
		O::JumpIn c = Tick({ 4090, 0, 0 }, { 4090 + 4000.0 / 120.0, 0, 0 }, { 4000, 0, 0 }, 1.0 / 120.0);
		c.cell = 0x1002;
		CHECK(!A::JumpEvent(c, true), "S2-D2 [%s] an outdoor cell change alone is no event", A::Impl());
		// D3: a 2000 u MoveTo while hovering
		O::JumpIn mv = Tick({ 0, 0, 0 }, { 2000, 0, 0 }, { 0, 0, 0 }, 1.0 / 60.0);
		CHECK(A::JumpEvent(mv, true), "S2-D3 [%s] a 2000 u MoveTo is an event", A::Impl());
		O::JumpIn mvd = Tick({ 0, 0, 0 }, { 2000 + 4000.0 / 60.0, 0, 0 }, { 4000, 0, 0 }, 1.0 / 60.0);
		CHECK(A::JumpEvent(mvd, true), "S2-D3 [%s] a 2000 u MoveTo in the middle of a 4000 u/s dash is an event", A::Impl());
		// D4: spaces and flags
		O::JumpIn ws = Tick({ 0, 0, 0 }, { 10, 0, 0 }, { 0, 0, 0 }, 1.0 / 60.0);
		ws.world = 0x2D;
		CHECK(A::JumpEvent(ws, true), "S2-D4 [%s] a worldspace change is an event even with no distance", A::Impl());
		O::JumpIn io = Tick({ 0, 0, 0 }, { 10, 0, 0 }, { 0, 0, 0 }, 1.0 / 60.0);
		io.interior = true;
		io.world = 0;
		CHECK(A::JumpEvent(io, true), "S2-D4 [%s] exterior -> interior is an event", A::Impl());
		O::JumpIn fl = Tick({ 0, 0, 0 }, { 50, 0, 0 }, { 0, 0, 0 }, 1.0 / 60.0);
		fl.flagged = true;
		CHECK(A::JumpEvent(fl, true), "S2-D4 [%s] a relocation flagged by the SetPosition / MoveRefToNewSpace hook is an event", A::Impl());
		O::JumpIn own = mv;
		own.ours = true;
		CHECK(!A::JumpEvent(own, true) && !A::JumpEvent(own, false), "S2-D4 [%s] our own flagged jump (DoBlink) is never an event", A::Impl());
		// D5: the 400 u boundary
		CHECK(!A::JumpEvent(Tick({ 0, 0, 0 }, { 399, 0, 0 }, { 0, 0, 0 }, 1.0 / 60), true) && A::JumpEvent(Tick({ 0, 0, 0 }, { 401, 0, 0 }, { 0, 0, 0 }, 1.0 / 60), true),
			"S2-D5 [%s] the residual threshold is 400 u", A::Impl());
		// D6: a blocked dash is no relocation: a 0.17 s hitch at 6300 u/s against a wall (commanded 1071 u, moved 25 u)
		O::JumpIn wall = Tick({ 0, 0, 0 }, { 25, 0, 0 }, v, 0.17);
		CHECK(!A::JumpEvent(wall, true), "S2-D6 [%s] a hitch at 6300 u/s against a wall (1046 u short of the command) is no event", A::Impl());
		O::JumpIn back = Tick({ 0, 0, 0 }, { -1200, 0, 0 }, v, 0.17);
		CHECK(A::JumpEvent(back, true), "S2-D6 [%s] 1200 u backwards while dashing forward is an event", A::Impl());
		if (A::kHaveCore) {  // D-X differential
			std::mt19937_64 rng(13);
			std::uniform_real_distribution<double> U(-1, 1);
			int diff = 0;
			for (int i = 0; i < 50000; ++i) {
				O::JumpIn j = Tick({ U(rng) * 1e4, U(rng) * 1e4, U(rng) * 500 }, {}, { U(rng) * 7000, U(rng) * 7000, U(rng) * 2000 }, 0.004 + 0.3 * (U(rng) + 1) / 2);
				j.pos = { j.prev.x + j.vCmd.x * j.T + U(rng) * 700, j.prev.y + j.vCmd.y * j.T + U(rng) * 700, j.prev.z + j.vCmd.z * j.T + U(rng) * 300 };
				if (rng() % 10 == 0) j.cell ^= 1;
				if (rng() % 40 == 0) j.world = 0x2D;
				if (rng() % 40 == 0) j.interior = true;
				if (rng() % 40 == 0) j.flagged = true;
				if (rng() % 40 == 0) j.ours = true;
				const bool key = rng() & 1;
				if (A::JumpEvent(j, key) != O::JumpEvent(j, key)) ++diff;
			}
			CHECK(diff == 0, "S2-DX core JumpEvent == oracle on 50000 generated ticks (%d differ)", diff);
		}
	}

	// D-L1: the REAL TABS body in a dash chain toward a far, fleeing foe (open field, cells of 4096 u), rendered at a random
	// fps 30-240 with +-20 % jitter and a 0.2-0.3 s hitch about every 2 s. The capsule (Havok) moves v_cmd x T (+ up to 60 u
	// of contact push) per frame, v_cmd = the sim hip velocity (MoveProxy's feed-forward). At 20 s a 2000 u MoveTo, at 30 s a
	// fast travel (worldspace change). Events must be exactly those 2.
	struct ChainOut
	{
		int    frames{ 0 }, hitches{ 0 }, cellChanges{ 0 }, events{ 0 }, falseEvents{ 0 }, oldFalse{ 0 }, scriptedSeen{ 0 };
		double vMax{ 0 }, hitchMaxStep{ 0 };
	};
	inline ChainOut ChainLoop(bool a_key, std::uint64_t a_seed, double a_speedScale)
	{
		ChainOut o;
		sp::body::Body b;
		b.Reset({ 0, 0, 0 }, 1.5707963267948966, 1.15, 64.18, a_seed);
		S2Floor      w;
		const double upm = 64.18;
		std::mt19937_64 rng(a_seed);
		std::uniform_real_distribution<double> U(0, 1);
		Vec3          foe{ 3000, 0, 0 };  // FO4 u, flees at 1200 u/s along +x
		Vec3          cap{ 0, 0, 0 }, capPrev{};
		std::uint32_t world = 0x3C, cell = 0;
		double        t = 0, nextHitch = 2.0;
		bool          moved = false, travelled = false;
		while (t < 40.0) {
			const double fps = 30.0 + U(rng) * 210.0;
			double       dt = (1.0 / fps) * (0.8 + 0.4 * U(rng));
			bool         hitch = false;
			if (t >= nextHitch) {
				dt = 0.2 + 0.1 * U(rng);
				nextHitch = t + 1.5 + U(rng);
				hitch = true;
				++o.hitches;
			}
			t += dt;
			foe.x += 1200 * dt;
			sp::body::Input in;
			in.hasTarget = true;
			in.targetVisible = true;
			in.targetTorso = sp::math::FO4ToUnityPos({ foe.x, foe.y, foe.z + 90 }, upm);
			in.targetGround = sp::math::FO4ToUnityPos(foe, upm);
			in.targetId = 7;
			in.autoAttack = true;
			b.Advance(std::min(dt, 0.1), in, w);  // the P6 clock clamps the sim step at 0.1 s
			const Vec3 hipU = b.Part(sp::body::kHip).pos, hvU = b.Part(sp::body::kHip).vel;
			Vec3       vCmd = sp::math::UnityToFO4Dir(hvU) * upm * a_speedScale;
			o.vMax = std::max(o.vMax, sp::math::Length(vCmd));
			(void)hipU;
			capPrev = cap;
			O::JumpIn j;
			j.prev = capPrev;
			j.vCmd = vCmd;
			j.T = dt;  // physics time of the frame (the whole hitch, MOT §3.12)
			const bool walled = std::fmod(t, 5.0) < 0.6;  // held at a wall for 0.6 s every 5 s: the capsule makes 10 % of the command
			cap = cap + vCmd * dt * (walled ? 0.1 : 1.0) + Vec3{ (U(rng) - 0.5) * 120, (U(rng) - 0.5) * 120, 0 } * (U(rng) < 0.1 ? 1.0 : 0.0);
			bool scripted = false;
			if (!moved && t >= 20.0) {  // Papyrus MoveTo 2000 u to the side
				cap = cap + Vec3{ 0, 2000, 0 };
				moved = scripted = true;
			}
			const std::uint32_t prevWorld = world;
			if (!travelled && t >= 30.0) {  // fast travel: another worldspace
				world = 0x2D;
				travelled = scripted = true;
			}
			const std::uint32_t prevCell = cell;
			cell = static_cast<std::uint32_t>(static_cast<int>(std::floor(cap.x / 4096.0)) * 1000 + static_cast<int>(std::floor(cap.y / 4096.0)) + 500000);
			if (cell != prevCell && o.frames > 0) ++o.cellChanges;
			j.pos = cap;
			j.prevWorld = prevWorld;
			j.world = world;
			j.prevCell = prevCell;
			j.cell = cell;
			if (o.frames > 0) {
				const bool ev = A::JumpEvent(j, a_key);
				o.events += ev;
				if (ev && !scripted) ++o.falseEvents;
				if (ev && scripted) ++o.scriptedSeen;
				if (O::JumpEvent(j, false) && !scripted) ++o.oldFalse;
				if (hitch) o.hitchMaxStep = std::max(o.hitchMaxStep, sp::math::Length(cap - capPrev));
			}
			++o.frames;
		}
		return o;
	}

	// D-L2: the MOT §3.12 case as a track: a straight dash at a constant 6300 u/s, 120 fps, a 0.24 s hitch every 2 s, cell
	// borders every 4096 u. The old 1500 u rule fires on every hitch; the new test never.
	inline void StraightTrack(bool a_key, int& a_events, int& a_hitches)
	{
		a_events = a_hitches = 0;
		Vec3 p{};
		double t = 0, nextHitch = 2.0;
		std::uint32_t cell = 0;
		const Vec3 v{ 6300, 0, 0 };
		while (t < 30) {
			double dt = 1.0 / 120.0;
			if (t >= nextHitch) {
				dt = 0.24;
				nextHitch += 2.0;
				++a_hitches;
			}
			t += dt;
			O::JumpIn j = Tick(p, p + v * dt, v, dt);
			j.prevCell = cell;
			cell = static_cast<std::uint32_t>(std::floor(j.pos.x / 4096.0));
			j.cell = cell;
			a_events += A::JumpEvent(j, a_key);
			p = j.pos;
		}
	}

	inline void JumpLoops()
	{
		const ChainOut on = ChainLoop(true, 5, 1.0);
		std::printf("info: S2-D-L1 [%s] TABS chain: %d frames, %d hitches (max step %.0f u), %d cell changes, v max %.0f u/s: events %d (false %d, scripted %d)\n",
			A::Impl(), on.frames, on.hitches, on.hitchMaxStep, on.cellChanges, on.vMax, on.events, on.falseEvents, on.scriptedSeen);
		CHECK(on.falseEvents == 0, "S2-D-L1 [%s] the real TABS dash chain (v max %.0f u/s, %d hitches, %d cell changes, wall holds): 0 false continuity events (%d)", A::Impl(),
			on.vMax, on.hitches, on.cellChanges, on.falseEvents);
		CHECK(on.scriptedSeen == 2 && on.events == 2, "S2-D-L1 [%s] the MoveTo and the fast travel are exactly one event each (%d of %d)", A::Impl(), on.scriptedSeen, on.events);
		for (std::uint64_t seed : { 11ull, 12ull, 13ull }) {
			const ChainOut o = ChainLoop(true, seed, 1.0);
			CHECK(o.falseEvents == 0 && o.scriptedSeen == 2, "S2-D-L1 [%s] seed %llu: false %d, scripted %d", A::Impl(), static_cast<unsigned long long>(seed), o.falseEvents,
				o.scriptedSeen);
		}
		int evOn = 0, evOff = 0, h = 0;
		StraightTrack(true, evOn, h);
		StraightTrack(false, evOff, h);
		std::printf("info: S2-D-L2 [%s] 6300 u/s straight, %d hitches of 0.24 s: events %d (old rule %d)\n", A::Impl(), h, evOn, evOff);
		CHECK(evOn == 0, "S2-D-L2 [%s] 6300 u/s with %d hitches of 0.24 s and a cell border every 4096 u: 0 events (%d)", A::Impl(), h, evOn);
		CHECK(evOff == h, "S2-D-L2 the old 1500 u rule fires on every hitch (%d of %d): the RE bug reproduced", evOff, h);
	}

	// ============================================================================================== S2-E (e) freeze gate
	inline void FreezeUnits()
	{
		// a_inMenu, a_freeze, a_boundLoaded, a_p6, a_fallbackDue, key
		CHECK(!A::MayTick(true, false, true, 1, true, true) && !A::MayTick(true, false, false, -1, true, true), "S2-E1 [%s] menu mode: no tick", A::Impl());
		CHECK(!A::MayTick(false, true, true, 1, true, true) && !A::MayTick(false, true, false, -1, true, true), "S2-E1 [%s] Main::freezeTime: no tick", A::Impl());
		CHECK(!A::MayTick(false, false, true, -1, true, true), "S2-E1 [%s] bound + loaded with no new P6 frame: no fallback tick", A::Impl());
		CHECK(A::MayTick(false, false, true, 1, false, true) && !A::MayTick(false, false, true, 0, true, true), "S2-E1 [%s] a new P6 frame ticks, the same frame does not",
			A::Impl());
		CHECK(A::MayTick(false, false, false, -1, true, true), "S2-E1 [%s] unbound / unloaded: the old fallback still ticks (Papyrus mode)", A::Impl());
		CHECK(A::MayTick(false, true, true, -1, true, false), "S2-E1 look.freezeGate 0 = the old rule (it ticked from the fallback while frozen)");
	}

	// E-L1: 120 fps; P6 frames counted by his Actor::Update; at 2 s the world freezes for 3 s (Main::freezeTime: no job lists,
	// no P6) while the F4SE pump keeps running; at 7 s a pause menu for 2 s (inMenuMode). The real P6Clock and the real body.
	struct FreezeOut
	{
		int    ticksFrozen{ 0 }, ticksMenu{ 0 }, ticksLive{ 0 }, firstStepsAfter{ -1 };
		double hipMoveFrozen{ 0 }, firstDtAfter{ -1 };
	};
	inline FreezeOut FreezeLoop(bool a_key)
	{
		FreezeOut o;
		sp::body::Body b;
		b.Reset({ 0, 0, 0 }, 0.0, 1.15, 64.18);
		S2Floor          w;
		sp::tick::P6Clock clk;
		std::uint64_t     p6Frames = 0;
		double            p6Time = 0, lastFallback = 0;
		Vec3              hipAtFreeze{};
		bool              wasFrozen = false;
		sp::body::Input   in;
		in.hasTarget = true;
		in.targetVisible = true;
		in.targetTorso = { 0, 1.5, 40 };
		in.targetGround = { 0, 0, 40 };
		in.targetId = 9;
		const double dt = 1.0 / 120.0;
		for (int fr = 0; fr < 1320; ++fr) {
			const double now = fr * dt;
			const bool   frozen = now >= 2.0 && now < 5.0;
			const bool   menu = now >= 7.0 && now < 9.0;
			if (!frozen && !menu) {  // the job lists ran: his Actor::Update counted a frame
				++p6Frames;
				p6Time += dt;
			}
			if (frozen && !wasFrozen) hipAtFreeze = b.Part(sp::body::kHip).pos;
			for (int pump = 0; pump < 2; ++pump) {  // the F4SE task pump runs ~2 x per frame, also while frozen / in menus
				const double nowReal = now + pump * dt * 0.5;
				double       tdt = 0;
				const int    p6 = clk.Pump(p6Frames, p6Time, nowReal, tdt);
				const bool   fallbackDue = nowReal - lastFallback >= dt * 0.6;
				if (!A::MayTick(menu, frozen, true, p6, fallbackDue, a_key)) continue;
				if (p6 < 0) {  // the old fallback rule: QPC spacing, the last BSTimer delta
					tdt = dt;
					lastFallback = nowReal;
				}
				const int steps = b.Advance(tdt, in, w);
				if (frozen) ++o.ticksFrozen;
				else if (menu) ++o.ticksMenu;
				else {
					++o.ticksLive;
					if (now >= 5.0 && o.firstDtAfter < 0) {
						o.firstDtAfter = tdt;
						o.firstStepsAfter = steps;
					}
				}
			}
			if (frozen) o.hipMoveFrozen = std::max(o.hipMoveFrozen, sp::math::Length(b.Part(sp::body::kHip).pos - hipAtFreeze));
			wasFrozen = frozen;
		}
		return o;
	}
	inline void FreezeLoops()
	{
		const FreezeOut on = FreezeLoop(true), off = FreezeLoop(false);
		std::printf("info: S2-E-L1 [%s] frozen 3 s: ticks %d (old %d), sim moved %.4f m (old %.3f m); pause menu ticks %d; live ticks %d; first dt after %.4f s (%d steps)\n",
			A::Impl(), on.ticksFrozen, off.ticksFrozen, on.hipMoveFrozen, off.hipMoveFrozen, on.ticksMenu, on.ticksLive, on.firstDtAfter, on.firstStepsAfter);
		CHECK(on.ticksFrozen == 0 && on.hipMoveFrozen == 0.0, "S2-E-L1 [%s] a frozen world gives 0 ticks and his sim does not move (%d ticks, %.4f m)", A::Impl(),
			on.ticksFrozen, on.hipMoveFrozen);
		CHECK(off.ticksFrozen > 0 && off.hipMoveFrozen > 0.0, "S2-E-L1 the old fallback reproduces the RE bug (%d ticks while frozen)", off.ticksFrozen);
		CHECK(on.ticksMenu == 0 && off.ticksMenu == 0, "S2-E-L1 [%s] a pause menu gives 0 ticks (%d)", A::Impl(), on.ticksMenu);
		CHECK(on.firstDtAfter >= 0 && on.firstDtAfter <= 0.1 + 1e-9 && on.firstStepsAfter <= 7, "S2-E-L1 [%s] no backlog after the freeze (dt %.4f s, %d steps)", A::Impl(),
			on.firstDtAfter, on.firstStepsAfter);
		CHECK(on.ticksLive >= 700 && on.ticksLive <= 722, "S2-E-L1 [%s] he ticks once per live frame when the world runs (%d of 720)", A::Impl(), on.ticksLive);
	}

	// ============================================================================================== S2-H hook load checks
	inline void HookUnits()
	{
		struct Site
		{
			const char*   name;
			std::uint64_t va;
			std::uint8_t  bytes[5];
			std::uint64_t target;
		};
		// re\scripts\plan\hook_sites.out (Fallout4.exe 1.11.240 copy): the real bytes at the 4 sites
		const Site sites[] = {
			{ "P9a late pose", 0x140db9dc4, { 0xe8, 0x47, 0xe8, 0xea, 0xff }, 0x140c68610 },
			{ "P9b late pose", 0x140db428a, { 0xe8, 0x81, 0x43, 0xeb, 0xff }, 0x140c68610 },
			{ "knock A", 0x140c4db39, { 0xe8, 0x72, 0x8f, 0x0f, 0x00 }, 0x140d46ab0 },
			{ "paralyze B", 0x140c4db7c, { 0xe8, 0x1f, 0x7c, 0x0e, 0x00 }, 0x140d357a0 },
		};
		for (const auto& s : sites) {
			CHECK(A::CheckCall(s.bytes, s.va, s.target) == O::Hook::kOk, "S2-H1 [%s] %s: the 1.11.240 bytes pass the load check", A::Impl(), s.name);
			std::uint8_t jmp[5];
			std::memcpy(jmp, s.bytes, 5);
			jmp[0] = 0xE9;  // another mod already detoured the call
			CHECK(A::CheckCall(jmp, s.va, s.target) == O::Hook::kOpcode, "S2-H2 [%s] %s: a jmp instead of the call is refused", A::Impl(), s.name);
			std::uint8_t moved[5];
			std::memcpy(moved, s.bytes, 5);
			moved[1] = static_cast<std::uint8_t>(moved[1] + 0x40);  // re-pointed rel32 (a trampoline)
			CHECK(A::CheckCall(moved, s.va, s.target) == O::Hook::kTarget, "S2-H2 [%s] %s: a call to another target is refused", A::Impl(), s.name);
			CHECK(A::CheckCall(s.bytes, s.va + 1, s.target) != O::Hook::kOk, "S2-H2 [%s] %s: a shifted site (other exe version) is refused", A::Impl(), s.name);
		}
		const std::uint8_t typo[5] = { 0xe8, 0x47, 0xe8, 0xea, 0xfe };
		CHECK(A::CheckCall(sites[0].bytes, sites[0].va, sites[0].target, sites[0].bytes) == O::Hook::kOk &&
				  A::CheckCall(sites[0].bytes, sites[0].va, sites[0].target, typo) != O::Hook::kOk,
			"S2-H2 [%s] with the exact 5 bytes embedded, a mismatch is refused", A::Impl());
		CHECK(A::CheckCall(nullptr, 0x140db9dc4, 0x140c68610) == O::Hook::kNull && A::CheckCall(sites[0].bytes, 0x140db9dc4, 0) == O::Hook::kNull,
			"S2-H2 [%s] a missing address (unresolved id) is refused, never dereferenced", A::Impl());
		struct Slot
		{
			int           idx;
			std::uint64_t fn;
		};
		const Slot slots[] = { { 0xAD, 0x140cab070 }, { 0xCA, 0x140c60b50 }, { 0x105, 0x140c83990 }, { 0xCF, 0x140c648a0 } };
		for (const auto& s : slots) {
			CHECK(A::CheckSlot(s.fn, s.fn) == O::Hook::kOk, "S2-H3 [%s] VTABLE_Actor slot %#x holds its function", A::Impl(), s.idx);
			CHECK(A::CheckSlot(s.fn + 0x30, s.fn) == O::Hook::kSlot && A::CheckSlot(0, s.fn) == O::Hook::kNull, "S2-H3 [%s] slot %#x re-pointed / empty: refused", A::Impl(),
				s.idx);
		}
	}

	// ============================================================================================== S2-K core-only rules
	// Details the core lane added on top of the plan (HOOKS.md D-b / D-d / D-e): checked on the core only.
	inline void CoreExtras()
	{
#if S2_HAVE_CORE
		namespace LK = sp::look;
		// shape filter info first (glass panes inside statics), the body's layer when the shape carries none
		CHECK(LK::HitLayer(0x00120003u, 1) == 3 && LK::HitLayer(0, 1) == 1 && LK::HitLayer(0xFFFFFFFFu, 13) == 13 && LK::HitLayer(0x00120000u, 9) == 9,
			"S2-K1 HitLayer: the shape layer wins, an empty / all-ones / layer-0 shape info falls back to the body layer");
		CHECK(!((LK::kMaskSight >> LK::HitLayer(0x00120003u, 1)) & 1), "S2-K1 a glass pane inside a static does not block sight");
		// his filter info: char controller layer 30 with a system group; 0 = no process / controller -> the old path
		CHECK(LK::FilterUsable(0x0ABC001Eu) && !LK::FilterUsable(0) && !LK::FilterUsable(0x0ABC0008u) && !LK::FilterUsable(0x0000001Eu) &&
				  !LK::FilterUsable(0x0ABC401Eu),
			"S2-K2 FilterUsable: only a layer-30 filter info with a non-zero group and no 'no collision' bit goes into a pick");
		// Floor(): stops on water (no blink spot over a lake), Wings / floor mask does not
		CHECK(((LK::kMaskFloorRay >> 11) & 1) && !((LK::kMaskFloor >> 11) & 1) && (LK::kMaskFloorRay & ~(1ull << 11)) == LK::kMaskFloor,
			"S2-K3 the brain's Floor() mask = floor + water (11), the Wings mask has no water");
		CHECK(LK::kDataDefinedFixed == (1ull << 55) && LK::kDataDefinedOld == (0x7FFFull << 48), "S2-K3 data-defined block: 55 only (old 48-62)");
		// continuity details
		LK::JumpIn off;
		off.dP = { 5000, 0, 0 };
		off.legacyDist = 0;
		off.fixOn = true;
		CHECK(LK::ContinuityEvent(off) == LK::JumpWhy::kNone, "S2-K4 runtime.teleportDist 0 (teleport carry off) = never an event, fix on or off");
		LK::JumpIn nan;
		nan.dP = { std::nan(""), 0, 0 };
		nan.fixOn = true;
		CHECK(LK::ContinuityEvent(nan) == LK::JumpWhy::kDistance, "S2-K4 a non-finite position is an event (re-seat on the safe side)");
		LK::JumpIn fl;
		fl.flagged = true;
		fl.fixOn = true;
		CHECK(LK::ContinuityEvent(fl) == LK::JumpWhy::kFlagged && std::string(LK::JumpWhyName(LK::JumpWhy::kFlagged)) == "flagged",
			"S2-K4 a flagged relocation reports why=flagged");
		LK::SpaceKey a{ 0x3C, 0x1001, false, true }, b{ 0x3C, 0x1002, false, true }, c{ 0, 0x2001, true, true }, d{ 0, 0x2002, true, true }, inval{};
		CHECK(!LK::SpaceChanged(a, b) && LK::SpaceChanged(a, c) && LK::SpaceChanged(c, d) && !LK::SpaceChanged(inval, c),
			"S2-K4 SpaceChanged: outdoor cell no, exterior -> interior yes, interior -> other interior yes, unknown start no");
		// step-2 fixer (REVIEW F2): the L16 "real relocation" rule
		CHECK(LK::RealRelocation(LK::kRelocWarpTo, false, 0, 400) && LK::RealRelocation(LK::kRelocDoor, true, 0, 400) && LK::RealRelocation(LK::kRelocReset, true, 5, 400) &&
				  !LK::RealRelocation(LK::kRelocSetPosTask, true, 0, 400) && !LK::RealRelocation(LK::kRelocSetPosTask, true, 399, 400) &&
				  LK::RealRelocation(LK::kRelocSetPosTask, true, 1600, 400) && !LK::RealRelocation(LK::kRelocSetPosTask, false, 0, 400) &&
				  !LK::RealRelocation(0xCAB306u, true, 50, 400) && LK::RealRelocation(0xCAB306u, true, 900, 400),
			"S2-K4b L16 rule: WarpTo / door / Reset by the caller alone; the queued set-position task (0 u in soak10) and any other caller only beyond look.jumpTol");
		// tick gate details
		LK::TickGateIn g;
		g.fixOn = true;
		g.dtPositive = false;
		g.p6Clock = 1;
		CHECK(LK::GateTick(g) == LK::TickGate::kNoDt, "S2-K5 BSTimer delta 0 = no tick");
		g.dtPositive = true;
		g.p6Clock = -1;
		g.boundLoaded = true;
		g.p6EverLive = false;
		CHECK(LK::GateTick(g) == LK::TickGate::kFallback, "S2-K5 his P6 never ran this session (hook broken): the old fallback keeps him moving");
		g.p6EverLive = true;
		CHECK(LK::GateTick(g) == LK::TickGate::kNoP6Frame && std::string(LK::TickGateName(LK::GateTick(g))) == "noP6Frame",
			"S2-K5 bound + loaded, P6 live before: no fallback tick (why=noP6Frame)");
		// look.freezeNoP6Max (shipped 1 s): his update skipped longer than that (off screen) = the old fallback again, so his
		// brain / flings / commands never stall; menus and freezeTime stop the tick with no cap
		g.noP6MaxS = 1.0;
		g.p6GapS = 0.5;
		const bool shortGap = LK::GateTick(g) == LK::TickGate::kNoP6Frame;
		g.p6GapS = 1.5;
		const bool longGap = LK::GateTick(g) == LK::TickGate::kFallback;
		g.p6GapS = 30.0;
		g.freezeTime = true;
		const bool frozenLong = LK::GateTick(g) == LK::TickGate::kFrozen;
		g.freezeTime = false;
		g.inMenuMode = true;
		const bool menuLong = LK::GateTick(g) == LK::TickGate::kMenu;
		CHECK(shortGap && longGap && frozenLong && menuLong,
			"S2-K5 no-P6 gap 0.5 s: no tick; 1.5 s: the fallback again (update skipped off screen); freezeTime / menu 30 s: still no tick (%d %d %d %d)", shortGap, longGap,
			frozenLong, menuLong);
		{  // step-2 fixer (REVIEW F1): the P6 clock NOT ASKED (fo4.p5.tickSource 0 / fo4.p5.dedupe 0) = the old clocks decide
			LK::TickGateIn na;
			na.fixOn = true;
			na.p6Clock = -2;
			na.boundLoaded = true;
			na.p6EverLive = true;
			na.p6GapS = 0.008;  // his P6 ran a frame ago
			na.noP6MaxS = 1.0;
			const bool fb = LK::GateTick(na) == LK::TickGate::kFallback;
			na.inMenuMode = true;
			const bool menu = LK::GateTick(na) == LK::TickGate::kMenu;
			na.inMenuMode = false;
			na.freezeTime = true;
			const bool frz = LK::GateTick(na) == LK::TickGate::kFrozen;
			CHECK(fb && menu && frz, "S2-K5 P6 clock not asked (tickSource 0): bound + loaded + P6 live 8 ms ago -> kFallback (he ticks), menu / freezeTime still stop it (%d %d %d)",
				fb, menu, frz);
		}
		// step-2 fixer (REVIEW F9): fix c applies to wall / floor / path rays; sight only with look.pickFailedSight 1
		CHECK(!LK::PickFixFor(LK::RayUse::kSight, true, false) && LK::PickFixFor(LK::RayUse::kSight, true, true) && LK::PickFixFor(LK::RayUse::kWalls, true, false) &&
				  LK::PickFixFor(LK::RayUse::kFloor, true, false) && LK::PickFixFor(LK::RayUse::kFloorRay, true, false) && !LK::PickFixFor(LK::RayUse::kWalls, false, true),
			"S2-K6b fix c on wall / floor rays; a refused SIGHT ray stays not-blocked unless look.pickFailedSight 1");
		// refused picks: the verdict names the refusal
		CHECK(LK::JudgePick(false, true, false) == LK::PickVerdict::kRefusedClear && LK::JudgePick(false, true, true) == LK::PickVerdict::kRefusedBlocked &&
				  LK::JudgePick(true, true, true) == LK::PickVerdict::kHit,
			"S2-K6 JudgePick tells a refusal apart (counted as refused, blocked with the fix)");
		std::printf("info: S2-K core-only rules checked\n");
#else
		std::printf("info: S2-K skipped (no core/look_rules.h)\n");
#endif
	}

	inline void RunLook0()
	{
		std::printf("info: s2 look-0 cases on the %s implementation\n", A::Impl());
		HitSinkUnits();
		HitSinkLoop();
		MaskUnits();
		PickLoops();
		RefusedUnits();
		RefusedLoop();
		JumpUnits();
		JumpLoops();
		FreezeUnits();
		FreezeLoops();
		HookUnits();
		CoreExtras();
	}
}
