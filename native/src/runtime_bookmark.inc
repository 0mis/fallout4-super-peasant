// OWNER: R6 BASELINE (new file).  The "that was weird" bookmark key in the runtime (core/bookmark.h, fo4/bookmark_io.h):
// the per-frame record, the candidate table (4 a second), the events, and the press -> capture -> writer thread ->
// "Super Peasant: moment saved". Included at the end of runtime.cpp's anonymous namespace (it shares g / g_lock /
// g_snap). Everything here runs on the F4SE task pump (GameTick) or under the tick lock (load).
//
// [Tabs] keys (shipped ini, "BOOKMARK" block): bookmark.on, bookmark.key, bookmark.keyMod, bookmark.pad, bookmark.padWindow,
// bookmark.seconds, bookmark.debounce, bookmark.notify. TEST ONLY (never in a shipped ini): debug.bookmarkAt = s after the
// first recorded frame (one bookmark), debug.bookmarkNow = 1 (one bookmark at the next 1 s refresh; set back to 0).

struct BmParams
{
	bool          on{ true };
	double        seconds{ 10.0 };
	int           key{ 119 };          // F8
	int           keyMod{ 0 };
	std::uint32_t pad{ 0x8020 };       // View + Y
	double        padWindow{ 0.5 };
	double        debounce{ 2.0 };
	bool          notify{ true };
	double        testAt{ 0 };         // debug.bookmarkAt (TEST)
};

struct BmState
{
	sp::bookmark::Recorder          rec;
	BmParams                        p{};
	double                          refreshAt{ -1e9 };
	double                          candAt{ -1e9 };
	double                          hpAt{ -1e9 };
	float                           hp{ -1.0f };
	bool                            hpBroken{ false };
	std::uint64_t                   frameNo{ 0 };
	int                             carryLast{ 0 };
	double                          firstT{ -1 };
	bool                            testAtDone{ false };
	bool                            testNow{ false };
	bool                            waiting{ false };  // a write runs: Poll when the writer is done
	std::string                     pendingName;
	std::uint64_t                   pressed{ 0 }, saved{ 0 }, failed{ 0 }, ignoredBusy{ 0 };
	// its own cost (the record + the candidate table, not the press): SPN|BOOKMARK|cost every 60 s
	std::uint64_t                   costNs{ 0 }, costMaxNs{ 0 }, costN{ 0 }, costLines{ 0 };
	double                          costAt{ -1 };
	std::vector<sp::bookmark::Cand> candBuf;
};
BmState g_bm;

double BmNow() { return sp::fo4::carry_input::Now(); }

void BmEvent(sp::bookmark::Ev a_k, std::uint32_t a_a, std::uint32_t a_b, std::int32_t a_i, float a_x, const char* a_text)
{
	if (!g_bm.p.on || !g_bm.rec.Configured() || !sp::fo4::threads::InPump()) return;
	const double now = BmNow();
	if (a_k == sp::bookmark::Ev::kVeto) {  // the same refusal of the same actor every frame: once per 0.5 s (keeps the ring for the rest)
		static std::uint32_t s_id = 0;
		static double        s_at = -1e9;
		static char          s_text[72]{};
		const char*          txt = a_text ? a_text : "";
		if (a_a == s_id && now - s_at < 0.5 && now >= s_at && std::strncmp(s_text, txt, sizeof(s_text) - 1) == 0) return;
		s_id = a_a;
		s_at = now;
		strncpy_s(s_text, txt, _TRUNCATE);
	}
	g_bm.rec.Push(sp::bookmark::MakeEvent(now, g_bm.frameNo, a_k, a_a, a_b, a_i, a_x, a_text));
}

void BmBeginFrame(std::uint64_t a_frameNo) { g_bm.frameNo = a_frameNo; }

void BmOnLoad()  // kPreLoadGame / kNewGame (tick lock held): the old session's frames and ids are not this game's
{
	g_bm.rec.Clear();
	g_bm.carryLast = 0;
	g_bm.hp = -1.0f;
	g_bm.hpAt = -1e9;
	g_bm.candAt = -1e9;
}

bool SehPlayerHp(RE::PlayerCharacter* a_pl, const RE::ActorValueInfo* a_hv, float* a_cur, float* a_max) noexcept
{
	__try {
		*a_cur = a_pl->GetActorValue(*a_hv);
		*a_max = a_pl->GetPermanentActorValue(*a_hv);
		return true;
	} __except (1) {
		return false;
	}
}

std::string BmPathText(const std::filesystem::path& a_p)
{
	try {
		const auto u8 = a_p.u8string();
		return std::string(u8.begin(), u8.end());
	} catch (...) {
		return "?";
	}
}

void BmRefresh(double a_now)
{
	if (a_now - g_bm.refreshAt < 1.0 && a_now >= g_bm.refreshAt) return;
	g_bm.refreshAt = a_now;
	auto& p = g_bm.p;
	p.on = Tunable("bookmark.on", 1.0) != 0;
	p.seconds = std::clamp(Tunable("bookmark.seconds", 10.0), 2.0, 30.0);
	p.key = static_cast<int>(std::clamp(Tunable("bookmark.key", 119.0), 0.0, 254.0));
	p.keyMod = static_cast<int>(std::clamp(Tunable("bookmark.keyMod", 0.0), 0.0, 254.0));
	p.pad = static_cast<std::uint32_t>(std::clamp(Tunable("bookmark.pad", 32800.0), 0.0, 65535.0));
	p.padWindow = Tunable("bookmark.padWindow", 0.5);
	p.debounce = Tunable("bookmark.debounce", 2.0);
	p.notify = Tunable("bookmark.notify", 1.0) != 0;
	p.testAt = Tunable("debug.bookmarkAt", 0.0);  // TEST ONLY
	if (Tunable("debug.bookmarkNow", 0.0) != 0) {  // TEST ONLY (SetTunable from a test runner)
		sp::cfg::SetTunable("debug.bookmarkNow", 0.0);
		g_bm.testNow = true;
	}
	sp::fo4::carry_input::SetBookmarkKeys(p.on ? p.key : 0, p.keyMod, p.on ? p.pad : 0u, p.padWindow, p.debounce);
	if (p.on && !g_bm.rec.Configured()) {
		g_bm.rec.Configure(4096, 4096, 8192);
		g_bm.candBuf.reserve(256);
		REX::INFO("SPN|BOOKMARK|ready: keyboard {}{}, controller {} (within {:.2f} s), the last {:.0f} s are kept; files go to {}", p.keyMod ? sp::bookmark::KeyName(p.keyMod) + "+" : std::string(),
			p.key ? sp::bookmark::KeyName(p.key) : std::string("off"), sp::bookmark::PadName(p.pad), p.padWindow, p.seconds, BmPathText(sp::fo4::bookmark_io::Folder()));
	}
}

void BmFrame(std::uint64_t a_frameNo, RE::Actor* me, float a_dt, int a_mode, const char* a_down, double a_costMs, double a_now)
{
	namespace B = sp::bookmark;
	B::Frame f;
	f.t = a_now;
	f.frame = a_frameNo;
	f.dt = a_dt;
	f.costMs = static_cast<float>(a_costMs);
	f.costP6Ms = static_cast<float>(static_cast<double>(g_lastP6Ns.load(std::memory_order_relaxed)) * 1e-6);
	f.mode = static_cast<std::int8_t>(std::clamp(a_mode, -1, 9));
	f.down = a_down;
	std::uint32_t fl = 0;
	if (g.brainOn) fl |= B::kFBrainOn;
	if (a_down) fl |= B::kFDown;
	if (g.indoors) fl |= B::kFIndoors;
	if (g.interiorCell) fl |= B::kFInterior;
	if (g.lowCeiling) fl |= B::kFLowCeiling;
	if (g.brainOn) {
		const auto& o = g.bout;
		f.target = o.target;
		f.tier = static_cast<std::uint8_t>(o.tier);
		f.cls = static_cast<std::uint8_t>(o.cls);
		f.move = static_cast<std::uint8_t>(o.move);
		f.hunt = static_cast<std::uint8_t>(std::clamp(static_cast<int>(o.hunt), 0, 255));
		f.reachStage = static_cast<std::int8_t>(std::clamp(o.reachStage, -1, 100));
		f.planKind = static_cast<std::int8_t>(std::clamp(o.planKind, -100, 100));
		f.giveUp = static_cast<std::uint8_t>(o.giveUp);
		f.giveUpId = o.giveUpId;
		f.blinkWhy = static_cast<std::uint8_t>(std::clamp(o.blinkWhy, 0, 255));
		f.navIdx = static_cast<std::int16_t>(std::clamp(o.navIdx, -32768, 32767));
		f.navN = static_cast<std::int16_t>(std::clamp(o.navN, -32768, 32767));
		f.stageWhy = o.stageWhy;
		f.dist = static_cast<float>(o.dist);
		f.navRemain = static_cast<float>(o.navRemain);
		f.vDes = static_cast<float>(Length(o.vDes));
		f.vMax = static_cast<float>(o.vMax);
		f.rays = static_cast<std::uint16_t>(std::clamp(o.rays, 0, 65535));
		if (o.visible) fl |= B::kFVisible;
		if (o.mayAttack) fl |= B::kFMayAttack;
		if (o.directClear) fl |= B::kFDirectClear;
		if (o.blink) fl |= B::kFBlink;
		if (o.putBack) fl |= B::kFPutBack;
		if (o.targetChanged) fl |= B::kFTargetChanged;
		if (o.pin) fl |= B::kFPin;
		if (o.climb) fl |= B::kFClimb;
		if (o.chaseBoost) fl |= B::kFChaseBoost;
		if (o.blinkAttack) fl |= B::kFBlinkAttack;
	} else {
		f.target = CurTarget();
		if (g.visible) fl |= B::kFVisible;
	}
	f.cands = static_cast<std::uint16_t>(std::min<std::size_t>(brain_glue::Cands().size(), 65535));
	if (g.followActive) fl |= B::kFFollow;
	if (g.folSimBlocked) fl |= B::kFFollowBlocked;
	if (g.tabsChain) fl |= B::kFTabsChain;
	if (g.huntMech) fl |= B::kFHuntMech;
	if (g.body.IsReady()) fl |= B::kFBodyReady;
	if (me && me->parentCell) fl |= B::kFLoaded;
	if (CarryNoFight()) fl |= B::kFCarryNoFight | B::kFEscort;
	if (g.blockedLatch) fl |= B::kFMoverBlocked;
	f.blockedFor = static_cast<float>(g.blockedFor);
	f.contactBlockedFor = static_cast<float>(g.ctLineBlkT);
	f.aroundBlockedFor = static_cast<float>(g.ctBlockedT);
	f.inSolidFor = static_cast<float>(g.inSolidFor);
	f.nearNoHitFor = static_cast<float>(g.ctNearT);
	f.headroom = static_cast<float>(g.headroom);
	f.wingsDist = static_cast<float>(g.wingsDist);
	f.swings = static_cast<std::uint32_t>(g.swings);
	f.hits = static_cast<std::uint32_t>(g.hits);
	f.flings = static_cast<std::uint16_t>(std::min<std::size_t>(g.flings.size(), 65535));
	{
		std::scoped_lock l{ g_snapLock };
		f.flightMode = static_cast<std::uint8_t>(std::clamp(static_cast<int>(g_snap.flightMode), 0, 255));
		f.bodyState = static_cast<std::uint8_t>(std::clamp(static_cast<int>(g_snap.body.state), 0, 255));
		f.carryState = static_cast<std::uint8_t>(std::clamp(g_snap.carryState, 0, 255));
		f.carryWhy = static_cast<std::int8_t>(std::clamp(g_snap.carryWhy, -128, 127));
		f.sight = static_cast<float>(g_snap.sight);
		if (g_snap.escortDist >= 0) fl |= B::kFEscort;
		if (fl & B::kFBodyReady) {
			f.sim[0] = static_cast<float>(g_snap.body.hip.x);
			f.sim[1] = static_cast<float>(g_snap.body.hip.y);
			f.sim[2] = static_cast<float>(g_snap.body.hip.z);
		}
	}
	if (me) {
		const auto p = me->GetPosition();
		f.me[0] = p.x;
		f.me[1] = p.y;
		f.me[2] = p.z;
	}
	f.meVel[0] = static_cast<float>(g.meVel.x);
	f.meVel[1] = static_cast<float>(g.meVel.y);
	f.meVel[2] = static_cast<float>(g.meVel.z);
	if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
		const auto pp = pl->GetPosition();
		f.player[0] = pp.x;
		f.player[1] = pp.y;
		f.player[2] = pp.z;
		// the player's health share, 10 times a second (two actor-value reads)
		if (!g_bm.hpBroken && (a_now - g_bm.hpAt >= 0.1 || a_now < g_bm.hpAt)) {
			g_bm.hpAt = a_now;
			auto* avs = RE::ActorValue::GetSingleton();
			float cur = 0, mx = 0;
			if (avs && avs->health) {
				if (SehPlayerHp(pl, avs->health, &cur, &mx)) g_bm.hp = (mx > 0 && std::isfinite(cur) && std::isfinite(mx)) ? std::clamp(cur / mx, 0.0f, 1.0f) : -1.0f;
				else g_bm.hpBroken = true;
			}
		}
	}
	f.playerHp = g_bm.hp;
	f.flags = fl;
	g_bm.rec.Push(f);
	if (f.carryState != g_bm.carryLast) {  // carry edges (the carry code has its own SPN|CARRY lines)
		BmEvent(sp::bookmark::Ev::kCarry, 0, static_cast<std::uint32_t>(g_bm.carryLast), f.carryState, 0.0f, sp::carry::StateName(static_cast<sp::carry::State>(f.carryState)));
		g_bm.carryLast = f.carryState;
	}
}

void BmCands(double a_now)
{
	if (!g.brainOn || a_now - g_bm.candAt < 0.25) return;
	g_bm.candAt = a_now;
	g_bm.candBuf.clear();
	Vec3 pl{};
	if (auto* p = RE::PlayerCharacter::GetSingleton()) pl = sp::fo4::ToVec(p->GetPosition());
	brain_glue::BookmarkCands(a_now, pl, g_bm.candBuf);
	int n = 0;
	for (const auto& r : g_bm.candBuf) {
		if (++n > 96) break;  // a crowd: the first 96 (the scan keeps the nearest first)
		g_bm.rec.Push(r);
	}
}

void BmPress(int a_how, double a_now, std::uint64_t a_frameNo, RE::Actor* me, int a_mode)
{
	namespace B = sp::bookmark;
	++g_bm.pressed;
	BmEvent(B::Ev::kPress, 0, 0, a_how, 0.0f, a_how == 1 ? "keyboard" : (a_how == 2 ? "controller" : "test hook"));
	if (g_bm.waiting || sp::fo4::bookmark_io::Busy()) {
		++g_bm.ignoredBusy;
		REX::INFO("SPN|BOOKMARK|pressed while the last one is still being written: skipped (this moment is in the ring for the next press) n={}", g_bm.ignoredBusy);
		return;
	}
	const auto& p = g_bm.p;
	B::Capture  cap;
	g_bm.rec.Grab(a_now + 1e-6, p.seconds, cap);
	const auto sys = std::chrono::system_clock::now();
	const auto tt = std::chrono::system_clock::to_time_t(sys);
	std::tm    tm{};
	localtime_s(&tm, &tt);
	const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count() % 1000);
	cap.tPress = a_now;
	cap.when = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
	cap.how = a_how == 1 ? std::format("keyboard {}{}", p.keyMod ? B::KeyName(p.keyMod) + "+" : std::string(), B::KeyName(p.key)) :
	          a_how == 2 ? std::format("controller {}", B::PadName(p.pad)) :
	                       std::string("test hook (debug.bookmarkAt / debug.bookmarkNow)");
	cap.header.push_back(std::format("plugin     SuperPeasantNative {} (built {} {}), F4SE {}, game {}", F4SE::GetPluginVersion().string(), __DATE__, __TIME__,
		F4SE::GetF4SEVersion().string(), REX::FModule::GetExecutingModule().GetFileVersion().string()));
	{
		std::string where = "-";
		if (me && me->parentCell) where = std::format("cell {:08X} ({})", me->parentCell->GetFormID(), me->parentCell->IsInterior() ? "interior" : "exterior");
		cap.header.push_back(std::format("him        {:08X}, mode {}, frame {}, {}", me ? me->GetFormID() : 0u, a_mode, a_frameNo, where));
	}
	cap.header.push_back("state      " + DebugState());
	cap.header.push_back("brain     " + brain_glue::StatLine());
	cap.header.push_back("input      " + sp::fo4::carry_input::Describe());
	cap.header.push_back(std::format("bookmark   #{} this session (saved {}, failed {}, skipped while writing {}), ring frames {} events {}", g_bm.pressed, g_bm.saved, g_bm.failed,
		g_bm.ignoredBusy, g_bm.rec.FramesKept(), g_bm.rec.EventsPushed()));
	sp::fo4::bookmark_io::LogTail(p.seconds + 0.5, cap.log);
	const std::string name = B::FileName(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
	const auto        nF = cap.frames.size(), nE = cap.events.size(), nC = cap.cands.size(), nL = cap.log.size();
	const std::string how = cap.how;
	if (sp::fo4::bookmark_io::Write(std::move(cap), name)) {
		g_bm.waiting = true;
		g_bm.pendingName = name;
		REX::INFO("SPN|BOOKMARK|pressed ({}) -> {} frames={} events={} cands={} logLines={}", how, name, nF, nE, nC, nL);
	}
}

void BmPoll()
{
	if (!g_bm.waiting || sp::fo4::bookmark_io::Busy()) return;
	sp::fo4::bookmark_io::Result r;
	if (!sp::fo4::bookmark_io::Poll(r)) return;
	g_bm.waiting = false;
	if (r.ok) {
		++g_bm.saved;
		REX::INFO("SPN|BOOKMARK|saved {} ({} bytes, {:.0f} ms on the writer thread) n={}", r.path, r.bytes, r.ms, g_bm.saved);
		if (g_bm.p.notify) sp::papyrus::Notify("Super Peasant: moment saved");
	} else {
		++g_bm.failed;
		REX::WARN("SPN|BOOKMARK|could not save {}: {} n={}", g_bm.pendingName, r.path, g_bm.failed);
		if (g_bm.p.notify) sp::papyrus::Notify("Super Peasant: the moment could not be saved (see SuperPeasantNative.log)");
	}
}

// GameTick, right after Publish (every frame, any mode)
void BookmarkTick(std::uint64_t a_frameNo, RE::Actor* me, float a_dt, int a_mode, const char* a_down, std::chrono::steady_clock::time_point a_t0)
{
	const double now = BmNow();
	BmRefresh(now);
	g_bm.frameNo = a_frameNo;
	if (!g_bm.p.on || !g_bm.rec.Configured()) {
		(void)sp::fo4::carry_input::TakeBookmark();
		g_bm.testNow = false;
		BmPoll();
		return;
	}
	const auto   c0 = std::chrono::steady_clock::now();
	const double costMs = std::chrono::duration<double, std::milli>(c0 - a_t0).count();
	BmFrame(a_frameNo, me, a_dt, a_mode, a_down, costMs, now);
	BmCands(now);
	{  // its own cost, logged once a minute (the gate checks it stays far below 0.05 ms a frame)
		const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - c0).count());
		g_bm.costNs += ns;
		g_bm.costMaxNs = std::max(g_bm.costMaxNs, ns);
		++g_bm.costN;
		if (g_bm.costAt < 0 || now < g_bm.costAt) g_bm.costAt = now;
		if (now - g_bm.costAt >= 60.0) {
			if (LogDue(g_bm.costLines, 15.0, 10.0))
				REX::INFO("SPN|BOOKMARK|cost avg={:.2f}us max={:.1f}us frames={} kept={} events={} pressed={} saved={}", static_cast<double>(g_bm.costNs) / 1000.0 / static_cast<double>(std::max<std::uint64_t>(1, g_bm.costN)),
					static_cast<double>(g_bm.costMaxNs) / 1000.0, g_bm.costN, g_bm.rec.FramesKept(), g_bm.rec.EventsPushed(), g_bm.pressed, g_bm.saved);
			g_bm.costAt = now;
			g_bm.costNs = g_bm.costMaxNs = g_bm.costN = 0;
		}
	}
	if (g_bm.firstT < 0) g_bm.firstT = now;
	int how = sp::fo4::carry_input::TakeBookmark();
	if (!how && g_bm.testNow) how = 3;
	if (!how && g_bm.p.testAt > 0 && !g_bm.testAtDone && now - g_bm.firstT >= g_bm.p.testAt) {
		g_bm.testAtDone = true;
		how = 3;
	}
	if (how) {
		g_bm.testNow = false;
		BmPress(how, now, a_frameNo, me, a_mode);
	}
	BmPoll();
}
