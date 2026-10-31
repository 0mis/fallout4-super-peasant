// FINISH LANE 5 (A_explosions_bursts, desktop, sp_tests): core/blast.h - the sentry-bot death blast launch (look.blastAway), pure units on
// a box world (the closed loop with the real brain is s81_tests F5-BLAST).
//   BL-01 the race: only SentryBotRace 0x000AE0B7 has the on-death blast (assaultron / protectron / Mr Handy / a raider: no)
//   BL-02 the blast model of robotSelfDestructExplosion: 150 within 200 u, 0 from 768 u, falling in between
//   BL-03 off (the code default) or the player beyond look.blastSafeR: the shipped direction comes back unchanged (bit exact)
//   BL-04 open floor: the heading is straight away from the player, the elevation kept, unit length
//   BL-05 a wall behind the sentry (the recorded GA factory / Hester's latch): the heading whose free run ends farthest wins (the
//         open side), far beyond the latched corpse
//   BL-06 a closed room: no heading is free; a hidden end point wins at once, else the farthest partial run
//   BL-07 the ray budget: never more than look.blastMaxRays rays
//   BL-08 the shipped ini carries the keys (read as text)
//   BL-09 a settler / teammate (the fling lane's protected points) near the straight-away end: the corpse ends away from both
// Included by tests/test_core.cpp after test_s13r3l5.inc.
#include "core/blast.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace f5blast_tests
{
	struct Box
	{
		Vec3 lo, hi;
	};
	inline bool SegHits(const Vec3& a, const Vec3& b, const Box& bx)
	{
		double       t0 = 0, t1 = 1;
		const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z }, L[3] = { bx.lo.x, bx.lo.y, bx.lo.z }, H[3] = { bx.hi.x, bx.hi.y, bx.hi.z };
		for (int k = 0; k < 3; ++k) {
			const double d = B[k] - A[k];
			if (std::abs(d) < 1e-12) {
				if (A[k] < L[k] || A[k] > H[k]) return false;
				continue;
			}
			double ta = (L[k] - A[k]) / d, tb = (H[k] - A[k]) / d;
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			if (t0 > t1) return false;
		}
		return true;
	}
	// the entry fraction of a -> b into the box (-1 none; 0 = a inside it)
	inline double SegT(const Vec3& a, const Vec3& b, const Box& bx)
	{
		double       t0 = 0, t1 = 1;
		const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z }, L[3] = { bx.lo.x, bx.lo.y, bx.lo.z }, H[3] = { bx.hi.x, bx.hi.y, bx.hi.z };
		for (int k = 0; k < 3; ++k) {
			const double d = B[k] - A[k];
			if (std::abs(d) < 1e-12) {
				if (A[k] < L[k] || A[k] > H[k]) return -1;
				continue;
			}
			double ta = (L[k] - A[k]) / d, tb = (H[k] - A[k]) / d;
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			if (t0 > t1) return -1;
		}
		return t0;
	}
	struct World
	{
		std::vector<Box> boxes;
		int              rays{ 0 };
		bool Clear(const Vec3& a, const Vec3& b)
		{
			for (const auto& x : boxes)
				if (SegHits(a, b, x)) return false;
			return true;
		}
		// the free run from a along the unit heading h (len when nothing is hit) - the body-wall ray's fraction x len
		double Run(const Vec3& a, const Vec3& h, double len)
		{
			++rays;
			double t = 1.0;
			for (const auto& x : boxes)
				if (const double u = SegT(a, a + h * len, x); u >= 0) t = std::min(t, u);
			return t * len;
		}
	};
	inline sp::blast::Cfg On()
	{
		sp::blast::Cfg c;
		c.on = true;
		return c;
	}
	inline double FlatDeg(const Vec3& a, const Vec3& b)
	{
		const double d = std::atan2(a.y, a.x) - std::atan2(b.y, b.x);
		double       r = std::fmod(std::fabs(d) * 180.0 / 3.14159265358979323846, 360.0);
		return r > 180 ? 360 - r : r;
	}

	inline void Run()
	{
		// BL-01
		CHECK(sp::blast::DeathBlastRace(0x000AE0B7) && !sp::blast::DeathBlastRace(0x000D8417) && !sp::blast::DeathBlastRace(0x000DFB33) && !sp::blast::DeathBlastRace(0x000359F4) &&
		          !sp::blast::DeathBlastRace(0x00013746) && !sp::blast::DeathBlastRace(0),
			"BL-01 only SentryBotRace has the on-death blast");
		// BL-02
		{
			using sp::blast::BlastDamage;
			bool mono = true;
			for (double d = 0; d < 900; d += 25)
				if (BlastDamage(d + 25) > BlastDamage(d) + 1e-12) mono = false;
			CHECK(BlastDamage(0) == 150 && BlastDamage(200) == 150 && BlastDamage(768) == 0 && BlastDamage(2000) == 0 && BlastDamage(484) > 70 && BlastDamage(484) < 80 && mono,
				"BL-02 the blast model: 150 / 150 / %.1f / 0 at 0 / 200 / 484 / 768 u, monotone %d", BlastDamage(484), mono ? 1 : 0);
		}
		const Vec3 player{ 0, 0, 90 }, vic{ 250, 0, 120 };
		const Vec3 shipped = Normalize(Vec3{ 0.2, 0.95, 0.21 }, { 1, 0, 0 });  // a launch along the side of the player
		// BL-03
		{
			World w;
			auto  clr = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
			auto  seen = [&](const Vec3&) { return true; };
			const auto p0 = sp::blast::ChooseAway(shipped, vic, player, sp::blast::Cfg{}, clr, seen);
			sp::blast::Cfg cfar = On();
			cfar.safeR = 200;
			const auto p1 = sp::blast::ChooseAway(shipped, vic, player, cfar, clr, seen);
			CHECK(!p0.applied && !p1.applied && p0.dir.x == shipped.x && p0.dir.y == shipped.y && p0.dir.z == shipped.z && p1.dir.x == shipped.x && p1.dir.y == shipped.y &&
			          p1.dir.z == shipped.z && w.rays == 0,
				"BL-03 off / beyond safeR: unchanged (applied %d / %d, rays %d)", p0.applied ? 1 : 0, p1.applied ? 1 : 0, w.rays);
		}
		// BL-04
		{
			World      w;
			auto       clr = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
			auto       seen = [&](const Vec3&) { return true; };
			const auto p = sp::blast::ChooseAway(shipped, vic, player, On(), clr, seen);
			CHECK(p.applied && p.free && FlatDeg(p.dir, vic - player) < 1e-6 && std::fabs(p.dir.z - shipped.z) < 1e-9 && std::fabs(Length(p.dir) - 1.0) < 1e-9 && p.turnDeg == 0 &&
			          std::fabs(p.sepEnd - 700.0) < 1e-6 && p.rays <= 2,
				"BL-04 open floor: straight away (off %.3f deg), z %.3f (kept %.3f), len %.6f, end %.0f u from the player (250 + travel 450), rays %d", FlatDeg(p.dir, vic - player), p.dir.z,
				shipped.z, Length(p.dir), p.sepEnd, p.rays);
		}
		// BL-05 a wall 100 u behind the sentry, across the line from the player (+x), 1400 u long, and a side wall at y = +400
		{
			World w;
			w.boxes.push_back({ { 350, -700, 0 }, { 380, 700, 400 } });
			w.boxes.push_back({ { -600, 400, 0 }, { 400, 430, 400 } });
			auto       clr = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
			auto       seen = [&](const Vec3&) { return true; };
			const auto p = sp::blast::ChooseAway(Vec3{ 0.97, 0, 0.21 }, vic, player, On(), clr, seen);  // the swing ran into the wall
			// straight away ends at 290 u (the wall 100 u behind, less the 60 u margin); -80 deg slides 450 u along the open side: 551 u
			CHECK(p.applied && p.changed && !p.free && !p.safe && std::fabs(p.turnDeg + 80.0) < 1e-9 && p.dir.y < 0 && p.sepEnd > 540 && p.sepEnd < 560,
				"BL-05 wall behind: %.0f deg off straight away (want -80: the open side), y %.2f, end %.0f u from the player (straight away: 290; the latched corpse: 250)", p.turnDeg,
				p.dir.y, p.sepEnd);
		}
		// BL-06 a closed room 700 x 700 around both; the sight check says the player cannot see the end point past y < -200 (a pillar)
		{
			World w;
			w.boxes.push_back({ { -350, -350, 0 }, { 450, -330, 400 } });
			w.boxes.push_back({ { -350, 330, 0 }, { 450, 350, 400 } });
			w.boxes.push_back({ { -370, -350, 0 }, { -350, 350, 400 } });
			w.boxes.push_back({ { 450, -350, 0 }, { 470, 350, 400 } });
			auto       clr = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
			auto       seen = [&](const Vec3& e) { return e.y > -200; };
			const auto p = sp::blast::ChooseAway(Vec3{ 0, 0.97, 0.21 }, vic, player, On(), clr, seen);
			auto       seen2 = [&](const Vec3&) { return true; };
			const auto q = sp::blast::ChooseAway(Vec3{ 0, 0.97, 0.21 }, vic, player, On(), clr, seen2);
			CHECK(p.applied && !p.free && p.hidden && p.safe && p.dir.y < 0 && std::fabs(p.turnDeg + 60.0) < 1e-9 && q.applied && !q.free && !q.hidden && !q.safe && q.sepEnd > 490 &&
			          q.sepEnd < 500,
				"BL-06 closed room: hidden end picked (free %d hidden %d y %.2f); without a hiding place the farthest partial run (%.0f u, free %d)", p.free ? 1 : 0, p.hidden ? 1 : 0, p.dir.y,
				q.sepEnd, q.free ? 1 : 0);
		}
		// BL-07
		{
			World w;
			w.boxes.push_back({ { 240, -10, 0 }, { 260, 10, 400 } });  // the victim inside a box: every heading is blocked at once
			int   seenRays = 0;
			auto  clr = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
			auto  seen = [&](const Vec3&) {
                ++seenRays;
                return true;
			};
			sp::blast::Cfg c = On();
			c.stepDeg = 5;
			c.maxTurnDeg = 180;
			c.maxRays = 24;
			const auto p = sp::blast::ChooseAway(shipped, vic, player, c, clr, seen);
			CHECK(p.applied && p.rays <= 24 && w.rays + seenRays <= 24 && p.rays == w.rays + seenRays && sp::math::IsFinite(p.dir) && std::fabs(Length(p.dir) - 1.0) < 1e-9,
				"BL-07 ray budget: %d rays (wall %d + sight %d), cap 24", p.rays, w.rays, seenRays);
		}
		// BL-09 a settler on the far side (the fling lane's protected points): the corpse never ends next to it either
		{
			World                   w;
			auto                    clr = [&](const Vec3& a, const Vec3& h, double len) { return w.Run(a, h, len); };
			auto                    seen = [&](const Vec3&) { return true; };
			const std::vector<Vec3> others{ Vec3{ 700, 0, 90 } };
			const auto              p = sp::blast::ChooseAway(shipped, vic, player, On(), clr, seen, &others);
			const Vec3              e = vic + Normalize(Vec3{ p.dir.x, p.dir.y, 0 }, { 1, 0, 0 }) * 450.0;
			const double            dSet = std::hypot(e.x - 700, e.y), dPl = std::hypot(e.x, e.y);
			CHECK(p.applied && std::fabs(std::fabs(p.turnDeg) - 80.0) < 1e-9 && dSet > 500 && dPl > 500,
				"BL-09 a settler 450 u past the sentry: %.0f deg off straight away (want +-80), the end %.0f u from it and %.0f u from you", p.turnDeg, dSet, dPl);
		}
		// BL-08 the shipped ini (text)
		{
			std::ifstream f("SuperPeasantNative.ini");
			if (!f) f.open("..\\SuperPeasantNative.ini");
			std::stringstream ss;
			ss << f.rdbuf();
			const std::string t = ss.str();
			const auto        has = [&](const char* k) { return t.find(k) != std::string::npos; };
			CHECK(has("\nlook.blastAway = 1 ") && has("\nlook.blastSafeR = 1100 ") && has("\nlook.blastStepDeg = 20 ") && has("\nlook.blastTurnMax = 100 ") && has("\nlook.blastLookU = 700 ") &&
			          has("\nlook.blastMarginU = 60 ") && has("\nlook.blastTravelU = 450 ") && has("\nlook.blastMaxRays = 24 "),
				"BL-08 the shipped ini carries look.blastAway 1 and its keys");
		}
	}
}
