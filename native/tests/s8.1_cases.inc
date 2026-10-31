// OWNER: TESTS lane, round 6 step 8.1 ("Fix the protect regression since step 7.5: the player drops low in long fights").
// The step-8.1 desktop acceptance on the protect closed loop (tests/s8.1_sim.h: the REAL old brain picks AND moves him, the
// shipped `[R6] Warden = 0` path; ground-truth meters). One scene per user failure of the s7.5c / s7.7c / s8c gates (their
// GATE.md root causes) and per user rule the fix could break; the in-game marks they stand for are in TESTPLAN.md.
//   PR-01 DT3     the downtown 10-raider burst (s7.5c / s7.7c / s8c soak deaths)
//   PR-02 DT1     a melee raider beats the player while he is 800 u away and ranged foes 800-1700 u wait (s8c dt1 god-backs)
//   PR-03 ALIAS   a hostile world-alias giant (Swan, 00056B7B) he must not hover over while raiders shoot the player
//   PR-04 NEXT    the s8c soak-1 death: the second hitter waits while he works on a non-hitter; the next pick after a kill
//   PR-05 CZ      Combat Zone: the explosive / heavy hitter, 2 protected raiders, ping-pong between foes that do not hit
//   PR-06 FAR     far world attackers after a fight: never idle > 1 s (LEAD 10-16 hard rule; s8c dw=ignored 3 s)
//   PR-07 P03     the raider the player provokes is his target within 0.3 s; the other neutral never
//   PR-08 DOOR    Combat Zone cz2: a melee raider hits the player behind the arena wall while he is outside (blinkWait 2.8 s)
//   PR-09 RULES   user rules (a) a settler that turns on the player is engaged, (d) a settler's stray shot is never a trigger
//   PR-10 SOAK    a desktop soak10 (dt1, dt3, cz1, cz2, settlement raid per cycle): K07 god-backs, deaths, P02 sections
//   PR-11 ROOM    the death room (P17): god mode off, 8 raiders around the player in a small room
// Each case prints `S81CASE|<id>|PASS|FAIL|<numbers>`; every run's meters go to the --json file (the A/B compare of
// testsuite\scenarios\s8.1\s8.1_compare.py pools them per arm: candidate vs the installed source + ini, and the bisect arms).
// Needs CHECK / g_fail / g_quick from the host (tests/s8.1_tests.cpp).
#include <cstdarg>
#include <fstream>
#include <numeric>

// a B case (the crowd-blind eyes of the s6f / s8c builds forced on): its absolute marks are REPORTED (S81INFO|mark|...; the
// installed build fails them too in that condition); the gate of a B case is the A/B of s8.1_compare.py: no blocker meter
// worse than the installed build (the guard must never make blind eyes worse)
#define S81_MARK(a_info, cond, ...)           \
	do {                                      \
		if (a_info) {                         \
			if (!(cond)) {                    \
				std::printf("S81INFO|mark|"); \
				std::printf(__VA_ARGS__);     \
				std::printf("\n");            \
			}                                 \
		} else {                              \
			CHECK(cond, __VA_ARGS__);         \
		}                                     \
	} while (0)

namespace s81_tests
{
	using namespace s81;

	struct CaseRec
	{
		std::string id, detail;
		bool        pass{ true };
	};
	inline std::vector<CaseRec>& Cases()
	{
		static std::vector<CaseRec> v;
		return v;
	}
	inline std::vector<std::pair<std::string, Meters>>& Runs()  // (case id, meters) of every run, for --json
	{
		static std::vector<std::pair<std::string, Meters>> v;
		return v;
	}
	inline std::string& OnlyCase()  // --case <id> (prefix)
	{
		static std::string s;
		return s;
	}
	inline int& SeedsFull()  // --seeds n
	{
		static int n = 0;
		return n;
	}
	struct CaseScope
	{
		std::string id;
		int         failAt;
		std::string detail;
		explicit CaseScope(const char* a_id) : id(a_id), failAt(g_fail) {}
		~CaseScope()
		{
			const bool ok = g_fail == failAt;
			std::printf("S81CASE|%s|%s|%s\n", id.c_str(), ok ? "PASS" : "FAIL", detail.c_str());
			std::fflush(stdout);
			Cases().push_back({ id, detail, ok });
		}
		void Note(const char* fmt, ...)
		{
			char    b[1600];
			va_list a;
			va_start(a, fmt);
			std::vsnprintf(b, sizeof b, fmt, a);
			va_end(a);
			if (!detail.empty()) detail += "; ";
			detail += b;
		}
	};
	inline int Seeds(int full)
	{
		if (SeedsFull() > 0) return SeedsFull();
		return g_quick ? std::max(3, full / 3) : full;
	}
	inline bool Want(const char* id) { return OnlyCase().empty() || std::string(id).rfind(OnlyCase(), 0) == 0; }
	inline Meters Keep(const char* a_case, Meters m)
	{
		Runs().push_back({ a_case, m });
		return m;
	}

	// pooled numbers over the runs of one case
	struct Pool
	{
		std::vector<Meters> v;
		void   Add(const Meters& m) { v.push_back(m); }
		template <class F>
		double Max(F f) const
		{
			double r = 0;
			for (const auto& m : v) r = std::max(r, static_cast<double>(f(m)));
			return r;
		}
		template <class F>
		double Min(F f) const
		{
			double r = 1e18;
			for (const auto& m : v) r = std::min(r, static_cast<double>(f(m)));
			return v.empty() ? 0 : r;
		}
		template <class F>
		double Sum(F f) const
		{
			double r = 0;
			for (const auto& m : v) r += static_cast<double>(f(m));
			return r;
		}
		template <class F>
		double Med(F f) const
		{
			std::vector<double> x;
			for (const auto& m : v) x.push_back(static_cast<double>(f(m)));
			if (x.empty()) return 0;
			std::sort(x.begin(), x.end());
			return x[x.size() / 2];
		}
		[[nodiscard]] std::string Common() const
		{
			char b[900];
			std::snprintf(b, sizeof b,
				"runs %zu: minHp min %.0f med %.0f; god-backs %.0f, deaths %.0f; no kill while hurt max %.2f s; hitter wait max %.2f s; non-hitter while hurt med %.2f s; "
				"hitter drops %.0f; wrong next %.0f; ping-pong %.0f, bursts %.0f; stall max %.2f s; hold far max %.2f s; idle max %.2f s; far from player max %.2f s; relocs %.0f (seen %.0f, "
				"put-backs %.0f); friendly %.0f; sight exhausted med %.0f %%; slides med %.0f",
				v.size(), Min([](const Meters& m) { return m.minHp; }), Med([](const Meters& m) { return m.minHp; }), Sum([](const Meters& m) { return m.godBacks; }),
				Sum([](const Meters& m) { return m.deaths; }), Max([](const Meters& m) { return m.noKillHitMax; }), Max([](const Meters& m) { return m.hitterWaitMax; }),
				Med([](const Meters& m) { return m.nonHitterWhileHit; }), Sum([](const Meters& m) { return m.hitterDrops; }), Sum([](const Meters& m) { return m.wrongNextAfterKill; }),
				Sum([](const Meters& m) { return m.pingPong; }), Sum([](const Meters& m) { return m.pingBursts; }), Max([](const Meters& m) { return m.stallMax; }),
				Max([](const Meters& m) { return m.holdFarMax; }),
				Max([](const Meters& m) { return m.idleMax; }), Max([](const Meters& m) { return m.farFromPlayerMax; }), Sum([](const Meters& m) { return m.relocs; }),
				Sum([](const Meters& m) { return m.relocsSeen; }), Sum([](const Meters& m) { return m.putBacks; }),
				Sum([](const Meters& m) { return m.friendlyHits + m.friendlyFrames; }), Med([](const Meters& m) { return m.exhaustedPct; }),
				Med([](const Meters& m) { return m.slideFrames; }));
			return b;
		}
	};

	// ================================================================================================ the scenes
	// PR-01 DT3: 10 raiders released at once around the player (SOAK.dt3: 3 rushers, 6 gunners behind cars, one heavy /
	// explosive hitter); he idles next to the player at the release
	inline s3sim::World Dt3(unsigned seed, double a_dmgScale = 1.0)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 140, -90, 0 };
		const double base = U(w, 0, 6.283);
		for (int k = 0; k < 6; ++k) {
			const Vec3 c = Polar(U(w, 400, 1500), base + k * 1.05 + U(w, -0.3, 0.3));
			Car(w, c);
		}
		for (int k = 0; k < 10; ++k) {
			const double ang = base + k * 0.628 + U(w, -0.2, 0.2);
			const std::uint32_t id = 0xD300u + static_cast<std::uint32_t>(k);
			if (k < 3) w.actors.push_back(Rusher(w, id, Polar(U(w, 600, 900), ang), U(w, 330, 420), 4.0 * a_dmgScale, 1.0));
			else if (k < 9) w.actors.push_back(Gunner(w, id, Polar(U(w, 700, 2000), ang), 2.5 * a_dmgScale, U(w, 1.1, 1.7), U(w, 0.4, 2.5)));
			else w.actors.push_back(Gunner(w, id, Polar(U(w, 1300, 1800), ang), 12.0 * a_dmgScale, 3.5, 2.2, false));  // the heavy / explosive one
		}
		return w;
	}
	// PR-02 DT1: he is 800 u from the player (busy elsewhere) when a melee raider rushes the player; 4 gunners 800-1700 u from
	// him aim at the player (combat target the player = engaged) from behind walls (their shots need a line: rarely)
	inline s3sim::World Dt1(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 820 + U(w, -60, 60), 120 + U(w, -60, 60), 0 };
		w.actors.push_back(Rusher(w, 0xD100, { -330 + U(w, -40, 40), 60 + U(w, -40, 40), 0 }, 360, 4.0, 1.0));
		for (int k = 0; k < 4; ++k) {
			const double ang = -0.9 + k * 0.6 + U(w, -0.15, 0.15);
			const Vec3   g = w.him.feet + Polar(U(w, 800, 1700), ang);
			// a wall between the gunner and the player (2/3 of the way from the player)
			const Vec3 dir = g * (1.0 / std::max(1.0, Length(g)));
			const Vec3 c = g * 0.66;
			const Vec3 side{ -dir.y, dir.x, 0 };
			Wall(w, Vec3{ std::min(c.x - side.x * 160, c.x + side.x * 160) - 12, std::min(c.y - side.y * 160, c.y + side.y * 160) - 12, 0 },
			     Vec3{ std::max(c.x - side.x * 160, c.x + side.x * 160) + 12, std::max(c.y - side.y * 160, c.y + side.y * 160) + 12, 260 });
			w.actors.push_back(Gunner(w, 0xD110u + static_cast<std::uint32_t>(k), g, 2.5, 1.4, U(w, 0.5, 1.5), true));
		}
		return w;
	}
	// PR-03 ALIAS: a hostile quest-alias giant (Swan: race size 3, 420 u tall, underwater in the pond 1480 u away, in combat
	// with HIM, never hits anyone) + 2 gunners 700-900 u from the player that shoot the player
	inline s3sim::World Alias(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 100, -60, 0 };
		const double ang = U(w, 0, 6.283);
		auto s = s3sim::Idle(0x56B7B, Polar(1480 + U(w, -60, 60), ang, -260));
		s.name = "swan";
		s.alias = true;
		s.unique = true;
		s.raceSize = 3;
		s.height = 420;
		s.boundR = 160;
		s.immobile = true;
		s.hostile = true;
		s.hostileKind = true;
		s.inCombat = true;
		s.ct = s3sim::kCtHim;
		s.attacking = false;
		s.dmg = 0;
		s.shootR = 1;
		s.script = [](s3sim::Actor& a, s3sim::World& ww) {  // the pond: the scan sees it underwater (facts persist across ScanTick)
			for (size_t i = 0; i < ww.actors.size() && i < ww.facts.size(); ++i)
				if (ww.actors[i].id == a.id) ww.facts[i].underwater = true;
		};
		w.actors.push_back(s);
		for (int k = 0; k < 2; ++k)
			w.actors.push_back(Gunner(w, 0xD3A0u + static_cast<std::uint32_t>(k), Polar(U(w, 700, 900), ang + 2.2 + 1.6 * k), 2.5, 1.3, 0.5 + 0.4 * k, false));
		return w;
	}
	// PR-04 NEXT: the s8c soak-1 death window. G1 aims at the player (engaged, not hitting until 3.5 s) 950 u away, nearest to
	// him; G2 / G3 hit the player hard from 1.0 / 1.4 s; G4 aims from 1400 u (first shot 4.5 s)
	inline s3sim::World Next(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { -420, 320, 0 };
		const double ang = U(w, -0.3, 0.3);
		w.actors.push_back(Aimer(w, 0xD401, Polar(950, 2.3 + ang), 3.0, 1.5, 3.5));
		w.actors.push_back(Gunner(w, 0xD402, Polar(870, -0.4 + ang), 7.0, 1.1, 1.0, false));
		w.actors.push_back(Gunner(w, 0xD403, Polar(1100, 0.6 + ang), 7.0, 1.3, 1.4, false));
		w.actors.push_back(Aimer(w, 0xD404, Polar(1400, 3.6 + ang), 3.0, 1.5, 4.5));
		return w;
	}
	// PR-05 CZ: the Combat Zone arena (interior, walls + ceiling, 4 pillars): 2 protected raiders and a gunner shoot with a line
	// rule, the heavy / explosive hitter (25 a throw, every 4 s, first at 3 s) needs no line
	inline s3sim::World Cz(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 150, 0, 0 };
		Wall(w, { -1320, -1320, 0 }, { -1300, 1320, 640 });
		Wall(w, { 1300, -1320, 0 }, { 1320, 1320, 640 });
		Wall(w, { -1320, -1320, 0 }, { 1320, -1300, 640 });
		Wall(w, { -1320, 1300, 0 }, { 1320, 1320, 640 });
		Wall(w, { -1320, -1320, 640 }, { 1320, 1320, 660 });  // the ceiling
		for (int k = 0; k < 4; ++k) {
			const Vec3 c = Polar(480, 0.785 + 1.571 * k);
			Wall(w, { c.x - 40, c.y - 40, 0 }, { c.x + 40, c.y + 40, 640 });
		}
		const double ang = U(w, 0, 6.283);
		for (int k = 0; k < 3; ++k) {
			auto g = Gunner(w, 0xD500u + static_cast<std::uint32_t>(k), Polar(U(w, 600, 950), ang + 1.3 * k), 2.5, U(w, 1.2, 1.8), U(w, 0.5, 2.0), true);
			g.protectedOnly = k < 2;
			w.actors.push_back(g);
		}
		w.actors.push_back(Gunner(w, 0xD5E0, Polar(U(w, 800, 1000), ang + 3.9), 25.0, 4.0, 3.0, false));
		return w;
	}
	// PR-06 FAR: after a fight 4 world raiders 1500-6000 u away keep shooting at the player (combat target the player, weak hits
	// every 2 s), two of them on roofs he cannot get onto (a bar cage round each: sight passes, his body does not)
	inline s3sim::World Far(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 40, 0 };
		const double ang = U(w, 0, 6.283);
		for (int k = 0; k < 4; ++k) {
			const double r = k < 2 ? U(w, 1500, 2500) : U(w, 3500, 6000);
			const Vec3   f = Polar(r, ang + 0.9 * k, k % 2 ? 700.0 : 0.0);
			auto         g = Gunner(w, 0xD600u + static_cast<std::uint32_t>(k), f, 0.8, 2.0, 0.3 + 0.5 * k, false);
			g.shootR = 12000;
			if (k % 2) {  // the roof cage
				Wall(w, { f.x - 260, f.y - 260, f.z - 10 }, { f.x + 260, f.y + 260, f.z }, 0);
				Wall(w, { f.x - 260, f.y - 260, f.z }, { f.x - 250, f.y + 260, f.z + 300 }, 1);
				Wall(w, { f.x + 250, f.y - 260, f.z }, { f.x + 260, f.y + 260, f.z + 300 }, 1);
				Wall(w, { f.x - 260, f.y - 260, f.z }, { f.x + 260, f.y - 250, f.z + 300 }, 1);
				Wall(w, { f.x - 260, f.y + 250, f.z }, { f.x + 260, f.y + 260, f.z + 300 }, 1);
				Wall(w, { f.x - 260, f.y - 260, f.z + 300 }, { f.x + 260, f.y + 260, f.z + 310 }, 1);
			}
			w.actors.push_back(g);
		}
		return w;
	}
	// PR-07 P03: two neutral raiders (hostile kind, not hostile to the player yet) 500-800 u away; the player shoots R at 1.0 s
	// (it turns on the player, first shot at 1.8 s); the other one never
	inline s3sim::World Provoke(unsigned seed, double* a_shotAt)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 60, 0 };
		const double ang = U(w, 0, 6.283), shotAt = 1.0 + U(w, 0, 0.3);
		if (a_shotAt) *a_shotAt = shotAt;
		for (int k = 0; k < 2; ++k) {
			auto n = s3sim::Shooter(0xD700u + static_cast<std::uint32_t>(k), Polar(U(w, 500, 800), ang + 1.6 * k), 3.0, 0.8);
			n.hostile = false;
			n.ct = s3sim::kCtNone;
			n.inCombat = false;
			n.attacking = false;
			if (k == 0)
				n.script = [shotAt](s3sim::Actor& a, s3sim::World& ww) {
					if (ww.Rel() >= shotAt && !a.attacking && a.alive && a.ct == s3sim::kCtNone) {
						ww.PlayerHits(a.id);
						a.hostile = true;
						a.ct = s3sim::kCtPlayer;
						a.inCombat = true;
						a.attacking = true;
						a.nextShot = ww.t0 + shotAt + 0.8;
					}
				};
			w.actors.push_back(n);
		}
		return w;
	}
	// PR-08 DOOR: the Combat Zone cz2 god-back. The player stands inside the arena (walls round 1200 x 1200, a door gap on the
	// far side, ceiling); he is OUTSIDE behind the near wall 570-740 u from the player; a melee raider walks in through the
	// door and beats the player
	inline s3sim::World Door(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 0, -650 + U(w, -80, 80), 0 };
		Wall(w, { -620, -420, 0 }, { 620, -400, 600 });          // the near wall (between him and the player)
		Wall(w, { -620, -420, 0 }, { -600, 820, 600 });
		Wall(w, { 600, -420, 0 }, { 620, 820, 600 });
		Wall(w, { -620, 800, 0 }, { -110, 820, 600 });           // the far wall with the door gap -110 .. 110
		Wall(w, { 110, 800, 0 }, { 620, 820, 600 });
		Wall(w, { -2000, -2000, 600 }, { 2000, 2000, 620 });     // one ceiling over everything (interior)
		Wall(w, { -2000, -2000, 0 }, { -1980, 2000, 600 });      // the outer room
		Wall(w, { 1980, -2000, 0 }, { 2000, 2000, 600 });
		Wall(w, { -2000, -2000, 0 }, { 2000, -1980, 600 });
		Wall(w, { -2000, 1980, 0 }, { 2000, 2000, 600 });
		auto r = Rusher(w, 0xD800, { U(w, -60, 60), 1150, 0 }, 300, 4.0, 1.0);
		w.actors.push_back(r);
		return w;
	}
	// PR-09 RULES: a raid on a settlement: 3 raiders shoot the player, 2 settlers shoot the raiders; at 2.0 s one settler's
	// stray shot hits the player (user rule d: never a trigger); at 4.0 s the OTHER settler turns on the player (user rule a)
	inline s3sim::World Rules(unsigned seed, double* a_turnAt)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 120, 40, 0 };
		const double ang = U(w, 0, 6.283), turnAt = 4.0 + U(w, 0, 0.4);
		if (a_turnAt) *a_turnAt = turnAt;
		for (int k = 0; k < 3; ++k)
			w.actors.push_back(Gunner(w, 0xD900u + static_cast<std::uint32_t>(k), Polar(U(w, 900, 1500), ang + 0.5 * k), 1.5, 1.6, U(w, 0.6, 1.4), false));
		for (int k = 0; k < 2; ++k) {
			auto s = s3sim::Friendly(0xD9A0u + static_cast<std::uint32_t>(k), Polar(220, ang + 3.1 + 0.6 * k));
			s.name = "settler";
			s.settlerNpc = true;
			s.inCombat = true;
			s.ct = s3sim::kCtOther;  // fighting the raiders
			if (k == 0)
				s.script = [stray = false](s3sim::Actor& a, s3sim::World& ww) mutable {
					if (!stray && ww.Rel() >= 2.0) {  // the stray shot: cause = the settler, its combat target NOT the player
						stray = true;
						ww.PushHit(a.id, O::kPlayerId, sp::fo4::scan::HitKind::kPlayer, Length(a.feet - ww.player.feet));
						if (!ww.player.god) ww.player.hp -= 2.0;
					}
				};
			else
				s.script = [turnAt](s3sim::Actor& a, s3sim::World& ww) {
					if (ww.Rel() >= turnAt && a.friendly) {  // it turns on the player: a real attacker from now on
						a.friendly = false;
						a.hostile = true;
						a.ct = s3sim::kCtPlayer;
						a.attacking = true;
						a.dmg = 2.0;
						a.shotEvery = 1.0;
						a.nextShot = ww.t;
					}
				};
			w.actors.push_back(s);
		}
		return w;
	}
	// PR-11 ROOM: the death room (P17, god mode off): 8 raiders round the player in a 900 x 900 room, 4 rushers + 4 gunners
	inline s3sim::World Room(unsigned seed)
	{
		s3sim::World w;
		w.rng.seed(seed);
		w.interior = true;
		w.player.feet = { 0, 0, 0 };
		w.him.feet = { 110, 0, 0 };
		Wall(w, { -470, -470, 0 }, { -450, 470, 400 });
		Wall(w, { 450, -470, 0 }, { 470, 470, 400 });
		Wall(w, { -470, -470, 0 }, { 470, -450, 400 });
		Wall(w, { -470, 450, 0 }, { 470, 470, 400 });
		Wall(w, { -470, -470, 400 }, { 470, 470, 420 });
		const double ang = U(w, 0, 6.283);
		for (int k = 0; k < 8; ++k) {
			const Vec3 p = Polar(U(w, 300, 420), ang + k * 0.785);
			if (k % 2) w.actors.push_back(Rusher(w, 0xDB00u + static_cast<std::uint32_t>(k), p, 300, 4.0, 1.0));
			else w.actors.push_back(Gunner(w, 0xDB00u + static_cast<std::uint32_t>(k), p, 3.0, 1.0, U(w, 0.3, 1.0), false));
		}
		return w;
	}

	// ================================================================================================ the cases
	// a_crowd: -1 = the eyes the ini gives (look.sightMask), 1 = the crowd-blind eyes of the s6f / s8c builds forced on (the
	// "B" cases: the protect guard alone must hold the player when the engine's sight ray is exhausted in a crowd)
	inline RunCfg Rc(int a_crowd)
	{
		RunCfg rc;
		rc.crowd = a_crowd;
		return rc;
	}
	inline void PR01(const char* id = "PR-01", int crowd = -1)
	{
		if (!Want(id)) return;
		CaseScope cs(id);
		Pool p;
		for (int s = 1; s <= Seeds(12); ++s) p.Add(Keep(id, Run("DT3", static_cast<unsigned>(s), Dt3(static_cast<unsigned>(s)), 30.0, Rc(crowd))));
		const double god = p.Sum([](const Meters& m) { return m.godBacks; }), nk = p.Max([](const Meters& m) { return m.noKillHitMax; });
		const double wait = p.Max([](const Meters& m) { return m.hitterWaitMax; }), stall = p.Max([](const Meters& m) { return m.stallMax; });
		const double hold = p.Max([](const Meters& m) { return m.holdFarMax; }), idle = p.Max([](const Meters& m) { return m.idleMax; });
		const double bursts = p.Sum([](const Meters& m) { return m.pingBursts; });
		S81_MARK(crowd == 1, god == 0, "%s DT3: the player fell below 30 %% %.0f times in %zu bursts (K07 = 0)", id, god, p.v.size());
		S81_MARK(crowd == 1, nk <= 2.5, "%s DT3: %.2f s without a kill while raiders hurt the player (want <= 2.5 s; s7.7c soak: 7.7 s)", id, nk);
		S81_MARK(crowd == 1, wait <= 1.5, "%s DT3: a raider that hit the player waited %.2f s while he worked on one that did not (want <= 1.5 s)", id, wait);
		S81_MARK(crowd == 1, stall <= 0.75 && hold <= 0.5, "%s DT3: stall %.2f s / hold %.2f s far from his target while the player was hurt (want <= 0.75 / 0.5 s; s8c skyStall / vDes=0)", id, stall,
		      hold);
		S81_MARK(crowd == 1, idle <= 1.0, "%s DT3: idle %.2f s while attacked (want <= 1.0 s)", id, idle);
		S81_MARK(crowd == 1, bursts == 0, "%s DT3: %.0f ping bursts (>= 4 picks in 0.2 s; CORE.md root cause 2)", id, bursts);
		cs.Note("%s; all dead med %.1f s", p.Common().c_str(), p.Med([](const Meters& m) { return m.allDeadAt; }));
	}
	inline void PR02(const char* id = "PR-02", int crowd = -1)
	{
		if (!Want(id)) return;
		CaseScope cs(id);
		Pool        p;
		double      tteMax = 0, deadMax = 0;
		int         never = 0, dropped = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			double firstHit = -1;
			int    drops = 0;
			std::uint32_t prev = 0;
			const Meters m = Keep(id, Run("DT1", static_cast<unsigned>(s), Dt1(static_cast<unsigned>(s)), 12.0, Rc(crowd), [&](PLoop& L, Meter& M) {
				const auto* a = L.w.Find(0xD100);
				auto        it = M.hs.find(0xD100);
				if (it != M.hs.end() && firstHit < 0) firstHit = it->second.firstHit;
				if (prev == 0xD100 && L.w.target != 0xD100 && a && a->Up()) ++drops;
				prev = L.w.target;
			}));
			p.Add(m);
			const double tte = m.Tte(0xD100), dead = m.Dead(0xD100);
			if (tte < -1.5) continue;  // it never hit the player (killed first): nothing to wait for
			if (tte < 0) ++never;
			else tteMax = std::max(tteMax, tte);
			if (dead < 0) ++never;
			else if (firstHit >= 0) deadMax = std::max(deadMax, dead - (firstHit - 100.0));
			dropped += drops;
		}
		S81_MARK(crowd == 1, never == 0 && tteMax <= 0.3, "%s DT1: the melee raider beating the player became his target %.2f s after its first hit (never %d; want <= 0.3 s)", id, tteMax, never);
		S81_MARK(crowd == 1, dropped == 0, "%s DT1: he dropped the melee raider %d times while it was up (want 0: s8c dt1 'tier=A pick dropped for tier-E foes')", id, dropped);
		S81_MARK(crowd == 1, deadMax <= 2.5, "%s DT1: the melee raider died %.2f s after its first hit at worst (want <= 2.5 s)", id, deadMax);
		S81_MARK(crowd == 1, p.Min([](const Meters& m) { return m.minHp; }) >= 70, "%s DT1: player low %.0f %% (want >= 70 %%)", id, p.Min([](const Meters& m) { return m.minHp; }));
		S81_MARK(crowd == 1, p.Sum([](const Meters& m) { return m.pingBursts; }) == 0, "%s DT1: %.0f ping bursts (>= 4 picks in 0.2 s)", id, p.Sum([](const Meters& m) { return m.pingBursts; }));
		cs.Note("%s; melee tte max %.2f s, dead <= %.2f s after its first hit", p.Common().c_str(), tteMax, deadMax);
	}
	inline void PR03()
	{
		if (!Want("PR-03")) return;
		CaseScope cs("PR-03");
		Pool   p;
		double aliasWhileHit = 0, aliasHold = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			double awh = 0, ahold = 0, run = 0;
			const Meters m = Keep("PR-03", Run("ALIAS", static_cast<unsigned>(s), Alias(static_cast<unsigned>(s)), 12.0, {}, [&](PLoop& L, Meter& M) {
				const double t = L.w.t;
				bool         hitterUp = false;
				for (const auto& a : L.w.actors)
					if (a.id != 0x56B7B && a.Up() && M.RecentHitter(a.id, t, 2.0)) hitterUp = true;
				if (L.w.target == 0x56B7B && hitterUp) awh += kDt;
				// hovering over the giant: it is his target, he is within 1000 u of it and makes no progress
				const auto* g = L.w.Find(0x56B7B);
				if (L.w.target == 0x56B7B && g && Length(g->Chest() - L.w.him.Torso()) < 1000 && Length(L.w.him.vel) < 150) {
					run += kDt;
					ahold = std::max(ahold, run);
				} else {
					run = 0;
				}
			}));
			p.Add(m);
			aliasWhileHit = std::max(aliasWhileHit, awh);
			aliasHold = std::max(aliasHold, ahold);
		}
		CHECK(aliasWhileHit <= 0.3, "PR-03 ALIAS: the hostile alias giant was his target %.2f s while a raider that hit the player was up (want <= 0.3 s; s7.7c soak 1)", aliasWhileHit);
		CHECK(p.Max([](const Meters& m) { return m.holdFarMax; }) <= 0.5 && p.Max([](const Meters& m) { return m.stallMax; }) <= 0.75,
		      "PR-03 ALIAS: hold %.2f s / stall %.2f s while the player was hurt (want <= 0.5 / 0.75 s)", p.Max([](const Meters& m) { return m.holdFarMax; }),
		      p.Max([](const Meters& m) { return m.stallMax; }));
		CHECK(p.Sum([](const Meters& m) { return m.godBacks; }) == 0, "PR-03 ALIAS: %.0f god-backs", p.Sum([](const Meters& m) { return m.godBacks; }));
		cs.Note("%s; alias picked while hurt max %.2f s, hover over it (no progress) max %.2f s", p.Common().c_str(), aliasWhileHit, aliasHold);
	}
	inline void PR04(const char* id = "PR-04", int crowd = -1)
	{
		if (!Want(id)) return;
		CaseScope cs(id);
		Pool p;
		for (int s = 1; s <= Seeds(12); ++s) p.Add(Keep(id, Run("NEXT", static_cast<unsigned>(s), Next(static_cast<unsigned>(s)), 12.0, Rc(crowd))));
		const double wait = p.Max([](const Meters& m) { return m.hitterWaitMax; }), wrong = p.Sum([](const Meters& m) { return m.wrongNextAfterKill; });
		const double drops = p.Sum([](const Meters& m) { return m.hitterDrops; }), low = p.Min([](const Meters& m) { return m.minHp; });
		S81_MARK(crowd == 1, wait <= 1.0, "%s NEXT: a raider hitting the player waited %.2f s while he worked on one that was not (want <= 1.0 s; s8c: FF001838 never picked)", id, wait);
		S81_MARK(crowd == 1, wrong == 0, "%s NEXT: %.0f times his next pick after a kill was a foe not hitting the player while one was (want 0)", id, wrong);
		S81_MARK(crowd == 1, drops == 0, "%s NEXT: %.0f drops of a target hitting the player for one that was not", id, drops);
		S81_MARK(crowd == 1, low >= 40, "%s NEXT: player low %.0f %% (want >= 40 %%)", id, low);
		cs.Note("%s", p.Common().c_str());
	}
	inline void PR05()
	{
		if (!Want("PR-05")) return;
		CaseScope cs("PR-05");
		Pool   p;
		double heavyTte = 0;
		int    secondThrows = 0, never = 0, below30 = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			int heavyHits = 0;
			const Meters m = Keep("PR-05", Run("CZ", static_cast<unsigned>(s), Cz(static_cast<unsigned>(s)), 16.0, {}, {}, [&](PLoop& L, Meter&) {
				for (const auto& h : L.w.playerHitLog)
					if (h.second == 0xD5E0) ++heavyHits;
			}));
			p.Add(m);
			const double t = m.Tte(0xD5E0);
			if (t > -1.5) {
				if (t < 0) ++never;
				else heavyTte = std::max(heavyTte, t);
			}
			secondThrows += std::max(0, heavyHits - 1);
			if (m.minHp < 30) ++below30;
		}
		CHECK(never == 0 && heavyTte <= 0.3, "PR-05 CZ: the explosive hitter became his target %.2f s after its first throw (never %d; want <= 0.3 s)", heavyTte, never);
		CHECK(secondThrows == 0, "PR-05 CZ: %d second throws (he must stop it inside 4 s)", secondThrows);
		CHECK(p.Sum([](const Meters& m) { return m.pingPong; }) == 0, "PR-05 CZ: ping-pong %.0f (s7.5c soak 1: 6 switches in 4.4 s)", p.Sum([](const Meters& m) { return m.pingPong; }));
		CHECK(below30 * 16 <= static_cast<int>(p.v.size()), "PR-05 CZ: %d of %zu sections below 30 %% (P02 <= 1 in 16)", below30, p.v.size());
		cs.Note("%s; explosive tte max %.2f s, second throws %d, sections < 30 %%: %d / %zu", p.Common().c_str(), heavyTte, secondThrows, below30, p.v.size());
	}
	inline void PR06()
	{
		if (!Want("PR-06")) return;
		CaseScope cs("PR-06");
		Pool   p;
		double busyMin = 1;
		for (int s = 1; s <= Seeds(8); ++s) {
			int attacked = 0, busy = 0;
			p.Add(Keep("PR-06", Run("FAR", static_cast<unsigned>(s), Far(static_cast<unsigned>(s)), 20.0, {}, [&](PLoop& L, Meter&) {
				bool any = false;
				for (const auto& a : L.w.actors)
					if (a.Up() && a.attacking) any = true;
				if (!any) return;
				++attacked;
				if (L.w.target) ++busy;
			})));
			if (attacked) busyMin = std::min(busyMin, static_cast<double>(busy) / attacked);
		}
		const double idle = p.Max([](const Meters& m) { return m.idleMax; });
		CHECK(idle <= 1.0, "PR-06 FAR: idle %.2f s while far raiders shot the player (LEAD 10-16: <= 1.0 s like the installed build; s8c dw=ignored 3.0 s)", idle);
		CHECK(busyMin >= 0.9, "PR-06 FAR: a target in only %.0f %% of the attacked frames (want >= 90 %%)", 100 * busyMin);
		cs.Note("%s; target while attacked >= %.0f %% of frames", p.Common().c_str(), 100 * busyMin);
	}
	inline void PR07()
	{
		if (!Want("PR-07")) return;
		CaseScope cs("PR-07");
		double worst = 0;
		int    never = 0, other = 0, runs = 0;
		for (int s = 1; s <= Seeds(12); ++s) {
			double shotAt = 0, at = -1;
			bool   otherPicked = false;
			Keep("PR-07", Run("P03", static_cast<unsigned>(s), Provoke(static_cast<unsigned>(s), &shotAt), 6.0, {}, [&](PLoop& L, Meter&) {
				const auto* r = L.w.Find(0xD700);
				if (at < 0 && L.w.Rel() >= shotAt && (L.w.target == 0xD700 || (r && !r->Up()))) at = L.w.Rel() - shotAt;
				if (L.w.target == 0xD701) otherPicked = true;
			}));
			++runs;
			if (at < 0) ++never;
			else worst = std::max(worst, at);
			if (otherPicked) ++other;
		}
		CHECK(never == 0 && worst <= 0.30, "PR-07 P03: the provoked raider became his target %.2f s after the player's shot (never %d of %d; want <= 0.30 s)", worst, never, runs);
		CHECK(other == 0, "PR-07 P03: the neutral the player did not shoot was picked in %d of %d runs (want 0)", other, runs);
		cs.Note("provoked raider picked <= %.2f s after the shot (never %d of %d); other neutral picked %d", worst, never, runs, other);
	}
	inline void PR08()
	{
		if (!Want("PR-08")) return;
		CaseScope cs("PR-08");
		Pool   p;
		double deadMax = 0;
		int    neverDead = 0;
		for (int s = 1; s <= Seeds(8); ++s) {
			double firstHit = -1;
			const Meters m = Keep("PR-08", Run("DOOR", static_cast<unsigned>(s), Door(static_cast<unsigned>(s)), 15.0, {}, [&](PLoop&, Meter& M) {
				auto it = M.hs.find(0xD800);
				if (it != M.hs.end() && firstHit < 0) firstHit = it->second.firstHit;
			}));
			p.Add(m);
			if (firstHit < 0) continue;  // killed before it reached the player
			const double dead = m.Dead(0xD800);
			if (dead < 0) ++neverDead;
			else deadMax = std::max(deadMax, dead - (firstHit - 100.0));
		}
		const double reach = p.Max([](const Meters& m) { return m.reachAfterHitMax; });
		CHECK(reach <= 2.0, "PR-08 DOOR: %.2f s from the melee raider's first hit to him at it (want <= 2.0 s; s8c cz2 blinkWait 2.8 s)", reach);
		CHECK(neverDead == 0 && deadMax <= 3.0, "PR-08 DOOR: the melee raider died %.2f s after its first hit (never %d; want <= 3.0 s)", deadMax, neverDead);
		CHECK(p.Min([](const Meters& m) { return m.minHp; }) >= 70, "PR-08 DOOR: player low %.0f %% (want >= 70 %%)", p.Min([](const Meters& m) { return m.minHp; }));
		cs.Note("%s; reach after the first hit max %.2f s, dead <= %.2f s after it", p.Common().c_str(), reach, deadMax);
	}
	inline void PR09()
	{
		if (!Want("PR-09")) return;
		CaseScope cs("PR-09");
		int    strayPicked = 0, runs = 0, turnedNever = 0;
		double turnedWorst = 0;
		Pool   p;
		for (int s = 1; s <= Seeds(12); ++s) {
			double turnAt = 0, at = -1;
			bool   stray = false;
			const Meters m = Keep("PR-09", Run("RULES", static_cast<unsigned>(s), Rules(static_cast<unsigned>(s), &turnAt), 9.0, {}, [&](PLoop& L, Meter&) {
				if (L.w.target == 0xD9A0) stray = true;
				const auto* a = L.w.Find(0xD9A1);
				if (at < 0 && L.w.Rel() >= turnAt && (L.w.target == 0xD9A1 || (a && !a->Up()))) at = L.w.Rel() - turnAt;
			}));
			p.Add(m);
			++runs;
			if (stray) ++strayPicked;
			if (at < 0) ++turnedNever;
			else turnedWorst = std::max(turnedWorst, at);
		}
		CHECK(strayPicked == 0 && p.Sum([](const Meters& m) { return m.friendlyHits + m.friendlyFrames; }) == 0,
		      "PR-09 RULES (d): the settler whose stray shot hit the player was his target in %d of %d runs, friendly hits / frames %.0f (want 0: never a trigger)", strayPicked, runs,
		      p.Sum([](const Meters& m) { return m.friendlyHits + m.friendlyFrames; }));
		// (a) is REPORTED, not judged: the shipped `[R6] Warden = 0` path keeps the V08 settler veto even for a settler that hits the
		// player (fo4/scan_core.h defendTurretLink lifts the workshop LINK of turrets, not WorkshopNPCFaction settlers), in the
		// installed build too - a gap of the user rule 10-14 20:59 (a), not part of the step-8.1 regression (TESTPLAN F-notes)
		std::printf("S81INFO|PR-09a|turned settler engaged <= %.2f s after it turned, never in %d of %d runs (user rule a; installed path: never)\n", turnedWorst, turnedNever, runs);
		cs.Note("stray settler picked %d / %d, turned settler engaged <= %.2f s (never %d)", strayPicked, runs, turnedWorst, turnedNever);
	}
	// PR-10: a desktop soak10 - per cycle dt1, dt3 (the burst), cz (x2), the settlement raid; 3 soaks (seeds) x 3 cycles
	inline void PR10(const char* id = "PR-10", int crowd = -1)
	{
		if (!Want(id)) return;
		CaseScope cs(id);
		const int soaks = g_quick ? 2 : 3;
		int       god = 0, deaths = 0, czSec = 0, czLow = 0;
		double    low = 100;
		std::string per;
		for (int k = 1; k <= soaks; ++k) {
			int godK = 0;
			for (int cyc = 0; cyc < 3; ++cyc) {
				const unsigned s = static_cast<unsigned>(1000 * k + 10 * cyc);
				std::vector<Meters> seg;
				seg.push_back(Keep(id, Run("SOAK.dt1", s + 1, Dt1(s + 1), 12.0, Rc(crowd))));
				seg.push_back(Keep(id, Run("SOAK.dt3", s + 2, Dt3(s + 2), 30.0, Rc(crowd))));
				seg.push_back(Keep(id, Run("SOAK.cz1", s + 3, Cz(s + 3), 16.0, Rc(crowd))));
				seg.push_back(Keep(id, Run("SOAK.cz2", s + 4, Door(s + 4), 15.0, Rc(crowd))));
				seg.push_back(Keep(id, Run("SOAK.raid", s + 5, Rules(s + 5, nullptr), 9.0, Rc(crowd))));
				for (size_t i = 0; i < seg.size(); ++i) {
					godK += seg[i].godBacks;
					deaths += seg[i].deaths;
					low = std::min(low, seg[i].minHp);
					if (i == 2 || i == 3) {
						++czSec;
						if (seg[i].minHp < 30) ++czLow;
					}
				}
			}
			god += godK;
			char b[48];
			std::snprintf(b, sizeof b, "%s%d", per.empty() ? "" : "/", godK);
			per += b;
		}
		S81_MARK(crowd == 1, god == 0, "%s SOAK: the player fell below 30 %% %d times in %d desktop soaks (%s per soak; K07 = 0)", id, god, soaks, per.c_str());
		S81_MARK(crowd == 1, deaths == 0, "%s SOAK: %d deaths (hp <= 0 before the god-back; P19 = 0)", id, deaths);
		S81_MARK(crowd == 1, czLow * 16 <= czSec, "%s SOAK: Combat Zone sections below 30 %%: %d of %d (P02 <= 0.0625)", id, czLow, czSec);
		cs.Note("god-backs %d (%s), deaths %d, lowest %.0f %%, cz sections < 30 %%: %d / %d", god, per.c_str(), deaths, low, czLow, czSec);
	}
	inline void PR11()
	{
		if (!Want("PR-11")) return;
		CaseScope cs("PR-11");
		Pool p;
		RunCfg rc;
		rc.god = false;
		for (int s = 1; s <= Seeds(8); ++s) p.Add(Keep("PR-11", Run("ROOM", static_cast<unsigned>(s), Room(static_cast<unsigned>(s)), 15.0, rc)));
		CHECK(p.Sum([](const Meters& m) { return m.deaths; }) == 0, "PR-11 ROOM: the player died in %.0f of %zu death rooms (god mode off; P17)", p.Sum([](const Meters& m) { return m.deaths; }), p.v.size());
		cs.Note("%s; all dead med %.1f s", p.Common().c_str(), p.Med([](const Meters& m) { return m.allDeadAt; }));
	}

	inline void Run()
	{
		PR01();
		PR02();
		PR03();
		PR04();
		PR05();
		PR06();
		PR07();
		PR08();
		PR09();
		PR10();
		PR11();
		// the B cases: the same scenes with the crowd-blind eyes of the s6f / s8c builds forced on (CORE.md arm A2: the guard alone)
		PR01("PR-01B", 1);
		PR02("PR-02B", 1);
		PR04("PR-04B", 1);
		PR10("PR-10B", 1);
	}

	// ================================================================================================ outputs
	inline void WriteReport(const char* a_path)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "| case | result | detail |\n|---|---|---|\n";
		for (const auto& c : Cases()) {
			std::string d = c.detail;
			for (char& ch : d)
				if (ch == '|') ch = '/';
			o << "| " << c.id << " | " << (c.pass ? "PASS" : "FAIL") << " | " << d << " |\n";
		}
	}
	inline std::string JsonStr(const std::string& s)
	{
		std::string r = "\"";
		for (char c : s) {
			if (c == '"' || c == '\\') r += '\\';
			if (static_cast<unsigned char>(c) < 0x20) continue;
			r += c;
		}
		return r + "\"";
	}
	inline void WriteJson(const char* a_path, const char* a_arm)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "{\"arm\":" << JsonStr(a_arm) << ",\"tree\":" << JsonStr(s81api::TreeName()) << ",\"profile\":" << JsonStr(s81api::Prof().label)
		  << ",\"ini\":" << JsonStr(s81api::Prof().ini) << ",\"switches\":" << JsonStr(s81api::SwitchLine()) << ",\"quick\":" << (g_quick ? "true" : "false") << ",\"cases\":[";
		for (size_t i = 0; i < Cases().size(); ++i) {
			const auto& c = Cases()[i];
			o << (i ? "," : "") << "{\"id\":" << JsonStr(c.id) << ",\"pass\":" << (c.pass ? "true" : "false") << ",\"detail\":" << JsonStr(c.detail) << "}";
		}
		o << "],\"runs\":[";
		char b[1400];
		for (size_t i = 0; i < Runs().size(); ++i) {
			const auto& [cid, m] = Runs()[i];
			std::snprintf(b, sizeof b,
				"{\"case\":\"%s\",\"scene\":\"%s\",\"seed\":%u,\"secs\":%.1f,\"minHp\":%.2f,\"endHp\":%.2f,\"godBacks\":%d,\"deaths\":%d,\"firstGodAt\":%.3f,\"foes\":%d,"
				"\"kills\":%d,\"allDeadAt\":%.3f,\"noKillHitMax\":%.3f,\"hitterWaitMax\":%.3f,\"hitterWaitId\":\"%X\",\"nonHitterWhileHit\":%.3f,\"hitterDrops\":%d,"
				"\"wrongNextAfterKill\":%d,\"pingPong\":%d,\"switches\":%d,\"stallMax\":%.3f,\"holdFarMax\":%.3f,\"idleMax\":%.3f,\"idle\":%.3f,\"farFromPlayerMax\":%.3f,"
				"\"reachAfterHitMax\":%.3f,\"relocs\":%d,\"relocsSeen\":%d,\"blinks\":%d,\"putBacks\":%d,\"friendly\":%d,\"hitsOnPlayer\":%d,\"slideFrames\":%d,\"pingBursts\":%d,\"picks\":%d,\"exhaustedPct\":%.1f}",
				cid.c_str(), m.scene.c_str(), m.seed, m.secs, m.minHp, m.endHp, m.godBacks, m.deaths, m.firstGodAt, m.foes, m.kills, m.allDeadAt, m.noKillHitMax, m.hitterWaitMax,
				m.hitterWaitId, m.nonHitterWhileHit, m.hitterDrops, m.wrongNextAfterKill, m.pingPong, m.switches, m.stallMax, m.holdFarMax, m.idleMax, m.idle,
				m.farFromPlayerMax, m.reachAfterHitMax, m.relocs, m.relocsSeen, m.blinks, m.putBacks, m.friendlyHits + m.friendlyFrames, m.hitsOnPlayer, m.slideFrames, m.pingBursts, m.picks, m.exhaustedPct);
			o << (i ? "," : "") << b;
		}
		o << "]}\n";
	}
}
