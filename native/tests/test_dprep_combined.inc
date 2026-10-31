// DPREP-COMBINE (round 5 D prep): targeting.patch + contact.patch + follow.patch together. Each patch was proven alone
// against round 5 B; these cases run the real Brain::Update with BOTH brain key sets on (the targeting keys of DPT() and
// the follow brain keys, exactly as the combined SuperPeasantNative.ini sets them) on the scenes where the two meet, and
// check the shipped ini block (every key once, the values of each prep, the required keys, LF, no BOM).
//   DC-1  his OWN blink (follow: OnOwnBlink keeps the target) inside the two-hidden-synth door scene (targeting DT-1):
//         0 switches while both live, both killed, no later than targeting alone (which blinks through OnTeleport).
//   DC-2  the railing-hall blink (follow DF-3) with the targeting commitment rules on: lands on the foe's side, keeps
//         the target, one blink, contact at once, no 'give up unreach'.
//   DC-3  the blink stage reached too near (follow DF-4) with the targeting rules on: push, then one path-checked blink.
//   DC-4  the 12-foe wave (targeting DT-7, the ArcJet-bar stand-in) with every brain key on: 0 switches between live
//         foes, cleared no later than round 5 B / targeting alone.
//   DC-5  a follow spot hidden behind a wall (follow DF-6) with every brain key on: navmesh return at once, no put-back.
//   DC-6  the combined ini: the 29 dprep keys at the prep values, each set once, the required keys, LF, no BOM;
//         and every brain dprep key's code default is 0 (= round 5 B).
#include <cctype>
#include <map>
#include <string>

namespace dprep_combined_tests
{
	using namespace brain_tests;
	inline bool Dbg() { return std::getenv("SP_DEBUG_DPC2") != nullptr; }  // NOLINT (desktop only)

	// the follow brain keys exactly as the combined ini sets them (the rest of follow's keys are runtime keys)
	inline void DPF(Params& p)
	{
		p.blinkKeep = 1;
		p.blinkNeedPath = 1;
		p.nearBlinkPush = 1;
		p.followBlockedNav = 1;
		p.followPutBackUnseen = 1;
	}
	enum class Keys { kB5, kT, kAll };  // round 5 B as shipped, + targeting, + targeting + follow
	// D2 (test-only hook): d2_tests re-runs these scenes with the D2 keys on top of kAll (null = as before)
	inline void (*g_extraKeys)(Params&) = nullptr;
	inline void SetKeys(Params& p, Keys k)
	{
		if (k == Keys::kB5) {
			dprep_t_tests::R5B(p);
			return;
		}
		dprep_t_tests::DPT(p);
		if (k == Keys::kAll) DPF(p);
		if (k == Keys::kAll && g_extraKeys) g_extraKeys(p);
	}

	// ---- DC-1: targeting DT-1 (two hidden synths behind a swinging door, the other one flickering through the frame),
	// with his blink going through OnOwnBlink when the follow keys are on (the runtime's brain_glue::DidBlink) ----
	inline dprep_t_tests::DoorRes DoorRun(Keys a_keys, bool a_ownBlink)
	{
		using dprep_t_tests::DoorRes;
		Sim s;
		SetKeys(s.p, a_keys);
		nav_tests::GridSpec g;
		g.W = 30;
		g.H = 30;
		g.x0 = -500;
		g.y0 = -1500;
		for (int j = 0; j < g.H; ++j)
			if (j != 14 && j != 15) g.blocked.insert({ 10, j });
		s.w.nav = nav_tests::Grid(g);
		s.w.navOn = true;
		s.w.boxes.push_back({ { 500, -1500, -10 }, { 600, -100, 400 }, 0 });
		s.w.boxes.push_back({ { 500, 100, -10 }, { 600, 1500, 400 }, 0 });
		s.w.boxes.push_back({ { 520, -100, -10 }, { 580, 100, 400 }, 0 });  // the swinging door leaf (open at 3 s)
		s.fx.indoors = s.fx.interiorCell = true;
		s.p.nav = 1;
		s.playerFeet = { -300, 0, 0 };
		s.fx.playerEye = { -300, 0, 100 };
		s.fx.playerView = { 0, -1, 0 };
		s.me.feet = { 0, 0, 0 };
		s.cands = { Foe(0x000DB75D, { 1500, 900, 0 }, Cls::kRanged), Foe(0x000DB75B, { 1500, -900, 0 }, Cls::kRanged) };
		dprep_t_tests::SightWorld sw;
		int                       frame = 0;
		std::uint32_t             curT = 0;
		sw.sight = [&frame, &curT](std::uint32_t id) {
			if (id == curT) return -1;
			const int ph = id == 0x000DB75D ? 0 : 9;
			return ((frame + ph) % 18) < 6 ? 1 : -1;
		};
		sw.w = &s.w;
		dprep_t_tests::Kills k;
		DoorRes              r;
		std::uint32_t        last = 0;
		for (frame = 0; frame < 900 && r.kills < 2; ++frame) {
			if (frame % 12 == 0) s.ClearAll();
			if (frame == static_cast<int>(3.0 / kDtB + 0.5)) s.w.boxes[2].lo.z = s.w.boxes[2].hi.z = -100000;
			// dprep_t_tests::StepW with the blink of the runtime: OnOwnBlink (follow) or OnTeleport (round 5 B)
			s.fx.now = s.now;
			s.fx.dt = kDtB;
			s.fx.meFeet = s.me.feet;
			s.fx.meChest = s.me.Chest();
			s.fx.meEye = s.me.Eye();
			s.fx.meVel = s.me.v;
			s.fx.meOnGround = s.me.feet.z < 1.0;
			s.fx.playerFeet = s.playerFeet;
			s.fx.playerChest = s.playerFeet + Vec3{ 0, 0, kChestUp };
			if (!s.fx.waiting) s.fx.centre = s.playerFeet;
			s.fx.bodyContact = s.me.contact;
			s.fx.intentSpeed = s.me.intent;
			s.fx.actualSpeed = s.me.actual;
			s.fx.blockedFor = s.me.blocked;
			s.fx.cands = std::span<const Cand>(s.cands);
			s.w.rays = 0;
			s.w.Tick();
			s.b.Update(s.fx, s.p, sw, s.out);
			if (s.out.blink && s.applyBlink) {
				s.me.feet = s.out.blinkFeet;
				s.me.v = {};
				if (a_ownBlink) s.b.OnOwnBlink(s.out.target);
				else s.b.OnTeleport();
			} else if (s.moveBody) {
				s.me.Apply(s.out, s.w, kDtB);
			}
			s.now += kDtB;
			const Out& o = s.out;
			if (o.target && last && o.target != last) {
				++r.switches;
				if (!k.Dead(last)) ++r.switchesAlive;
			}
			if (o.blink) ++r.blinks;
			if (o.target) last = o.target;
			curT = o.target;
			if (Dbg())
				std::printf("  dc1 %d %.2f tgt %08X vis %d stage %d move %s d %.0f me (%.0f,%.0f)\n", static_cast<int>(a_keys), frame * kDtB, o.target, o.visible, o.reachStage,
					MoveName(o.move), o.dist, s.me.feet.x, s.me.feet.y);
			if (k.Step(s, o, 110.0)) {
				++r.kills;
				(r.kills == 1 ? r.tKill1 : r.tKill2) = (frame + 1) * kDtB;
			}
		}
		return r;
	}

	// ---- DC-2 / DC-3: follow DF-3 / DF-4 (the railing hall), with the targeting keys on as well ----
	inline dprep_follow_tests::BlinkRes RailAll(double a_foeX, double a_meX, double a_secs, bool a_alcove, double a_blinkNear)
	{
		dprep_follow_tests::BlinkRes r;
		Sim                          s;
		SetKeys(s.p, Keys::kAll);
		dprep_follow_tests::RailScene(s, a_foeX, a_meX);
		s.applyBlink = true;
		s.ownBlink = true;
		if (a_blinkNear > 0) s.p.navBlinkNear = a_blinkNear;
		if (a_alcove) {
			s.w.boxes.push_back({ { -400, 150, -10 }, { 20, 170, 400 }, 0 });
			s.w.boxes.push_back({ { -400, -170, -10 }, { 20, -150, 400 }, 0 });
			s.w.boxes.push_back({ { -170, -170, -10 }, { -150, 170, 400 }, 0 });
		}
		const int n = static_cast<int>(a_secs * 60.0);
		for (int i = 0; i < n; ++i) {
			if (i % 30 == 0 && s.Find(501)) SetHit(*s.Find(501), s.now);
			if (i % 15 == 0) s.ClearAll();
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.blink) {
				++r.blinks;
				if (r.tBlink < 0) {
					r.tBlink = t;
					r.landX = o.blinkFeet.x;
				}
			}
			if (o.giveUp == GiveUp::kUnreach && o.giveUpId == 501) {
				++r.unreachAll;
				if (r.tBlink > 0) ++r.unreachAfter;
			}
			const Cand&  c = *s.Find(501);
			const double dh = Length(Flat(c.chest - s.me.Chest()));
			if (r.tContactAll < 0 && dh <= 80.0) r.tContactAll = t;
			if (r.tBlink > 0) {
				if (o.target != 501 && t - r.tBlink < 1.5) ++r.dropFrames;
				if (r.tContact < 0 && dh <= 80.0) r.tContact = t - r.tBlink;
			}
		}
		r.keeps = static_cast<int>(s.b.GetStats().blinkKeeps);
		r.rejects = s.b.GetStats().blinkPathRejects;
		r.nearPushes = s.b.GetStats().nearBlinkPushes;
		return r;
	}

	// ---- DC-4: targeting DT-7 (12 foes walking at the player from 300-1500 u), the given key set ----
	inline std::pair<double, int> WaveRun(Keys a_keys, bool a_indoors)
	{
		Sim s;
		SetKeys(s.p, a_keys);
		s.fx.indoors = s.fx.interiorCell = a_indoors;
		s.playerFeet = { 0, -150, 0 };
		s.me.feet = { 0, 0, 0 };
		std::mt19937                           rng(7);
		std::uniform_real_distribution<double> ang(0, 2 * 3.14159265), rad(300, 1500);
		for (int i = 0; i < 12; ++i) {
			const double a = ang(rng), r = rad(rng);
			s.cands.push_back(Engaged(0x00F9C000 + static_cast<std::uint32_t>(i), { r * std::cos(a), r * std::sin(a), 0 }, i % 3 == 0 ? Cls::kRanged : Cls::kRegular));
		}
		dprep_t_tests::Kills k;
		std::uint32_t        last = 0;
		int                  swAlive = 0;
		double               tAll = -1;
		for (int i = 0; i < 1200 && tAll < 0; ++i) {
			if (i % 12 == 0) s.ClearAll();
			for (Cand& c : s.cands)
				if (!c.killPending) {
					const Vec3 d = FlatDir(s.playerFeet - c.feet);
					if (Length(Flat(s.playerFeet - c.feet)) > 120) MoveFoe(c, c.feet + d * (150.0 * kDtB));
					c.vel = d * 150.0;
				}
			const Out& o = s.Step();
			if (o.target && last && o.target != last && !k.Dead(last)) ++swAlive;
			if (o.target) last = o.target;
			k.Step(s, o, 100.0);
			if (k.at.size() == 12) tAll = (i + 1) * kDtB;
		}
		return { tAll, swAlive };
	}

	// ---- DC-5: follow DF-6 (follow spot 230 u behind a wall, a doorway), every brain key on ----
	inline dprep_follow_tests::FolRes FollowWallAll()
	{
		dprep_follow_tests::FolRes r;
		Sim                        s;
		SetKeys(s.p, Keys::kAll);
		dprep_follow_tests::WallScene(s);
		for (int i = 0; i < 480 && r.tBack < 0; ++i) {
			const bool following = (i == 0 || s.out.move == Move::kFollow) && s.me.feet.x > 150;
			s.moveBody = !following;
			s.fx.followBlocked = following && !s.w.Clear(s.me.Chest(), s.fx.followSpot + Vec3{ 0, 0, 80 });
			if (following) {
				s.me.contact = false;
				s.me.intent = 0;
				s.me.actual = 0;
			}
			const auto&  o = s.Step();
			const double t = (i + 1) * kDtB;
			if (o.putBack) ++r.putBacks;
			if (o.move == Move::kReturn && r.tReturn < 0) r.tReturn = t;
			if (r.tReturn > 0 && s.me.feet.x < 100 && Length(Flat(s.me.feet - s.fx.followSpot)) <= 200.0) r.tBack = t;
		}
		return r;
	}

	// ---- DC-6: the combined ini (text level: sections, duplicates, LF, BOM; values through sp::cfg::LoadIni) ----
	struct IniKey
	{
		const char* key;
		double      value;
	};
	inline void IniBlock()
	{
		std::string path = __FILE__;
		path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
		std::string text;
		if (FILE* f = nullptr; fopen_s(&f, path.c_str(), "rb") == 0 && f) {
			char   buf[4096];
			size_t n = 0;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
			std::fclose(f);
		}
		CHECK(!text.empty(), "DC-6 the shipped ini is readable (%s)", path.c_str());
		CHECK(text.size() < 3 || !(static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF),
			"DC-6 the shipped ini has no UTF-8 BOM");
		CHECK(text.find('\r') == std::string::npos, "DC-6 the shipped ini is LF only");
		// active "key = value" lines per section (comments and blank lines skipped; keys lower-cased like sp::cfg)
		std::map<std::string, std::map<std::string, std::pair<int, std::string>>> sec;  // section -> key -> (count, value)
		{
			std::string cur;
			size_t      pos = 0;
			while (pos < text.size()) {
				size_t e = text.find('\n', pos);
				if (e == std::string::npos) e = text.size();
				std::string l = text.substr(pos, e - pos);
				pos = e + 1;
				const size_t sc = l.find(';');
				if (sc != std::string::npos) l = l.substr(0, sc);
				auto trim = [](std::string v) {
					const size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t");
					return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
				};
				l = trim(l);
				if (l.empty()) continue;
				if (l.front() == '[' && l.back() == ']') {
					cur = l.substr(1, l.size() - 2);
					continue;
				}
				const size_t eq = l.find('=');
				if (eq == std::string::npos) continue;
				std::string kk = trim(l.substr(0, eq));
				for (char& ch : kk) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				auto& slot = sec[cur][kk];
				++slot.first;
				slot.second = trim(l.substr(eq + 1));
			}
		}
		auto one = [&sec](const char* s, const char* k, const char* v) {
			std::string kk = k;
			for (char& ch : kk) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			const auto si = sec.find(s);
			if (si == sec.end()) return false;
			const auto ki = si->second.find(kk);
			return ki != si->second.end() && ki->second.first == 1 && ki->second.second == v;
		};
		CHECK(one("Tabs", "fo4.forceConvention", "1") && one("Tabs", "brain.range", "1") && one("Tabs", "brain.requireCombat", "0") && one("Brain", "Native", "1"),
			"DC-6 required keys: [Tabs] fo4.forceConvention 1, brain.range 1, brain.requireCombat 0; [Brain] Native 1");
		int dup = 0;
		for (const auto& [sname, keys] : sec)
			for (const auto& [kname, cv] : keys)
				if (cv.first != 1) {
					++dup;
					std::printf("info: DC-6 [%s] %s is set %d times\n", sname.c_str(), kname.c_str(), cv.first);
				}
		CHECK(dup == 0, "DC-6 no key is set twice in the shipped ini (%d)", dup);
		// the three prep blocks, at the values each prep shipped (targeting / contact / follow NOTES.md)
		static constexpr IniKey kKeys[] = {
			// targeting
			{ "brain.switchHold", 0.5 }, { "brain.curOccFree", 1 }, { "brain.occFlickerKeep", 0.5 }, { "brain.predClosing", 1 }, { "brain.predHiddenZero", 1 },
			{ "brain.attackHoldR", 750 }, { "brain.leashPick", 1 }, { "brain.fpHiddenDrop", 0.5 }, { "brain.hostileFlipRecheck", 1 }, { "brain.hostileRecheckNoCombat", 0.25 },
			// contact
			{ "punch.collateral", 1 }, { "punch.shape", 1 }, { "punch.contactFistR", 35 }, { "punch.torsoBone", 1 }, { "punch.vic.lie", 1 }, { "punch.contactSeenWithin", 0.5 },
			{ "punch.contactFistRay", 1 }, { "punch.around", 1 },
			// follow
			{ "body.tabsNearIndoorR", 1500 }, { "body.tabsNearFarR", 1500 }, { "runtime.followSpotFromSim", 1 }, { "runtime.followHeldReanchor", 0.3 }, { "brain.followBlockedNav", 1 },
			{ "brain.followPutBackUnseen", 1 }, { "runtime.teleportClean", 1 }, { "brain.blinkKeep", 1 }, { "brain.blinkNeedPath", 1 }, { "brain.nearBlinkPush", 1 },
			{ "runtime.followOffHold", 1.5 },
		};
		static std::string line;
		const bool         ok = sp::cfg::LoadIni(path, [](const std::string& a) { line = a; });
		CHECK(ok && line.find(" 0 ignored") != std::string::npos, "DC-6 the combined ini parses with 0 ignored keys: %s", line.c_str());
		int bad = 0;
		for (const auto& kv : kKeys) {
			const double v = sp::cfg::Tunable(kv.key, -12345.0);
			if (!(std::fabs(v - kv.value) < 1e-9)) {
				++bad;
				std::printf("info: DC-6 %s = %g (expected %g)\n", kv.key, v, kv.value);
			}
		}
		CHECK(bad == 0, "DC-6 the 29 dprep keys are on at the prep values (%d wrong)", bad);
		// every brain-side dprep key defaults to 0 in code (round 5 B); the runtime keys' Tunable defaults are 0 in runtime.cpp
		const Params P0{};
		CHECK(P0.switchHold == 0 && P0.curOccFree == 0 && P0.occFlickerKeep == 0 && P0.predClosing == 0 && P0.predHiddenZero == 0 && P0.attackHoldR == 0 && P0.leashPick == 0 &&
		          P0.fpHiddenDrop == 0 && P0.blinkKeep == 0 && P0.blinkNeedPath == 0 && P0.nearBlinkPush == 0 && P0.followBlockedNav == 0 && P0.followPutBackUnseen == 0,
			"DC-6 code defaults: every brain dprep key 0 (= round 5 B)");
	}

	inline void Run()
	{
		// ---- DC-1 ----
		{
			const auto b = DoorRun(Keys::kB5, false), t = DoorRun(Keys::kT, false), a = DoorRun(Keys::kAll, true);
			std::printf("info: DC-1 two hidden synths behind a door: round 5 B %d alive switches, kills %d at %.2f / %.2f s, blinks %d | targeting %d, %d at %.2f / %.2f s, blinks %d | "
			            "targeting + follow (own blink kept) %d, %d at %.2f / %.2f s, blinks %d\n",
				b.switchesAlive, b.kills, b.tKill1, b.tKill2, b.blinks, t.switchesAlive, t.kills, t.tKill1, t.tKill2, t.blinks, a.switchesAlive, a.kills, a.tKill1, a.tKill2, a.blinks);
			CHECK(a.switchesAlive == 0 && a.kills == 2, "DC-1 all keys: 0 switches while both live (%d), both killed (%d)", a.switchesAlive, a.kills);
			CHECK(t.kills < 2 || a.tKill2 <= t.tKill2 + 0.3, "DC-1 all keys: no later than targeting alone (%.2f vs %.2f s)", a.tKill2, t.tKill2);
			CHECK(b.kills < 2 || a.tKill2 <= b.tKill2, "DC-1 all keys: no later than round 5 B (%.2f vs %.2f s)", a.tKill2, b.tKill2);
		}
		// ---- DC-2 ----
		{
			const auto r = RailAll(130.0, -700.0, 15.0, false, 0.0);
			std::printf("info: DC-2 railing hall (all brain keys): blink %.2f s to x %.0f, contact %.2f s after, dropped %d frames, unreach %d, blinks %d, keeps %d\n", r.tBlink, r.landX,
				r.tContact, r.dropFrames, r.unreachAll, r.blinks, r.keeps);
			CHECK(r.tBlink > 0 && r.keeps >= 1 && r.landX > 40.0, "DC-2 all keys: one kept blink to the foe's side (%.2f s, keeps %d, x %.0f)", r.tBlink, r.keeps, r.landX);
			CHECK(r.dropFrames == 0 && r.unreachAll == 0 && r.blinks == 1, "DC-2 all keys: target never dropped (%d), no unreach (%d), one blink (%d)", r.dropFrames, r.unreachAll, r.blinks);
			CHECK(r.tContact > 0 && r.tContact <= 0.6, "DC-2 all keys: contact at once after the landing (%.2f s)", r.tContact);
		}
		// ---- DC-3 ----
		{
			const auto r = RailAll(110.0, -5.0, 10.0, true, 400.0);
			std::printf("info: DC-3 too near to blink (all brain keys): unreach %d, near-push frames %llu, blinks %d (x %.0f), contact %.2f s\n", r.unreachAll,
				static_cast<unsigned long long>(r.nearPushes), r.blinks, r.landX, r.tContactAll);
			CHECK(r.unreachAll == 0 && r.nearPushes > 0, "DC-3 all keys: no give-up (%d), pushes first (%llu)", r.unreachAll, static_cast<unsigned long long>(r.nearPushes));
			CHECK(r.blinks == 1 && r.landX > 40.0 && r.tContactAll > 0 && r.tContactAll <= 8.0, "DC-3 all keys: one blink to the foe's side (%d, x %.0f), contact %.2f s", r.blinks, r.landX,
				r.tContactAll);
		}
		// ---- DC-4 ----
		for (const bool indoors : { false, true }) {
			const auto b = WaveRun(Keys::kB5, indoors), t = WaveRun(Keys::kT, indoors), a = WaveRun(Keys::kAll, indoors);
			std::printf("info: DC-4 12-foe wave %s: round 5 B %.2f s (%d live switches) | targeting %.2f s (%d) | all brain keys %.2f s (%d)\n", indoors ? "indoors" : "outdoors", b.first,
				b.second, t.first, t.second, a.first, a.second);
			CHECK(a.first > 0 && a.second == 0, "DC-4 %s all keys: cleared (%.2f s), 0 switches between live foes (%d)", indoors ? "indoors" : "outdoors", a.first, a.second);
			CHECK(b.first < 0 || a.first <= b.first + 1e-9, "DC-4 %s all keys: no slower than round 5 B (%.2f vs %.2f s)", indoors ? "indoors" : "outdoors", a.first, b.first);
			CHECK(t.first < 0 || a.first <= t.first + 0.1, "DC-4 %s all keys: no slower than targeting alone (%.2f vs %.2f s)", indoors ? "indoors" : "outdoors", a.first, t.first);
		}
		// ---- DC-5 ----
		{
			const auto r = FollowWallAll();
			std::printf("info: DC-5 follow spot behind a wall (all brain keys): return %.3f s, back %.2f s, put-backs %d\n", r.tReturn, r.tBack, r.putBacks);
			CHECK(r.tReturn > 0 && r.tReturn <= 2 * kDtB, "DC-5 all keys: the navmesh return at once (%.3f s)", r.tReturn);
			CHECK(r.tBack > 0 && r.tBack <= 3.0 && r.putBacks == 0, "DC-5 all keys: back beside the spot in %.2f s, no put-back (%d)", r.tBack, r.putBacks);
		}
		// ---- DC-6 ----
		IniBlock();
	}
}
