// OWNER: R6 (round 6 step 2, ARCH S0 logging + TABS_LOOK_PLAN rank 0). Included at the end of runtime.cpp's anonymous
// namespace (after runtime_bookmark.inc: it shares g / g_lock / g_bm). Everything here runs on the F4SE task pump
// (GameTick) under the tick lock, except RequestDumpImpl (any thread: it only sets a flag).
//
//   - the swing CSV (core/events.h SwingLog + CsvFile): [Tabs] log.swingCsv 1, log.swingCsvMB 2
//   - SPN|TELE| event lines (blink, putback (brain_glue), jump, warp, hitch): [Tabs] log.events 1
//   - the black box dumps (core/blackbox.h via brain_glue, written by fo4/bb_io): the player's death edge, the bookmark
//     key (bb.onBookmark 1), [Debug] BlackBoxDump > 0 (edge), a look feature switched off by its crash shield;
//     [Tabs] bb.on, bb.seconds 30, bb.maxMB 2, bb.maxDumps 5
//   - fix d (look.jumpTest): the continuity test at the top of GameTick uses R6JumpTest below.

struct R6State
{
	sp::events::SwingLog    swings;
	sp::events::CsvFile     csv;
	bool                    csvOn{ false }, csvTried{ false };
	sp::events::RateLimiter tele{ 4 };
	double                  cfgAt{ -1e9 };
	bool                    events{ false };
	// black box dumps
	bool                    deathArmed{ true };
	double                  hpAt{ -1e9 };
	std::uint64_t           bmPressedSeen{ 0 };
	bool                    bmSeenInit{ false };
	double                  dbgPrev{ 0 };
	std::atomic<bool>       dumpReq{ false };
	std::mutex              dumpWhyLock;
	std::string             dumpWhy;
	double                  lastDumpAt{ -1e9 };
	std::uint64_t           dumps{ 0 }, dumpFails{ 0 }, dumpSkipped{ 0 };
	// fix d
	std::uint64_t           jumpEvents{ 0 }, hitches{ 0 }, hitchEvents{ 0 }, menuGaps{ 0 };
	std::uint64_t           lastMenuPumps{ 0 };
	double                  lastCheckWall{ -1 };
	double                  statAt{ -1e9 };
};
R6State g_r6;

double R6Wall()
{
	return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-6;
}

void R6Config(double a_now)
{
	if (a_now - g_r6.cfgAt < 1.0 && a_now >= g_r6.cfgAt) return;
	g_r6.cfgAt = a_now;
	g_r6.events = Tunable("log.events", 0.0) != 0.0;
	g_r6.tele.SetPerSecond(static_cast<int>(std::clamp(Tunable("log.eventsPerSec", 4.0), 1.0, 60.0)));
	const bool want = Tunable("log.swingCsv", 0.0) != 0.0;
	if (want && !g_r6.csvOn && !g_r6.csvTried) {
		g_r6.csvTried = true;
		// B2 codex review (BB-01): path::string() throws on a Documents path the ANSI code page cannot spell (a non-ASCII user
		// name); that exception left the F4SE task (P5) - a crash. The swing CSV is a diagnostic: it is just off then.
		try {
			const auto dir = sp::fo4::bookmark_io::Folder();
			if (!dir.empty()) {
				const auto path = (dir.parent_path() / "SuperPeasantNative.swings.csv").string();
				const auto mb = std::clamp(Tunable("log.swingCsvMB", 2.0), 0.1, 64.0);
				g_r6.csvOn = g_r6.csv.Open(path, sp::events::kSwingCsvHeader, static_cast<std::uint64_t>(mb * 1048576.0));
				REX::INFO("SPN|EVT|swing csv {} ({})", g_r6.csvOn ? "on" : "could not be opened", path);
			}
		} catch (...) {
			g_r6.csv.Close();
			g_r6.csvOn = false;
			REX::WARN("SPN|EVT|swing csv off: its file path could not be set up (path encoding / I/O exception)");
		}
	}
	if (!want && g_r6.csvOn) {
		g_r6.csv.Close();
		g_r6.csvOn = false;
		g_r6.csvTried = false;
	}
}

// ---------------------------------------------------------------- swing CSV
void R6SwingStart(int a_fist, RE::Actor* a_t, RE::Actor* a_me)
{
	if (!g_r6.csvOn) return;
	double d = -1;
	if (a_t && a_me) {
		const Vec3 aim = AimPoint(a_t, a_me, SizeOf(a_t));
		Vec3       from = sp::fo4::ToVec(ChestOf(a_me));
		if (g.body.IsReady()) {
			const Vec3 tor = UnityToFO4Pos(g.body.Part(sp::body::kTorso).pos, g.body.Upm());
			if (IsFinite(tor)) from = tor;
		}
		d = Length(aim - from);
	}
	g_r6.swings.Start(a_fist, brain_glue::Now(), a_t ? a_t->GetFormID() : 0u, d, g.visible);
}
void R6SwingMiss(int a_fist, std::string_view a_reason)
{
	if (g_r6.csvOn) g_r6.swings.NoteMiss(a_fist, a_reason);
}
void R6SwingRefused(int a_fist, int a_fists, const char* a_why)
{
	if (!g_r6.csvOn) return;
	const std::string code = sp::events::RefusedCode(a_why ? a_why : "veto");
	g_r6.swings.NoteMiss(a_fist, code);
	for (int f = 0; f < 2; ++f)
		if (a_fists & (1 << f)) g_r6.swings.NoteMiss(f, code);
}
void R6SwingHit(int a_fist, int a_fists, std::uint32_t a_id, double a_k)
{
	if (!g_r6.csvOn) return;
	g_r6.swings.NoteHit(a_fist, a_id, a_k);
	for (int f = 0; f < 2; ++f)
		if (a_fists & (1 << f)) g_r6.swings.NoteHit(f, a_id, a_k);
}

// ---------------------------------------------------------------- TELE lines
void R6Tele(const char* a_kind, const char* a_why, const Vec3& a_from, const Vec3& a_to, std::uint32_t a_target, int a_seen, bool a_fromSameSpace)
{
	sp::warden_glue::ProbeRelocation(a_kind, a_from, a_to, a_fromSameSpace);  // R6 step 3: probe P-R6-4 (does the player see its ends?)
	if (!g_r6.events) return;
	int supp = 0;
	if (!g_r6.tele.Allow(sp::events::KindId(a_kind), a_target, R6Wall(), supp)) return;
	const double from[3] = { a_from.x, a_from.y, a_from.z }, to[3] = { a_to.x, a_to.y, a_to.z };
	// (R6 step 10: hp= is the native player health now - the brain's share was -1 whenever Papyrus gave none)
	const double hpNative = sp::runtime::PlayerHealthNative();
	std::string  line = sp::events::TeleLine(a_kind, brain_glue::Now(), a_why ? a_why : "-", from, to, a_seen, hpNative >= 0 ? hpNative : brain_glue::PlayerHealthShare(), a_target);
	if (supp) line += std::format(" supp={}", supp);
	REX::INFO("SPN|TELE|{}", line);
}

// ---------------------------------------------------------------- fix d: the continuity test
sp::look::SpaceKey R6SpaceOf(RE::Actor* a_me)
{
	sp::look::SpaceKey k;
	auto*              c = a_me ? a_me->parentCell : nullptr;
	if (!c) return k;
	k.valid = true;
	k.cell = c->GetFormID();
	k.interior = c->IsInterior();
	k.worldspace = (!k.interior && c->worldSpace) ? c->worldSpace->GetFormID() : 0u;
	return k;
}

// a_pos = his actor position now; returns the event kind (kNone = no continuity event). Updates the hitch counters.
sp::look::JumpWhy R6JumpTest(RE::Actor* a_me, const Vec3& a_pos, double a_legacyDist)
{
	Vec3 cmd{};
	{
		std::scoped_lock l{ g_lock };
		cmd = g.cmdDisp;
		g.cmdDisp = {};
	}
	const auto sk = R6SpaceOf(a_me);
	const bool flagged = sp::fo4::look::TakeRelocation();
	const bool fixOn = Tunable("look.jumpTest", 0.0) != 0.0 && !CarryActive();  // carry moves him with the player: the old rule
	sp::look::JumpIn ji;
	ji.dP = a_pos - g.telePrev;
	ji.cmdDisp = cmd;
	ji.flagged = flagged;
	ji.spaceChanged = sp::look::SpaceChanged(g.teleSpace, sk);
	ji.legacyDist = a_legacyDist;
	ji.tolerance = std::clamp(Tunable("look.jumpTol", 400.0), 50.0, 4000.0);
	ji.fixOn = fixOn;
	const auto why = g.haveTelePrev ? sp::look::ContinuityEvent(ji) : sp::look::JumpWhy::kNone;
	g.teleSpace = sk;
	// hitches (real time between two checks > 0.2 s): the gate counts false events against these
	const double now = R6Wall();
	const double wdt = g_r6.lastCheckWall > 0 ? now - g_r6.lastCheckWall : 0.0;
	g_r6.lastCheckWall = now;
	// a pause menu between two checks is no hitch (the world did not run)
	const auto mp = sp::fo4::look::MenuPumps();
	const bool menuGap = mp != g_r6.lastMenuPumps;
	g_r6.lastMenuPumps = mp;
	if (wdt > 0.2 && menuGap) ++g_r6.menuGaps;
	if (wdt > 0.2 && g.haveTelePrev && !menuGap) {
		++g_r6.hitches;
		if (why != sp::look::JumpWhy::kNone) ++g_r6.hitchEvents;
		if (g_r6.events)
			REX::INFO("SPN|TELE|hitch t={:.3f} dt={:.3f} moved={:.0f} cmd={:.0f} event={} why={} fix={} n={}", brain_glue::Now(), wdt, Length(ji.dP), Length(cmd),
				why != sp::look::JumpWhy::kNone ? 1 : 0, sp::look::JumpWhyName(why), fixOn ? 1 : 0, g_r6.hitches);
	}
	if (why != sp::look::JumpWhy::kNone) {
		++g_r6.jumpEvents;
		R6Tele("jump", sp::look::JumpWhyName(why), g.telePrev, a_pos, 0, -1, !ji.spaceChanged);  // s3 fix lane 3: P-R6-4 skips a take-off in another space
	}
	return why;
}
// our own relocation (blink): the next test starts from here
void R6TeleBase(const Vec3& a_pos)
{
	// (the commanded displacement since the last check stays: it is small, and the next check zeroes it; no g_lock here:
	// DoBlink may run inside a g_lock scope)
	g.telePrev = a_pos;
	g.haveTelePrev = true;
}

// ---------------------------------------------------------------- black box dumps
void R6Dump(const char* a_why, bool a_force)
{
	const double now = R6Wall();
	if (!a_force && now - g_r6.lastDumpAt < 5.0 && now >= g_r6.lastDumpAt) {
		++g_r6.dumpSkipped;
		return;
	}
	auto s = brain_glue::BbSnapshot(a_why);
	if (s.Empty()) {
		++g_r6.dumpSkipped;
		REX::INFO("SPN|BB|dump skipped ({}): the black box is empty (bb.on 0, or the brain has not run)", a_why ? a_why : "?");
		return;
	}
	const double mb = std::clamp(Tunable("bb.maxMB", 2.0), 0.25, 16.0);
	const int    keep = static_cast<int>(std::clamp(Tunable("bb.maxDumps", 5.0), 1.0, 50.0));
	if (!sp::fo4::bb_io::Write(std::move(s), mb, keep)) {
		++g_r6.dumpSkipped;
		REX::INFO("SPN|BB|dump skipped ({}): a dump is still being written", a_why ? a_why : "?");
		return;
	}
	g_r6.lastDumpAt = now;
	REX::INFO("SPN|BB|dump requested ({}) {}", a_why ? a_why : "?", brain_glue::BbDescribe());
}

void RequestDumpImpl(const char* a_why)
{
	{
		std::scoped_lock l{ g_r6.dumpWhyLock };
		g_r6.dumpWhy = a_why ? a_why : "request";
	}
	g_r6.dumpReq.store(true, std::memory_order_release);
}

// GameTick, every frame at the end (any mode)
void R6Frame(RE::Actor* a_me)
{
	const double now = R6Wall();
	R6Config(now);
	// swing CSV: close the rows whose window ended; write them
	if (g_r6.csvOn) {
		const bool can[2] = { g.swingsT.CanDeal(0), g.swingsT.CanDeal(1) };
		g_r6.swings.Tick(can);
		const auto rows = g_r6.swings.Take();
		if (!rows.empty()) g_r6.csv.Append(rows);
		static double s_flushAt = 0;
		if (now - s_flushAt >= 2.0 || now < s_flushAt) {
			s_flushAt = now;
			g_r6.csv.Flush();
		}
	}
	// the player's death edge (10 times a second): one dump per death
	if (now - g_r6.hpAt >= 0.1 || now < g_r6.hpAt) {
		g_r6.hpAt = now;
		if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
			auto* avs = RE::ActorValue::GetSingleton();
			float cur = 1, mx = 1;
			const bool hpOk = avs && avs->health && SehPlayerHp(pl, avs->health, &cur, &mx);
			const bool dead = (hpOk && cur <= 0.0f) || pl->IsDead(false);
			if (dead && g_r6.deathArmed) {
				g_r6.deathArmed = false;
				REX::WARN("SPN|BB|player death edge (hp {:.0f}/{:.0f}): black box dump", cur, mx);
				R6Dump("player death", true);
			} else if (!dead && hpOk && mx > 0 && cur / mx > 0.05f) {
				g_r6.deathArmed = true;
			}
		}
	}
	// the bookmark key also dumps the black box
	if (!g_r6.bmSeenInit) {
		g_r6.bmSeenInit = true;
		g_r6.bmPressedSeen = g_bm.pressed;
	}
	if (g_bm.pressed != g_r6.bmPressedSeen) {
		g_r6.bmPressedSeen = g_bm.pressed;
		if (Tunable("bb.onBookmark", 1.0) != 0.0) R6Dump("bookmark", true);
	}
	// [Debug] BlackBoxDump (harness): a rising value dumps
	{
		const double v = Tunable("Debug.blackboxdump", 0.0);
		if (v > 0 && v != g_r6.dbgPrev) R6Dump("debug", true);
		g_r6.dbgPrev = v;
	}
	if (g_r6.dumpReq.exchange(false, std::memory_order_acq_rel)) {
		std::string why;
		{
			std::scoped_lock l{ g_r6.dumpWhyLock };
			why = g_r6.dumpWhy;
		}
		R6Dump(why.c_str(), false);
		g_bm.testNow = true;  // and a bookmark of the moment (the crash-shield rule: "a bookmark-style dump")
	}
	{
		sp::fo4::bb_io::Result r;
		if (sp::fo4::bb_io::Poll(r)) {
			if (r.ok) {
				++g_r6.dumps;
				REX::INFO("SPN|BB|dump ok path={} bytes={} ms={:.0f} frames={} replay={} mismatches={} tapeMisses={} wardenFrames={} wardenMismatches={} wardenNoKey={} why={} n={}", r.path,
					r.bytes, r.ms, r.frames, r.identical ? "identical" : (r.frames ? "DIFFERENT" : "none"), r.mismatches, r.tapeMisses, r.wFrames, r.wMismatches, r.wNoKey, r.why,
					g_r6.dumps);  // R6 step 4: + the Warden's frames
			} else {
				++g_r6.dumpFails;
				REX::WARN("SPN|BB|dump failed: {} why={} n={}", r.path, r.why, g_r6.dumpFails);
			}
		}
	}
	// one stat line a minute
	if ((g_r6.events || g_r6.csvOn) && (now - g_r6.statAt >= 60.0 || now < g_r6.statAt)) {
		g_r6.statAt = now;
		REX::INFO("SPN|EVT|stat {} swings={} swingHits={} csvBytes={} csvRot={} teleEvents={} teleSupp={} jumps={} hitches={} hitchEvents={} menuGaps={} dumps={} dumpFails={} dumpSkipped={} | {}",
			brain_glue::BbDescribe(), g_r6.swings.Rows(), g_r6.swings.Hits(), g_r6.csv.Bytes(), g_r6.csv.Rotations(), g_r6.tele.Emitted(), g_r6.tele.Suppressed(), g_r6.jumpEvents,
			g_r6.hitches, g_r6.hitchEvents, g_r6.menuGaps, g_r6.dumps, g_r6.dumpFails, g_r6.dumpSkipped, sp::fo4::look::Describe());
	}
	(void)a_me;
}

// unload / load: open swing rows are closed
void R6OnLoad()
{
	g_r6.swings.Flush();
	if (g_r6.csvOn) {
		g_r6.csv.Append(g_r6.swings.Take());
		g_r6.csv.Flush();
	}
	g_r6.lastCheckWall = -1;
	g_r6.deathArmed = true;
}
