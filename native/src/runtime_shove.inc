// OWNER: R6 STEP 15 (CORE lane). "Bodies shove bodies + his real physics body" glue (core/shove.h, fo4/shove_glue.h).
// Included in the runtime's anonymous namespace right BEFORE runtime_cage.inc: the Cage's body adapter hands his colliders to
// ShoveNoteShapes every TABS step, and CageAfterAdvance runs ShoveTick at P5 (GameTick -> StepBody, under g_tickLock), after
// Body::Advance and before fo4/contacts::EndFrame (the shove pass uses that frame's engine world and query layer).
// Here the runtime says who is who (the user rule): the player, the scan's bystanders (teammates, companions, Dogmeat, settlers,
// non-hostile / protected actors near him) are FRIENDS by their collision group (never pushed; their corpses / the props at their
// feet are left alone, pushes never go toward them); with look.shoveFoes 1 the kept hostile foes the hit rules allow (FoeVeto,
// not essential / protected, the scan's live veto) are FOES; his target, the punch victims and every body a fling or the height
// watch owns are SKIPPED (step 13 launches them). The reactions (the equal and opposite impulses) go back into his TABS parts.

namespace shove15
{
	struct Prev
	{
		int  part{ -1 };
		Vec3 a{}, b{};
	};
	struct GroupMemo
	{
		std::uint32_t id{ 0 }, group{ 0 };
		double        at{ -1e9 };
		bool          allowed{ false };
		double        allowedAt{ -1e9 };
	};
	struct St
	{
		std::vector<sp::shove::PartShape> shapes;  // this frame's colliders (start = the end of the last frame)
		bool                              fresh{ false };
		std::vector<Prev>                 prev;     // the end of the last frame
		std::vector<sp::shove::PartShape> last;     // the last stepped frame's colliders (the pushers follow them on frames without a step)
		double                            lastFreshT{ -1e9 };
		double                            prevT{ -1e9 };
		double                            lastT{ -1e9 };
		bool                              cont{ false };  // a continuity event: no sweep across it
		std::vector<sp::shove::Friend>    friends;
		std::vector<sp::shove::Foe>       foes;
		std::vector<std::uint32_t>        skip;
		std::vector<Vec3>                 skipPos;  // B2 lane 3: the dead / downed skipped actors' chests (shove.victimR)
		std::vector<GroupMemo>            memo;
		double                            capAt{ -1e9 }, capR{ 0 }, capH{ 0 };  // B2 lane 3: his capsule size (re-read once a second)
		Vec3                              capPrev{};
		double                            capPrevT{ -1e9 };                       // B2 lane 3: his capsule last frame (the measured speed)
		std::array<double, sp::body::kPartCount> mass{};
		std::uint64_t                     ticks{ 0 }, noShapes{ 0 }, reactions{ 0 };
		double                            prepareUs{ 0 };
		// finB (shove.idleEvery, cost): the last frame's answer - nothing near and no pusher alive = an idle frame may be skipped
		bool                              lastNear{ true }, lastPushers{ true };
		bool                              skipThis{ false };  // decided in CagePrepare (before Body::Advance), read by ShoveTick
		std::uint64_t                     idleCount{ 0 }, idleSkips{ 0 };
		// B2 fix lane 1: this P5 tick reached the step-15 Tick (else ShoveTickEnd pauses the pushers); the lists' last build (cost)
		bool                              ranTick{ false };
		double                            listsAt{ -1e9 };
		std::uint64_t                     listsBuilt{ 0 };
	};
	St s;
	struct PreparationTimer
	{
		std::chrono::steady_clock::time_point start{ std::chrono::steady_clock::now() };
		~PreparationTimer() { s.prepareUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count(); }
	};

	std::uint32_t GroupCached(RE::Actor* a_a, double a_now)
	{
		if (!a_a) return 0;
		const std::uint32_t id = a_a->GetFormID();
		for (auto& m : s.memo)
			if (m.id == id) {
				// Groups can change with a new controller or ragdoll: resolve them in this pump.
				const std::uint32_t g2 = sp::fo4::shove::GroupOf(a_a);
				// B2 lane 3 (shove.victimR > 0): a dead / ragdolled actor's group read can come back 0 - keep the last real one (its corpse
				// bones still carry it), so a victim or a downed friend is never "nobody's" for path A
				if (g2 != 0 || (a_a->lifeState == 0 && a_a->knockState == 0)) m.group = g2;
				m.at = a_now;
				return m.group;
			}
		if (s.memo.size() > 96) s.memo.erase(s.memo.begin(), s.memo.begin() + 32);
		s.memo.push_back({ id, sp::fo4::shove::GroupOf(a_a), a_now });
		return s.memo.back().group;
	}
	GroupMemo* Memo(std::uint32_t a_id)
	{
		for (auto& m : s.memo)
			if (m.id == a_id) return &m;
		return nullptr;
	}
	bool DownOf(const RE::Actor* a_a) { return !a_a || a_a->lifeState != 0 || a_a->knockState != 0; }

	void BuildLists(RE::Actor* a_me, double a_now, bool a_foes)
	{
		s.friends.clear();
		s.foes.clear();
		s.skip.clear();
		s.skipPos.clear();
		const bool wantPos = sp::fo4::shove::Keys().victimR > 0;
		// friends: the player first, then every bystander the scan keeps (teammates, companions, Dogmeat, settlers, non-hostile,
		// protected / crime / scene actors near him)
		if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
			sp::shove::Friend f;
			f.id = pl->GetFormID();
			f.group = GroupCached(pl, a_now);
			f.pos = sp::fo4::ToVec(pl->GetPosition()) + Vec3{ 0, 0, 100 };
			f.player = true;
			f.down = DownOf(pl);  // the player can be down too (corpse-pusher quiet rule)
			if (f.down && pl->Get3D()) {
				const Vec3 chest = sp::fo4::ToVec(sp::fo4::GetChestPos(pl));
				if (IsFinite(chest)) f.pos = chest;
			}
			s.friends.push_back(f);
		}
		for (const auto& b : sp::fo4::scan::Bystanders()) {
			if (b.why == 0) continue;  // the player (above)
			RE::Actor* a = ActorFromID(b.id);
			if (!a || a == a_me) continue;
			sp::shove::Friend f;
			f.id = b.id;
			f.group = GroupCached(a, a_now);
			f.pos = a->Get3D() ? sp::fo4::ToVec(a->GetPosition()) + Vec3{ 0, 0, 100 } : b.pos;
			f.down = DownOf(a);
			if (f.down && a->Get3D()) {
				const Vec3 chest = sp::fo4::ToVec(sp::fo4::GetChestPos(a));
				if (IsFinite(chest)) f.pos = chest;
			}
			s.friends.push_back(f);
		}
		// skipped: his target, the punch victims, the bodies a fling / the height watch owns
		auto skipId = [&](std::uint32_t a_id) {
			if (!a_id) return;
			if (RE::Actor* a = ActorFromID(a_id)) {
				const std::uint32_t gr = GroupCached(a, a_now);
				if (gr && std::find(s.skip.begin(), s.skip.end(), gr) == s.skip.end()) s.skip.push_back(gr);
				// B2 lane 3: where a dead / downed one is (its corpse bones are its own, whatever group they report); living ones' bodies
				// are actor layers path A never pushes - no bone lookup for them
				if (wantPos && DownOf(a) && a->Get3D()) {
					const Vec3 c = sp::fo4::ToVec(sp::fo4::GetChestPos(a));
					if (IsFinite(c)) s.skipPos.push_back(c);
				}
			}
		};
		skipId(CurTarget());
		for (const auto v : g.vicId) skipId(v);
		for (const auto& fl : g.flings)
			if (!fl.done) skipId(fl.id);
		for (const auto& h : g.heights) skipId(h.id);
		// foes (look.shoveFoes): recheck current life and protection in the same pump as the writes
		if (a_foes)
			for (const auto& k : sp::fo4::scan::Kept()) {
				if (!k.hostile) continue;
				RE::Actor* a = ActorFromID(k.id);
				if (!a || !Alive(a)) continue;
				const std::uint32_t gr = GroupCached(a, a_now);
				if (!gr) continue;
				GroupMemo* m = Memo(k.id);
				if (m) {
					m->allowed = !FoeVeto(a_me, a) && !IsEssentialOrProtected(a) && !sp::fo4::scan::LiveVeto(a_me, a, brain_glue::Now(), "shove");
					m->allowedAt = a_now;
				}
				s.foes.push_back({ k.id, gr, m ? m->allowed : false });
			}
	}
}

namespace shove15
{
	// Safety positions, down state, membership and current groups are rebuilt for every writing pump.
	// The former listsEvery cache admitted moved/new friends and stale fling geometry.
	void ListsFor(RE::Actor* a_me, double a_now, bool a_foes)
	{
		BuildLists(a_me, a_now, a_foes);
		s.listsAt = a_now;
		++s.listsBuilt;
	}
}

// CagePrepare attributes its exclusive step-15 engine binding here (shared Cage work is separate).
void ShoveNotePreparationUs(double a_us) { if (std::isfinite(a_us) && a_us > 0) shove15::s.prepareUs += a_us; }

// the Cage's body adapter (every TABS step, P5): his colliders of this step
void ShoveNoteShapes(const sp::body::ContactShape* a_s, int a_n, double a_upm)
{
	using namespace shove15;
	if (!a_s || a_n <= 0 || !sp::fo4::shove::Wanted()) return;
	PreparationTimer prepTimer;
	if (!s.fresh || static_cast<int>(s.shapes.size()) != a_n) {
		s.shapes.clear();
		const bool cont = !s.cont && g.dpNow - s.prevT < 0.1 && g.dpNow >= s.prevT;
		for (int i = 0; i < a_n; ++i) {
			auto p = sp::shove::FromContactShape(a_s[i], a_upm);
			if (cont && i < static_cast<int>(s.prev.size()) && s.prev[static_cast<std::size_t>(i)].part == p.part) {
				p.a0 = s.prev[static_cast<std::size_t>(i)].a;
				p.b0 = s.prev[static_cast<std::size_t>(i)].b;
			}
			s.shapes.push_back(p);
		}
		s.cont = false;
		s.fresh = true;
		return;
	}
	for (int i = 0; i < a_n; ++i) {
		auto&      v = s.shapes[static_cast<std::size_t>(i)];
		const auto p = sp::shove::FromContactShape(a_s[i], a_upm);
		if (v.part != p.part) continue;
		v.a1 = p.a1;
		v.b1 = p.b1;
		v.r = p.r;
		v.kind = p.kind;
		v.vel = p.vel;
	}
}

// finB integrator (cost; first game runs: downtown 97 % of the step-15 frames found nothing near, yet the frame - the colliders taken every
// body step, the engine contacts, the lists, the read lock, the cull query - cost ~12 us each): CagePrepare asks this once per body step,
// before Body::Advance. With nothing near last frame and no pusher alive only every shove.idleEvery-th frame runs; the skipped frame
// takes no colliders and asks no engine world. s.prev is kept, so the next swept frame starts where the last one ended and covers the
// skipped one (2 frames x <= 100 u at dash speed stay inside the 250 u cull pad). shove.idleEvery 1 = every frame (the lane's build)
bool ShoveIdleDecide()
{
	using namespace shove15;
	const auto& keys = sp::fo4::shove::Keys();
	s.skipThis = keys.idleEvery > 1 && !s.lastNear && !s.lastPushers && (s.idleCount++ % static_cast<std::uint64_t>(keys.idleEvery)) != 0;
	return s.skipThis;
}

// a continuity event (a jump, a load door, his own relocation): no sweep across it
void ShoveContinuity()
{
	shove15::s.cont = true;
	shove15::s.capPrevT = -1e9;  // B2 lane 3: no capsule speed measured across a jump
}
void ShoveOnLoad()
{
	sp::fo4::shove::OnPreLoad();
	shove15::s = {};
}

// Every safe pump that did not reach the body step services retirement and filter restoration.
// The adapter retains busy cleanup in its original world, independently of feature keys.
void ShoveTickEnd(RE::Actor* a_me)
{
	using namespace shove15;
	if (std::exchange(s.ranTick, false)) return;
	if (!sp::fo4::threads::SafeForEngineWrites()) return;
	sp::fo4::shove::Pause(a_me, "no step");
	s.prepareUs = 0;
	s.fresh = false;
	s.cont = true;
	s.capPrevT = -1e9;
}

// P5, CageAfterAdvance: the step-15 frame (path A impulses + reactions, path B pushers)
void ShoveTick()
{
	using namespace shove15;
	const auto prepStart = std::chrono::steady_clock::now();
	// writes on other bodies (impulses, the pushers) only from the pump (C247: engine mutations of other objects -> P5). If a later
	// step moves Body::Advance / CageAfterAdvance off the pump (step 14 version B: Advance at P6), the step-15 frame is skipped
	// (one line) until it is called from P5 again - it never writes from his Actor::Update.
	if (!sp::fo4::threads::SafeForEngineWrites()) {
		static std::uint64_t s_offPump = 0;
		if (s_offPump++ == 0) REX::WARN("SPN|SHOVE|not on the pump: the step-15 frame is skipped (call ShoveTick from P5)");
		s.fresh = false;
		return;
	}
	const double now = g.dpNow;
	const double dt = (now > s.lastT && now - s.lastT < 0.25) ? now - s.lastT : 1.0 / 60.0;
	s.lastT = now;
	if (!sp::fo4::shove::Wanted()) {
		sp::fo4::shove::FrameIn none;
		none.me = sp::fo4::PeekPeasant();
		none.now = now;
		none.maintenanceOnly = true;
		none.dt = dt;
		none.prepareUs = std::exchange(s.prepareUs, 0.0) + std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - prepStart).count();
		sp::fo4::shove::Tick(none);  // destroys pushers left from when the keys were on; nothing else
		s.ranTick = true;            // B2 fix lane 1
		s.fresh = false;
		return;
	}
	RE::Actor* me = sp::fo4::PeekPeasant();
	const auto& keys = sp::fo4::shove::Keys();
	// finB integrator (cost): an idle frame CagePrepare decided to skip (ShoveIdleDecide) - no colliders were taken, no engine world
	// asked for, nothing runs here (s.prev kept: the next swept frame covers this one)
	if (s.skipThis) {
		s.skipThis = false;
		++s.idleSkips;
		s.fresh = false;
		return;
	}
	ListsFor(me, now, keys.foes);  // B2 fix lane 1 (shove.listsEvery; was BuildLists every frame)
	double total = 0;
	for (int i = 0; i < sp::body::kPartCount; ++i) {
		s.mass[static_cast<std::size_t>(i)] = g.body.IsReady() ? g.body.PartMass(i) : 0.0;
		total += s.mass[static_cast<std::size_t>(i)];
	}
	sp::fo4::shove::FrameIn in;
	in.me = me;
	in.now = now;
	in.dt = dt;
	if (s.fresh) {
		in.shapes = { s.shapes.data(), s.shapes.size() };
		s.last = s.shapes;
		s.lastFreshT = now;
	} else {
		++s.noShapes;
	}
	if (!s.last.empty() && now - s.lastFreshT < 0.25 && now >= s.lastFreshT) {
		in.pusherShapes = { s.last.data(), s.last.size() };
		in.pusherAge = now - s.lastFreshT;
	}
	in.partMass = s.mass.data();
	in.totalMass = total;
	in.friends = { s.friends.data(), s.friends.size() };
	in.foes = { s.foes.data(), s.foes.size() };
	in.skipGroups = { s.skip.data(), s.skip.size() };
	in.skipPos = { s.skipPos.data(), s.skipPos.size() };
	in.bodyReady = g.body.IsReady() && me && !DownOf(me) && me->Get3D() && me->parentCell;
	in.maintenanceOnly = !in.bodyReady;
	in.loading = sp::runtime::IsLoading();
	// B2 lane 3 (shove.capsulePass): his engine capsule - where it is, the velocity the mover commands it, its size (once a second)
	if (keys.pass && me && in.bodyReady) {
		if (now - s.capAt > 1.0 || now < s.capAt) {
			s.capAt = now;
			s.capR = s.capH = 0;
			if (auto* con = sp::fo4::GetController(me)) {
				const double k = (me->refScale > 20 && me->refScale < 500) ? me->refScale / 100.0 : 1.0;
				const double h = static_cast<double>(con->height) * sp::cfg::K::kHavokToGame * k;
				const double r = static_cast<double>(con->radius) * sp::cfg::K::kHavokToGame * k;
				if (std::isfinite(h) && h > 10 && h < 1000) s.capH = h;
				if (std::isfinite(r) && r > 2 && r < 300) s.capR = r;
			}
		}
		in.capPos = sp::fo4::ToVec(me->GetPosition());
		// where his capsule goes next step = the mover's command; but only while it really moves (a stale command of a mover that
		// stopped writing must never read as "fast"): both speed bounds and direction must agree
		const bool   contPos = s.capPrevT > -1e8 && now > s.capPrevT && now - s.capPrevT < 0.25 && IsFinite(s.capPrev);
		const Vec3   meas = contPos ? (in.capPos - s.capPrev) / (now - s.capPrevT) : Vec3{};
		const double vm = Length(meas), vc = Length(g.lastVelCmd);
		const bool agree = contPos && IsFinite(meas) && IsFinite(g.lastVelCmd) && vc > 0 &&
		                   vm >= vc / 3.0 && vm <= vc * 3.0 && vm < 50000.0 &&
		                   sp::math::Dot(meas, g.lastVelCmd) >= 0.8 * vm * vc;
		// A command without observed direction/speed agreement never predicts a forward sweep.
		in.capVel = agree ? g.lastVelCmd : meas;
		s.capPrev = in.capPos;
		s.capPrevT = now;
		in.capR = s.capR;
		in.capH = s.capH;
		in.capOk = contPos && s.capR > 0 && s.capH > 0 && IsFinite(in.capPos) && IsFinite(in.capVel);
	} else {
		s.capPrevT = -1e9;
	}
	in.prepareUs = std::exchange(s.prepareUs, 0.0) + std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - prepStart).count();
	const auto out = sp::fo4::shove::Tick(in);
	const auto postStart = std::chrono::steady_clock::now();
	s.ranTick = true;
	s.lastNear = out.near;
	s.lastPushers = out.pushersAlive;
	// the equal and opposite impulses back into his TABS parts (kg u/s -> kg TABS m/s, Unity frame), applied by the next step
	if (g.body.IsReady()) {
		const double upm = g.body.Upm();
		for (int i = 0; i < out.nReact; ++i) {
			const auto& r = out.react[static_cast<std::size_t>(i)];
			if (r.part < 0 || r.part >= sp::body::kPartCount || !IsFinite(r.J)) continue;
			g.body.AddImpulse(r.part, FO4ToUnityDir(r.J) / upm);
			++s.reactions;
		}
	}
	// the end of this frame = the start of the next one's sweep
	if (s.fresh) {
		s.prev.resize(s.shapes.size());
		for (std::size_t i = 0; i < s.shapes.size(); ++i) s.prev[i] = { s.shapes[i].part, s.shapes[i].a1, s.shapes[i].b1 };
		s.prevT = now;
	}
	s.fresh = false;
	++s.ticks;
	sp::fo4::shove::AddPostCost(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - postStart).count());
}
