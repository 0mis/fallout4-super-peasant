// OWNER: R6 (round 6 step 5). Included by runtime.cpp inside its anonymous namespace (shares g / g_lock / DownReason).
// "Nothing knocks him down, hits make him wobble" (TABS_LOOK_PLAN rank 6), the sim half. The engine half is in
// fo4/look_hooks.cpp (L4 / L8 / L32 vetoes, the P5 knock backstop + health floor, the P6 velocityMod read) and
// fo4/hitsink.cpp (a record per hit on him). Every producer pushes into one lock-free ring; this drains it once per tick
// and turns each record into capped TABS velocity changes on his torso + the part nearest the hit (core/unstoppable.h).
// The TABS body (Wings, the StandingHandler) brings him back: he never falls (TABS: Wings sets canFall = false).
// Keys (code default = before step 5): look.hitWobble 0, look.hitWobbleMax 15 (m/s), look.hitWobbleWindow 0.25 (s),
// look.hitWobbleKnock 0.5 (m/s per knock magnitude), look.hitWobbleHit 1 (x the hit's knockback speed), look.hitWobbleVmod 1
// (x the capsule knockback, 1:1), look.hitWobbleMin 0.05, look.hitWobbleTorso 1, look.hitWobbleNoUp 0, look.hitWobbleMeleeUp 0 (m) /
// look.hitWobbleMelee 1 (the melee rule, WobbleCfg::meleeUpM / meleeGain), look.hitMergeWindow 0.1 (s:
// records of one hit are never summed, s5 HOOKS.md D4); TEST: debug.wobbleKick, debug.wobbleHit (+ debug.wobbleHitHd).
// The hit sink's size keys (look.hitWobbleWeapon, look.hitWobbleMeleeMin / ProjMin / ExplMin, look.hitKb*) are in core/hit_size.h.

struct UnstopSim
{
	sp::unstop::Budget                    budget;
	sp::unstop::MergeWindow               merge;    // one hit, up to 3 records (s5 HOOKS.md D4): never summed
	double                                clock{ 0 };  // s of P5 ticks (the merge window's clock)
	std::uint64_t                         drained{ 0 }, applied{ 0 }, skipped{ 0 }, small{ 0 }, capped{ 0 }, merged{ 0 }, kicks{ 0 }, evLogs{ 0 }, recLogs{ 0 };
	std::array<std::uint64_t, 5>          bySrc{};  // applied records per sp::unstop::Src
	double                                maxDv{ 0 }, lastDv{ 0 };
	// tilt watch (in-game acceptance: torso tilt after a pipe hit 20-45 deg, back within 1 s)
	bool                                  watching{ false };
	double                                watchT{ 0 }, tiltMax{ 0 }, tiltMaxAt{ 0 }, backAt{ -1 }, watchDv{ 0 };
	int                                   watchSrc{ -1 };
	Vec3                                  up0{};
	std::uint64_t                         watches{ 0 }, watchBack{ 0 };
	double                                tiltMaxEver{ 0 };
	// the stat line
	double                                lineAt{ -1e9 };
	std::uint64_t                         lineSig{ 0 }, lines{ 0 };
};
UnstopSim g_un5;

sp::unstop::WobbleCfg WobbleCfgNow()
{
	sp::unstop::WobbleCfg c;
	c.maxDv = Tunable("look.hitWobbleMax", 15.0);
	c.window = Tunable("look.hitWobbleWindow", 0.25);
	c.knockGain = Tunable("look.hitWobbleKnock", 0.5);
	c.hitGain = Tunable("look.hitWobbleHit", 1.0);
	c.vmodScale = Tunable("look.hitWobbleVmod", 1.0);
	c.minDv = Tunable("look.hitWobbleMin", 0.05);
	c.torso = Tunable("look.hitWobbleTorso", 1.0) != 0;
	c.noUp = Tunable("look.hitWobbleNoUp", 0.0) != 0;
	c.meleeUpM = Tunable("look.hitWobbleMeleeUp", 0.0);  // integrator (TESTPLAN F1): a swing lands on his upper chest / head
	c.meleeGain = Tunable("look.hitWobbleMelee", 1.0);   // x look.hitWobbleHit for melee hits
	return c;
}

Vec3 WobbleTorsoUp() { return g.body.Part(sp::body::kTorso).rot.Rotate(Vec3{ 0, 1, 0 }); }

// GameTick, right after GetPeasant and BEFORE the down gate (so a knock state the backstop clears never pauses him).
void UnstopTick(RE::Actor* me, float dt, int mode)
{
	sp::fo4::look::UnstopP5(me, mode);  // the engine half: knock backstop, health floor, counters (P5 = the engine-write safe point)
	auto&      u = g_un5;
	const auto cfg = WobbleCfgNow();
	u.budget.Decay(dt, cfg);
	if (dt > 0 && dt < 1.0) u.clock += dt;
	const double mergeWin = Tunable("look.hitMergeWindow", 0.1);
	const bool on = Tunable("look.hitWobble", 0.0) != 0;
	// TEST helper (default 0): SetTunable("debug.wobbleKick", v) pushes ONE sideways push of v m/s (his right) through the
	// ring like a real hit, then sets the key back to 0. The in-game tune of the tilt numbers without an enemy.
	if (const double k = Tunable("debug.wobbleKick", 0.0); k > 0 && me) {
		sp::cfg::SetTunable("debug.wobbleKick", 0.0);
		const double     h = me->data.angle.z;
		sp::unstop::Rec  r;
		r.src = static_cast<std::uint8_t>(sp::unstop::Src::kTest);
		r.flags = sp::unstop::kHasDir;
		r.dx = static_cast<float>(std::cos(h));
		r.dy = static_cast<float>(-std::sin(h));
		r.mag = static_cast<float>(k);
		sp::fo4::look::PushWobble(r);
		++u.kicks;
		REX::INFO("SPN|LOOK|wobble kick {:.1f} m/s (debug.wobbleKick) n={}", k, u.kicks);
	}
	// TEST helper (default 0, integrator): SetTunable("debug.wobbleHit", damage) pushes ONE melee hit-sink record from an
	// attacker standing 110 u in front of him (point = there, dir = attacker -> him), then sets the key back to 0. The in-game
	// check of the pipe-hit tilt when no melee enemy can reach his hover. Fix lane 1 (REVIEW W1): the record is built by the
	// hit sink's OWN code (fo4/hitsink.cpp PushTestMelee: core/hit_size.h SizeHit with the live keys) for a weapon of base damage
	// `damage` (a lead pipe is 10) whose hit carries the healthDamage a real swing on him has (debug.wobbleHitHd, 0.0001: his
	// x0.01 perk and his armor), so it no longer skips the step that made every real hit the minimum size.
	if (const double hd = Tunable("debug.wobbleHit", 0.0); hd > 0 && me) {
		sp::cfg::SetTunable("debug.wobbleHit", 0.0);
		const double       h = me->data.angle.z;
		const RE::NiPoint3 p = me->GetPosition();
		const float        att[3]{ static_cast<float>(p.x + std::sin(h) * 110.0), static_cast<float>(p.y + std::cos(h) * 110.0), p.z };
		const float        him[3]{ p.x, p.y, p.z };
		const double       hdmg = Tunable("debug.wobbleHitHd", 0.0001);
		const float        speed = sp::fo4::hitsink::PushTestMelee(att, him, hd, hdmg);
		++u.kicks;
		REX::INFO("SPN|LOOK|wobble hit {:.0f} damage (healthDamage {:.6g}) -> {:.0f} u/s melee from the front (debug.wobbleHit, the hit sink's size rule) n={}", hd, hdmg, speed, u.kicks);
	}
	std::array<sp::unstop::Rec, 32> recs{};
	const int                       n = sp::fo4::look::DrainWobble(recs.data(), static_cast<int>(recs.size()));
	if (n > 0) {
		u.drained += static_cast<std::uint64_t>(n);
		// only while his TABS body runs and is his own (not down, not carrying the player): otherwise the records are dropped
		const bool can = on && mode >= 2 && me && g.body.IsReady() && !DownReason(me) && !CarryActive();
		Vec3       parts[sp::body::kPartCount]{};
		if (can)
			for (int i = 0; i < sp::body::kPartCount; ++i) parts[i] = g.body.Part(i).pos;
		const double upm = g.body.IsReady() ? g.body.Upm() : sp::cfg::K::kUpmDefault;
		// s5 HOOKS.md D4 order inside one drain: the knock (point + magnitude) first, then the hit, then the capsule push
		std::stable_sort(recs.begin(), recs.begin() + n, [](const sp::unstop::Rec& a, const sp::unstop::Rec& b) { return a.src < b.src; });
		const auto mePos = me ? me->GetPosition() : RE::NiPoint3{};
		for (int i = 0; i < n; ++i) {
			const auto& r = recs[i];
			// s5 HOOKS.md F per-event lines (first 50, then every 50th): what reached him, from where
			if (++u.recLogs <= 50 || (u.recLogs % 50) == 0) {
				const auto s = static_cast<sp::unstop::Src>(r.src);
				const double d = (r.flags & sp::unstop::kHasPoint) ? std::sqrt((r.px - mePos.x) * (r.px - mePos.x) + (r.py - mePos.y) * (r.py - mePos.y) + (r.pz - mePos.z) * (r.pz - mePos.z)) : -1.0;
				if (s == sp::unstop::Src::kKnockA || s == sp::unstop::Src::kParalyze)
					REX::INFO("SPN|LOOK|UNSTOP|veto{} at={:.0f},{:.0f},{:.0f} mag={:.2f} d={:.0f} tid={} n={}", s == sp::unstop::Src::kKnockA ? "A" : "B", r.px, r.py, r.pz, r.mag, d, r.tid, u.recLogs);
				else if (s == sp::unstop::Src::kHitSink)
					REX::INFO("SPN|LOOK|UNSTOP|sink kind={} speed={:.0f} flags=0x{:X} at={:.0f},{:.0f},{:.0f} d={:.0f} dir={:.2f},{:.2f},{:.2f} tid={} n={}",
						(r.flags & sp::unstop::kMelee) ? "melee" : ((r.flags & sp::unstop::kExplosion) ? "expl" : "proj"), r.mag, r.flags, r.px, r.py, r.pz, d, r.dx, r.dy, r.dz, r.tid, u.recLogs);
				else if (s == sp::unstop::Src::kVelMod)
					REX::INFO("SPN|LOOK|UNSTOP|vmod v={:.2f},{:.2f},{:.2f} speed={:.0f}u/s t={}ms kind=hit n={}", r.dx, r.dy, r.dz, r.mag, r.frame, u.recLogs);
			}
			if (!can || r.src == static_cast<std::uint8_t>(sp::unstop::Src::kParalyze) || r.src >= static_cast<std::uint8_t>(sp::unstop::Src::kCount)) {
				++u.skipped;
				continue;
			}
			const auto plan = sp::unstop::PlanWobble(r, parts, sp::body::kPartCount, cfg, upm);
			if (plan.n == 0) {
				++u.small;
				continue;
			}
			const double share = u.merge.Admit(static_cast<sp::unstop::Src>(r.src), plan.dv, u.clock, mergeWin);
			if (share <= 0.0) {
				++u.merged;
				continue;
			}
			if (share < 1.0) ++u.merged;  // topped up to a bigger record of the same hit
			double got = 0;
			int    gotPart = -1;
			for (int j = 0; j < plan.n; ++j) {
				const Vec3   dv = u.budget.Admit(plan.imp[j].part, plan.imp[j].dvU * share, cfg);
				const double m = Length(dv);
				if (m > 0) {
					g.body.AddVelocity(plan.imp[j].part, dv);
					if (m > got) {
						got = m;
						gotPart = plan.imp[j].part;
					}
				}
			}
			if (got <= 0) {
				++u.capped;
				continue;
			}
			if (got + 1e-6 < plan.dv * share) ++u.capped;
			++u.applied;
			++u.bySrc[r.src];
			u.lastDv = got;
			u.maxDv = std::max(u.maxDv, got);
			if (!u.watching || got > u.watchDv) {  // the tilt watch follows the biggest push of the last 1.5 s
				u.watching = true;
				u.watchT = 0;
				u.up0 = WobbleTorsoUp();
				u.tiltMax = 0;
				u.tiltMaxAt = 0;
				u.backAt = -1;
				u.watchSrc = r.src;
				u.watchDv = got;
				++u.watches;
			}
			if (++u.evLogs <= 300 || (u.evLogs % 50) == 0)
				REX::INFO("SPN|LOOK|wobble src={} mag={:.1f} dv={:.2f}/{:.2f} part={} nearest={} n={} flags=0x{:X} tid={}", sp::unstop::SrcName(static_cast<sp::unstop::Src>(r.src)), r.mag, got,
					plan.dv, sp::body::RigName(gotPart), plan.nearest >= 0 ? sp::body::RigName(plan.nearest) : "-", u.applied, r.flags, r.tid);
		}
	}
	if (u.watching) {
		if (!g.body.IsReady()) {
			u.watching = false;
		} else {
			u.watchT += dt;
			const double tilt = AngleDeg(WobbleTorsoUp(), u.up0);
			if (tilt > u.tiltMax) {
				u.tiltMax = tilt;
				u.tiltMaxAt = u.watchT;
				u.backAt = -1;  // integrator: "back" = under 10 deg after the HIGHEST tilt (a later, bigger peak resets it)
			}
			if (u.backAt < 0 && u.tiltMax >= 5.0 && u.watchT > u.tiltMaxAt && tilt <= 10.0) u.backAt = u.watchT;
			if (u.watchT >= 1.5) {
				u.watching = false;
				u.tiltMaxEver = std::max(u.tiltMaxEver, u.tiltMax);
				if (u.backAt >= 0) ++u.watchBack;
				REX::INFO("SPN|LOOK|wobble done src={} dv={:.2f} tiltMax={:.1f}deg at={:.2f}s back={:.2f}s n={}", sp::unstop::SrcName(static_cast<sp::unstop::Src>(u.watchSrc)), u.watchDv, u.tiltMax,
					u.tiltMaxAt, u.backAt, u.watches);
			}
		}
	}
	// SPN|LOOK|UNSTOP: every look.unstopLogEvery s (1) when an event counter moved, else once a minute (s5 HOOKS.md F)
	const double now = brain_glue::Now();
	if (me && mode > 0 && now - u.lineAt >= std::max(1.0, Tunable("look.unstopLogEvery", 1.0))) {
		// the signature covers EVENT counters only (frame counts move every frame): a line when something happened, else
		// one a minute (the per-minute '= 0' acceptance needs the totals at least once a minute)
		const std::uint64_t sig = sp::fo4::look::UnstopSignature() ^ (u.drained * 1315423911ull) ^ (u.applied << 20) ^ (u.kicks << 40);
		if (sig != u.lineSig || now - u.lineAt >= 60.0) {
			u.lineSig = sig;
			u.lineAt = now;
			++u.lines;
			REX::INFO("SPN|LOOK|UNSTOP {} | wob on={} drained={} applied={} skipped={} small={} merged={} capped={} kicks={} maxDv={:.2f} lastDv={:.2f} bySrc={}/{}/{}/{} tilt watches={} back={} maxEver={:.1f}",
				sp::fo4::look::UnstopDescribe(), on ? 1 : 0, u.drained, u.applied, u.skipped, u.small, u.merged, u.capped, u.kicks, u.maxDv, u.lastDv, u.bySrc[0], u.bySrc[1], u.bySrc[2],
				u.bySrc[4], u.watches, u.watchBack, u.tiltMaxEver);
		}
	}
}

void UnstopOnLoad()
{
	sp::fo4::look::UnstopOnLoad();
	g_un5.budget.Reset();
	g_un5.merge.Reset();
	g_un5.watching = false;
}
