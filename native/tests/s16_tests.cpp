// OWNER: R6 step 16 (finish B lane; finish\B_step16.md). Desktop tests of core/engine_hits.h (his punches as real engine hits, A/B,
// shipped OFF). No game: a FakeEngine stands in for HitData / DoHitMe / the health actor value.
//   EH-01  the Decide truth table: every veto alone, in its documented order; the keys (off / survivors / crime / cap / broken)
//   EH-02  DoHitMe is NEVER reached for a non-hostile / friend / protected / turned / settler / kill-move / crime victim:
//          10 000 random victims through the NativeDamage model (the acceptance "mock Verdict" row of STEP_ORDER step 16)
//   EH-03  the damage split: engine + rest == dmg, the engine part never kills (leaves keepHp), the rest kills; fuzz
//   EH-04  the HitData plan (fields listed in TABS_LOOK_PLAN rank 11 / HIT 11.3) and the offsets (= CommonLibF4 HitData.h)
//   EH-05  the load proofs: right bytes pass, a changed byte / a detour E9 / a wrong RVA refuse (the feature never runs)
//   EH-06  the NativeDamage model end to end: total damage unchanged, our kill still kills (instant ragdoll path kept),
//          a short engine part (perk / god mode) is made up by the rest, a faulting engine falls back to the old write
//   EH-07  the A/B probe counters (allies on him / on the player, share)
//   EH-08  the shipped ini: look.engineHits = 0, no TEST key (look.engineHitsProbe*), LF, no BOM
// Build: xmake build s16_tests ; run from the tree root: build\windows\x64\releasedbg\s16_tests.exe [--ini <f>]
// Prints S16|<id>|PASS/FAIL|detail lines; exit 0 = no FAIL. Never shipped.
#include "config.h"
#include "core/engine_hits.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	namespace EH = sp::enginehits;
	int g_pass = 0, g_fail = 0;
	void Row(const char* a_id, bool a_ok, const std::string& a_detail)
	{
		std::printf("S16|%s|%s|%s\n", a_id, a_ok ? "PASS" : "FAIL", a_detail.c_str());
		(a_ok ? g_pass : g_fail)++;
	}

	EH::Facts Raider(float a_hp = 100.0f)
	{
		EH::Facts f;
		f.valid = true;
		f.hostileToPlayer = true;
		f.hp = a_hp;
		return f;
	}
	EH::Keys On()
	{
		EH::Keys k;
		k.on = true;
		return k;
	}

	// ---------------------------------------------------------------- EH-01
	void Eh01()
	{
		const EH::Keys k = On();
		struct C
		{
			const char* name;
			void (*set)(EH::Facts&);
			EH::Why     want;
		};
		const C cases[] = {
			{ "invalid", [](EH::Facts& f) { f.valid = false; }, EH::Why::kInvalid },
			{ "player", [](EH::Facts& f) { f.player = true; }, EH::Why::kPlayer },
			{ "him", [](EH::Facts& f) { f.him = true; }, EH::Why::kHim },
			{ "dead", [](EH::Facts& f) { f.dead = true; }, EH::Why::kDead },
			{ "teammate", [](EH::Facts& f) { f.teammate = true; }, EH::Why::kTeammate },
			{ "commanded", [](EH::Facts& f) { f.commanded = true; }, EH::Why::kCommanded },
			{ "essentialOrProtected", [](EH::Facts& f) { f.essentialOrProtected = true; }, EH::Why::kProtected },
			{ "turned", [](EH::Facts& f) { f.turned = true; }, EH::Why::kTurned },
			{ "settler", [](EH::Facts& f) { f.settler = true; }, EH::Why::kSettler },
			{ "killMove", [](EH::Facts& f) { f.killMove = true; }, EH::Why::kKillMove },
			{ "notHostile", [](EH::Facts& f) { f.hostileToPlayer = false; }, EH::Why::kNotHostile },
			{ "crimeFaction", [](EH::Facts& f) { f.crimeFaction = true; }, EH::Why::kCrime },
			{ "lowHp", [](EH::Facts& f) { f.hp = 1.5f; }, EH::Why::kLowHp },
		};
		bool        all = true;
		std::string bad;
		for (const auto& c : cases) {
			EH::Facts f = Raider();
			c.set(f);
			const EH::Why got = EH::Decide(k, true, false, 0, f, 30000.0f);
			if (got != c.want) {
				all = false;
				bad += std::format(" {}->{}", c.name, EH::WhyName(got));
			}
		}
		Row("EH-01a", all, "every veto alone gives its own why" + bad);
		Row("EH-01b", EH::Decide(k, true, false, 0, Raider(), 30000.0f) == EH::Why::kOk, "a plain hostile raider, a killing punch: ok");
		EH::Keys off;
		Row("EH-01c", EH::Decide(off, true, false, 0, Raider(), 30000.0f) == EH::Why::kOff, "look.engineHits 0 (the code default and the shipped value): off");
		Row("EH-01d", EH::Decide(k, false, false, 0, Raider(), 30000.0f) == EH::Why::kBroken && EH::Decide(k, true, true, 0, Raider(), 30000.0f) == EH::Why::kBroken,
			"a failed load proof or a tripped crash shield: never");
		Row("EH-01e", EH::Decide(k, true, false, 4, Raider(), 30000.0f) == EH::Why::kCap && EH::Decide(k, true, false, 3, Raider(), 30000.0f) == EH::Why::kOk,
			"at most look.engineHitsPerPump (4) a pump");
		Row("EH-01f", EH::Decide(k, true, false, 0, Raider(500.0f), 100.0f) == EH::Why::kSurvivor, "a punch that does not kill: old path (look.engineHitsSurvivors 0)");
		EH::Keys ks = k;
		ks.survivors = true;
		Row("EH-01g", EH::Decide(ks, true, false, 0, Raider(500.0f), 100.0f) == EH::Why::kOk, "look.engineHitsSurvivors 1: the non-lethal punch too");
		EH::Facts g = Raider();
		g.crimeFaction = true;
		EH::Keys kc = k;
		kc.crime = true;
		Row("EH-01h", EH::Decide(kc, true, false, 0, g, 30000.0f) == EH::Why::kOk, "look.engineHitsCrime 1: crime-faction victims too");
		Row("EH-01i", EH::Decide(k, true, false, 0, Raider(), std::nanf("")) == EH::Why::kBadDmg && EH::Decide(k, true, false, 0, Raider(), -5.0f) == EH::Why::kBadDmg,
			"NaN / negative damage: never");
		// order: a dead teammate player is "player" first (the log names the strongest reason)
		EH::Facts multi = Raider();
		multi.player = true;
		multi.teammate = true;
		multi.dead = true;
		Row("EH-01j", EH::Decide(k, true, false, 0, multi, 30000.0f) == EH::Why::kPlayer, "several vetoes: the first in the documented order");
		bool names = true;
		for (int i = 0; i < EH::kWhyN; ++i) names = names && std::string(EH::WhyName(static_cast<EH::Why>(i))) != "?";
		Row("EH-01k", names, "every why has a log name");
	}

	// ---------------------------------------------------------------- the FakeEngine + the NativeDamage model
	struct FakeVictim
	{
		EH::Facts f;
		float     hp{ 100.0f };
		bool      dead{ false };
		bool      instantRagdoll{ false };
		int       doHitMe{ 0 };       // DoHitMe calls on it
		float     doHitMeDamage{ 0 };  // healthDamage it received from DoHitMe
		bool      engineSawDeath{ false };
	};
	struct FakeEngine
	{
		bool  faultDoHitMe{ false };
		float shortBy{ 0.0f };  // the engine applies this much less (a perk / god mode)
		int   calls{ 0 };
		// DoHitMe as the game does it for our HitData: DoDamage(healthDamage) - never a kill here unless hp hits 0
		bool DoHitMe(FakeVictim& v, const EH::Plan& p)
		{
			++calls;
			++v.doHitMe;
			if (faultDoHitMe) return false;
			const float d = std::max(0.0f, p.health - shortBy);
			v.doHitMeDamage += d;
			v.hp -= d;
			if (v.hp <= 0.0f) v.engineSawDeath = true;
			return true;
		}
	};
	// runtime NativeDamage with step 16, restated: TryHit (Decide, Split, the HitData plan, DoHitMe, measured dealt), then the old
	// write of the rest and the old kill (KillImpl with the instant ragdoll) when health <= 0 and the victim is killable
	struct Outcome
	{
		float dealtEngine{ 0 }, dealtOld{ 0 };
		bool  killed{ false };
		EH::Why why{ EH::Why::kOff };
		bool  broken{ false };
	};
	Outcome NativeDamageModel(const EH::Keys& k, FakeEngine& e, FakeVictim& v, float dmg, bool& a_broken, int a_used = 0)
	{
		Outcome o;
		v.f.hp = v.hp;
		o.why = EH::Decide(k, true, a_broken, a_used, v.f, dmg);
		float engineDealt = 0.0f;
		if (o.why == EH::Why::kOk) {
			const EH::Split s = EH::SplitDamage(v.hp, dmg, k.keepHp);
			if (s.engine > 0.0f) {
				const float hp0 = v.hp;
				const EH::Plan p = EH::MakePlan(s.engine);
				if (!e.DoHitMe(v, p)) a_broken = true;
				engineDealt = std::max(0.0f, hp0 - v.hp);
			}
		}
		o.dealtEngine = engineDealt;
		const float rest = dmg - engineDealt;
		if (rest > 0.0f) {
			v.hp -= rest;
			o.dealtOld = rest;
		}
		const bool killable = !v.f.essentialOrProtected && !v.f.turned;
		if (v.hp <= 0.0f && killable && !v.dead) {
			v.dead = true;
			v.instantRagdoll = true;  // KillImpl(ragdollInstant = true): the step-13 kill
			o.killed = true;
		}
		o.broken = a_broken;
		return o;
	}

	// ---------------------------------------------------------------- EH-02
	void Eh02()
	{
		std::mt19937 rng(16016);
		std::uniform_int_distribution<int> coin(0, 9);
		std::uniform_real_distribution<float> hp(0.5f, 3000.0f);
		const EH::Keys k = On();
		int reached = 0, reachedBad = 0, eligible = 0, eligibleReached = 0;
		for (int i = 0; i < 10000; ++i) {
			FakeVictim v;
			auto b = [&] { return coin(rng) == 0; };  // each fact true 10 % of the time
			v.f.valid = coin(rng) != 0;
			v.f.dead = b();
			v.f.player = b();
			v.f.him = b();
			v.f.teammate = b();
			v.f.commanded = b();
			v.f.essentialOrProtected = b();
			v.f.turned = b();
			v.f.settler = b();
			v.f.killMove = b();
			v.f.hostileToPlayer = coin(rng) < 6;
			v.f.crimeFaction = b();
			v.hp = hp(rng);
			const float dmg = coin(rng) < 8 ? 30000.0f : 50.0f;
			const float hp0 = v.hp;  // (the model takes the damage off v.hp)
			FakeEngine e;
			bool       broken = false;
			NativeDamageModel(k, e, v, dmg, broken);
			const bool friendOrSafe = !v.f.valid || v.f.dead || v.f.player || v.f.him || v.f.teammate || v.f.commanded || v.f.essentialOrProtected || v.f.turned ||
			                          v.f.settler || v.f.killMove || !v.f.hostileToPlayer || v.f.crimeFaction;
			if (e.calls) ++reached;
			if (e.calls && friendOrSafe) ++reachedBad;
			const bool want = !friendOrSafe && dmg >= hp0 && hp0 >= 2.0f;
			if (want) {
				++eligible;
				if (e.calls == 1) ++eligibleReached;
			}
		}
		Row("EH-02a", reachedBad == 0, std::format("10000 random victims: DoHitMe reached {} times, {} of them on a friend / non-hostile / protected / turned / settler / kill-move / crime / dead / invalid victim (must be 0)", reached, reachedBad));
		Row("EH-02b", eligible > 0 && eligibleReached == eligible, std::format("every plain hostile victim of a killing punch (hp >= 2) went through the engine exactly once ({}/{})", eligibleReached, eligible));
		// the named friends of the user rules, one by one (a killing punch each)
		struct N
		{
			const char* who;
			void (*set)(EH::Facts&);
		};
		const N named[] = {
			{ "the player", [](EH::Facts& f) { f.player = true; } },
			{ "a companion (teammate)", [](EH::Facts& f) { f.teammate = true; } },
			{ "Danse turned on the player (turned, essential)", [](EH::Facts& f) { f.turned = true; f.essentialOrProtected = true; } },
			{ "a settler (base WorkshopNPCFaction)", [](EH::Facts& f) { f.settler = true; } },
			{ "a settler whose stray shot hit the player (not hostile)", [](EH::Facts& f) { f.settler = true; f.hostileToPlayer = false; } },
			{ "a provoked trader / guard (crime faction)", [](EH::Facts& f) { f.crimeFaction = true; } },
			{ "a neutral (not hostile)", [](EH::Facts& f) { f.hostileToPlayer = false; } },
			{ "an essential quest NPC", [](EH::Facts& f) { f.essentialOrProtected = true; } },
			{ "a victim in a kill move", [](EH::Facts& f) { f.killMove = true; } },
		};
		bool        ok = true;
		std::string det;
		for (const auto& n : named) {
			FakeVictim v;
			v.f = Raider(150.0f);
			n.set(v.f);
			v.hp = 150.0f;
			FakeEngine e;
			bool       br = false;
			NativeDamageModel(On(), e, v, 30000.0f, br);
			if (e.calls != 0) {
				ok = false;
				det += std::format(" [{}: {} DoHitMe]", n.who, e.calls);
			}
		}
		Row("EH-02c", ok, "user-rule friends (player, companion, turned Danse, settler incl. a stray-fire settler, guard / trader, neutral, essential, kill move): 0 DoHitMe" + det);
	}

	// ---------------------------------------------------------------- EH-03
	void Eh03()
	{
		std::mt19937 rng(316);
		std::uniform_real_distribution<float> hp(0.01f, 5000.0f), dmg(0.01f, 40000.0f), keep(0.1f, 10.0f);
		int bad = 0, n = 0;
		std::string first;
		for (int i = 0; i < 20000; ++i) {
			const float h = hp(rng), d = dmg(rng), kp = keep(rng);
			const EH::Split s = EH::SplitDamage(h, d, kp);
			++n;
			const bool sum = std::abs((s.engine + s.rest) - d) <= 1e-3f * std::max(1.0f, d);
			const bool lethalOk = s.lethal == (d >= h);
			const bool neverKills = !s.lethal || (h - s.engine >= std::min(kp, h) - 1e-3f);
			const bool restKills = !s.lethal || (h - s.engine - s.rest <= 1e-2f);
			const bool nonNeg = s.engine >= 0.0f && s.rest >= 0.0f;
			if (!(sum && lethalOk && neverKills && restKills && nonNeg)) {
				if (bad++ == 0) first = std::format(" first: hp {} dmg {} keep {} -> engine {} rest {} lethal {}", h, d, kp, s.engine, s.rest, s.lethal);
			}
		}
		Row("EH-03a", bad == 0, std::format("{} random splits: engine + rest == dmg, the engine part never kills (leaves keepHp), the rest kills ({} bad){}", n, bad, first));
		const EH::Split a = EH::SplitDamage(100.0f, 30000.0f, 1.0);
		Row("EH-03b", a.lethal && std::abs(a.engine - 99.0f) < 1e-4f && std::abs(a.rest - 29901.0f) < 1e-2f, std::format("hp 100, TABS 30000, keep 1: engine {} rest {}", a.engine, a.rest));
		const EH::Split b = EH::SplitDamage(500.0f, 120.0f, 1.0);
		Row("EH-03c", !b.lethal && b.engine == 120.0f && b.rest == 0.0f, "non-lethal: the engine takes it all (only used with look.engineHitsSurvivors 1)");
		const EH::Split c = EH::SplitDamage(std::nanf(""), 30000.0f, 1.0), d = EH::SplitDamage(100.0f, -1.0f, 1.0);
		Row("EH-03d", c.engine == 0.0f && c.rest == 0.0f && d.engine == 0.0f, "NaN health / negative damage: no engine part");
	}

	// ---------------------------------------------------------------- EH-04
	void Eh04()
	{
		const EH::Plan p = EH::MakePlan(99.0f);
		const bool     fields = p.health == 99.0f && p.physical == 99.0f && p.total == 0.0f && p.limb == 0.0f && p.blocked == 0.0f && p.stagger == 0u &&
		                    p.pushBack == 0.0f && p.reflected == 0.0f && p.critMult == 1.0f && p.sneak == 1.0f && p.bonusMult == 1.0f && p.clearAttackData &&
		                    p.clearCritEffect && p.clearHitEffect;
		Row("EH-04a", fields, "plan: health = physical = the engine part, total 0, limb 0, blocked 0, stagger 0, pushBack 0, reflected 0, crit mult 1, no attack data / crit / hit spell");
		const std::uint32_t want = (1u << 18) | (1u << 12);
		Row("EH-04b", p.flags == want, std::format("flags = kMeleeAttack | kIgnoreCritical only (0x{:X}; no crit / fatal / dismember / explode / cripple / bash / power / blocked)", p.flags));
		// CommonLibF4 RE/H/HitData.h member comments (the DLL static_asserts the same numbers against the real struct)
		const bool offs = EH::hd::kSize == 0xE0 && EH::hd::kAttackData == 0x50 && EH::hd::kCritEffect == 0x68 && EH::hd::kHitEffect == 0x70 && EH::hd::kHealth == 0x90 &&
		                  EH::hd::kTotal == 0x94 && EH::hd::kPhysical == 0x98 && EH::hd::kLimb == 0x9C && EH::hd::kBlocked == 0xA0 && EH::hd::kResPhys == 0xA4 &&
		                  EH::hd::kResTyped == 0xA8 && EH::hd::kStagger == 0xAC && EH::hd::kSneak == 0xB0 && EH::hd::kBonusMult == 0xB4 && EH::hd::kPushBack == 0xB8 &&
		                  EH::hd::kReflected == 0xBC && EH::hd::kCritMult == 0xC0 && EH::hd::kFlags == 0xC4 && EH::hd::kImpactLoc == 0 && EH::hd::kImpactNormal == 0x10 &&
		                  EH::hd::kImpactVel == 0x20;
		Row("EH-04c", offs, "HitData offsets = CommonLibF4 HitData.h (0xE0; health 0x90, flags 0xC4, ...)");
	}

	// ---------------------------------------------------------------- EH-05
	void Eh05()
	{
		// a fake exe made from the proof texts themselves ("xx" filled with 0xAB): the reader returns those bytes
		auto parse = [](const char* hex, std::vector<std::uint8_t>& out) {
			std::uint8_t b[32]{}, any[32]{};
			const int    n = sp::r6check::ParseHex(hex, b, any, 32);
			out.clear();
			for (int i = 0; i < n; ++i) out.push_back(any[i] ? 0xAB : b[i]);
			return n;
		};
		std::vector<std::vector<std::uint8_t>> img;
		bool lens = true;
		for (const auto& p : EH::kProofs) {
			img.emplace_back();
			const int n = parse(p.hex, img.back());
			lens = lens && n > 0 && n <= 32;
		}
		Row("EH-05a", lens && std::size(EH::kProofs) == 5, "5 proofs (DoHitMe, HitData ctor / InitializeHitData / dtor, GetCrimeTrackingFaction), each 1-32 bytes");
		int  mode = 0;  // 0 right, 1 one byte changed, 2 E9 at +0, 3 wrong RVA
		auto reader = [&](std::uint64_t id, std::uint32_t& rva, std::uint32_t off, std::uint8_t* out, int n) -> bool {
			for (std::size_t i = 0; i < std::size(EH::kProofs); ++i) {
				if (EH::kProofs[i].id != id) continue;
				rva = EH::kProofs[i].rva + (mode == 3 ? 0x10u : 0u);
				for (int k = 0; k < n; ++k) out[k] = img[i][off + k];
				if (mode == 1 && n > 3) out[3] ^= 0x01;
				if (mode == 2) out[0] = 0xE9;
				return true;
			}
			return false;
		};
		std::string whyAll;
		auto log = [&](const sp::r6check::Proof&, bool, const std::string& w) { whyAll += w.empty() ? "" : w + "; "; };
		mode = 0;
		const bool ok0 = EH::RunProofs(reader, log);
		mode = 1;
		whyAll.clear();
		const bool ok1 = EH::RunProofs(reader, log);
		const std::string why1 = whyAll;
		mode = 2;
		const bool ok2 = EH::RunProofs(reader, log);
		mode = 3;
		whyAll.clear();
		const bool ok3 = EH::RunProofs(reader, log);
		Row("EH-05b", ok0, "the exe copy's bytes: every proof passes");
		Row("EH-05c", !ok1 && why1.find("bytes") != std::string::npos, "one byte changed: refused (" + why1.substr(0, 60) + ")");
		Row("EH-05d", !ok2, "a detour (E9 at +0): refused");
		Row("EH-05e", !ok3 && whyAll.find("rva") != std::string::npos, "a wrong Address Library RVA: refused");
		// the glue refuses to run when the proofs fail: Decide(..., proven = false) is "broken"
		Row("EH-05f", EH::Decide(On(), false, false, 0, Raider(), 30000.0f) == EH::Why::kBroken, "not proven -> never DoHitMe (the old health write)");
	}

	// ---------------------------------------------------------------- EH-06
	void Eh06()
	{
		{
			FakeVictim v;
			v.f = Raider();
			v.hp = 100.0f;
			FakeEngine e;
			bool       br = false;
			const auto o = NativeDamageModel(On(), e, v, 30000.0f, br);
			Row("EH-06a", e.calls == 1 && std::abs(v.doHitMeDamage - 99.0f) < 1e-3f && !v.engineSawDeath && o.killed && v.instantRagdoll &&
			                  std::abs(o.dealtEngine + o.dealtOld - 30000.0f) < 1e-2f,
				std::format("raider hp 100, TABS 30000: DoHitMe 99 (alive after it), old write {:.0f}, killed by OUR kill with the instant ragdoll", o.dealtOld));
		}
		{
			FakeVictim v;
			v.f = Raider();
			v.hp = 100.0f;
			FakeEngine e;
			e.shortBy = 40.0f;  // a perk / god mode took 40 off
			bool       br = false;
			const auto o = NativeDamageModel(On(), e, v, 30000.0f, br);
			Row("EH-06b", o.killed && std::abs(o.dealtEngine - 59.0f) < 1e-3f && std::abs(o.dealtOld - (30000.0f - 59.0f)) < 1e-2f,
				"the engine took less (59 of 99): the measured rest is written the old way, still killed");
		}
		{
			FakeVictim v;
			v.f = Raider();
			v.hp = 100.0f;
			FakeEngine e;
			e.faultDoHitMe = true;
			bool       br = false;
			const auto o = NativeDamageModel(On(), e, v, 30000.0f, br);
			FakeVictim v2;
			v2.f = Raider();
			v2.hp = 100.0f;
			FakeEngine e2;
			NativeDamageModel(On(), e2, v2, 30000.0f, br);
			Row("EH-06c", o.killed && o.dealtEngine == 0.0f && br && e2.calls == 0 && v2.dead,
				"DoHitMe faults: this punch gets the whole damage the old way, the feature is off for the session (the next punch: 0 DoHitMe, still killed)");
		}
		{
			FakeVictim v;
			v.f = Raider(500.0f);
			v.hp = 500.0f;
			FakeEngine e;
			bool       br = false;
			const auto o = NativeDamageModel(On(), e, v, 100.0f, br);
			Row("EH-06d", e.calls == 0 && !o.killed && std::abs(v.hp - 400.0f) < 1e-3f, "a non-lethal punch (survivors 0): the old write only, unchanged");
		}
		{
			FakeVictim v;
			v.f = Raider();
			v.f.essentialOrProtected = true;
			v.hp = 100.0f;
			FakeEngine e;
			bool       br = false;
			const auto o = NativeDamageModel(On(), e, v, 30000.0f, br);
			Row("EH-06e", e.calls == 0 && !o.killed, "an essential: no DoHitMe, never killed (the spare rule of the old path)");
		}
		{
			// look.engineHits 0: byte-for-byte the old path
			FakeVictim a, b;
			a.f = b.f = Raider();
			a.hp = b.hp = 100.0f;
			FakeEngine ea, eb;
			bool       br = false;
			EH::Keys   off;
			const auto oa = NativeDamageModel(off, ea, a, 30000.0f, br);
			const auto ob = NativeDamageModel(off, eb, b, 30000.0f, br);
			Row("EH-06f", ea.calls == 0 && oa.killed && ob.killed && a.hp == b.hp && oa.dealtOld == 30000.0f, "look.engineHits 0: no DoHitMe, the old write of the whole damage");
		}
	}

	// ---------------------------------------------------------------- EH-07
	void Eh07()
	{
		EH::AggroCounts c;
		EH::Count(c, EH::Tgt::kPlayer, EH::Tgt::kHim, true);
		EH::Count(c, EH::Tgt::kPlayer, EH::Tgt::kPlayer, true);
		EH::Count(c, EH::Tgt::kHim, EH::Tgt::kHim, true);
		EH::Count(c, EH::Tgt::kPlayer, EH::Tgt::kNone, false);
		Row("EH-07a", c.n == 3 && c.him == 2 && c.player == 1 && c.player0 == 3 && c.him0 == 1 && c.gone == 1 && std::abs(EH::HimShare(c) - 2.0 / 3.0) < 1e-9,
			"allies: 3 alive (2 on him, 1 on the player), 1 gone; share on him 0.67");
		EH::AggroCounts z;
		Row("EH-07b", EH::HimShare(z) < 0.0, "no living ally: share -1 (not counted)");
	}

	// ---------------------------------------------------------------- EH-08
	void Eh08(const std::string& a_ini)
	{
		std::ifstream f(a_ini, std::ios::binary);
		if (!f) {
			Row("EH-08", false, "cannot read " + a_ini);
			return;
		}
		std::stringstream ss;
		ss << f.rdbuf();
		const std::string s = ss.str();
		const bool bom = s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF;
		const bool cr = s.find('\r') != std::string::npos;
		Row("EH-08a", !bom && !cr, "the shipped ini: LF line ends, no BOM");
		sp::cfg::LoadIni(a_ini, nullptr);
		const double v = sp::cfg::Tunable("look.engineHits", -1.0);
		Row("EH-08b", v == 0.0, std::format("look.engineHits = {} in the shipped ini (0 until the A/B wins)", v));
		bool       testKey = false;
		std::istringstream in(s);
		std::string        line;
		while (std::getline(in, line)) {
			const auto b = line.find_first_not_of(" \t");
			if (b == std::string::npos) continue;
			if (line.compare(b, 20, "look.engineHitsProbe") == 0) testKey = true;
		}
		Row("EH-08c", !testKey, "no TEST key (look.engineHitsProbe*) active in the shipped ini");
	}
}

int main(int argc, char** argv)
{
	std::string ini = "SuperPeasantNative.ini";
	for (int i = 1; i + 1 < argc; ++i)
		if (std::string(argv[i]) == "--ini") ini = argv[i + 1];
	Eh01();
	Eh02();
	Eh03();
	Eh04();
	Eh05();
	Eh06();
	Eh07();
	Eh08(ini);
	std::printf("s16_tests: %d passed, %d failed -> %s\n", g_pass, g_fail, g_fail ? "FAIL" : "ALL PASS");
	return g_fail ? 1 : 0;
}
