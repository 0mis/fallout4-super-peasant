// Integrated shove safety regressions. Included inside s15u after the sphere-world fixtures.
inline void FinishShoveUnits()
{
	const auto mass=Masses();
	std::vector<S::Friend> far{ {1,100,{5000,5000,100},false,true} };
	auto cfg=B2L3Cfg();cfg.friendRecheck=true;cfg.capFinal=true;
	// Incoming motion invalidates an empty scene immediately, even when his geometry is stationary.
	{
		SphereWorld w;w.comIsCentre=true;w.b.push_back({201,4,801,{600,0,60},6,1,{-900,0,0}});
		auto c=cfg;c.pass=false;c.nearEvery=.1;c.slowSkip=true;c.pushers=true;
		std::vector<S::PartShape> sh{Sph(sp::body::kTorso,{0,0,60},{0,0,60},20,{})};
		auto in=B2L3In(10,far,mass);in.shapes=sh;S::Shover sv;
		CHECK(sv.Frame(in,c,w).pushes==0,"FIN-SH01 empty initial box");
		w.Find(201)->c={25,0,60};w.us=0;in.now+=.016;
		const auto o=sv.Frame(in,c,w);
		CHECK(o.pushes==1 && w.Find(201)->v.x>-900,"FIN-SH01 incoming body is detected inside former negative-cache interval");
	}
	// A prop is admitted ahead of the capsule; a friend enters its clearance radius before the delayed contact.
	{
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;w.b.push_back({202,4,802,{100,0,60},6,1});
		auto c=cfg;c.passLook=.1;auto in=B2L3In(20,far,mass);in.capVel={1500,0,0};S::Shover sv;
		sv.Capsule(in,c,w,{});
		CHECK(sv.PassedNow()==1 && w.Find(202)->pushes==0,"FIN-SH02 delayed prop admitted without a premature impulse");
		std::vector<S::Friend> near{{1,100,{100,50,100},false,true}};in.friends=near;in.now+=.016;in.capPos={40,0,0};w.us=0;
		sv.Capsule(in,c,w,{});
		CHECK(w.Find(202)->pushes==0 && sv.St().friendImpulses==0,"FIN-SH02 live near-friend clearance cancels delayed impulse");
	}
	// The first capsule impulse obeys the total-vector cap, including existing lateral motion.
	for(const Vec3 velocity : {Vec3{900,0,0},Vec3{0,900,0}}) {
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;w.b.push_back({203,4,803,{100,0,60},6,1,velocity});
		auto in=B2L3In(30,far,mass);in.capVel={3000,0,0};S::Shover sv;
		const auto o=sv.Capsule(in,cfg,w,{});
		CHECK(o.pushes==1 && Length(w.Find(203)->v)<=cfg.maxDvProp*S::kToGame+1e-6,"FIN-SH03 capsule first impulse respects total speed cap (%.3f)",Length(w.Find(203)->v));
	}
	// Every friend must remain clear after the final direction turn on the capsule path too.
	{
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;w.b.push_back({204,4,804,{100,0,60},6,1});
		const double a=100*kDeg2Rad;std::vector<S::Friend> friends{{1,100,{594,0,60},false,true},{2,101,{94+500*std::cos(a),500*std::sin(a),60},false,false}};
		auto in=B2L3In(40,friends,mass);in.capVel={3000,0,0};S::Shover sv;sv.Capsule(in,cfg,w,{});
		const auto* can=w.Find(204);
		CHECK(can->pushes==0 || (AngleDeg(can->J,friends[0].pos-Vec3{94,0,60})>=60-1e-6 && AngleDeg(can->J,friends[1].pos-Vec3{94,0,60})>=60-1e-6),"FIN-SH04 final capsule direction clears every friend");
	}
	// Restoration is independent of feature keys, read-section availability, and optional query/impulse budgets.
	{
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;w.b.push_back({205,4,805,{100,0,60},6,1});
		auto in=B2L3In(50,far,mass);in.capVel={3000,0,0};S::Shover sv;sv.Capsule(in,cfg,w,{});
		CHECK(sv.HasPending(),"FIN-SH05 owns a pending filter");
		auto off=cfg;off.pass=false;off.govV=0;off.budgetUs=0;off.maxQueries=0;off.maxPushes=0;
		w.allowRead=false;w.busy=true;in.now+=.016;sv.Capsule(in,off,w,{});
		CHECK(sv.HasPending() && w.Find(205)->group==7,"FIN-SH05 busy cleanup retains ownership");
		sv.Reset();CHECK(sv.HasPending(),"FIN-SH05 Reset retains pending restoration");
		w.busy=false;in.maintenanceOnly=true;in.now+=.016;sv.Capsule(in,off,w,{});
		CHECK(!sv.HasPending() && w.Find(205)->group==805 && sv.St().passRestored==1,"FIN-SH05 keys-off/paused/reset restore through write lock only");
	}
	// A temporary inability to inspect a pending filter never silently abandons ownership.
	{
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;w.b.push_back({206,4,806,{100,0,60},6,1});
		auto in=B2L3In(60,far,mass);in.capVel={3000,0,0};S::Shover sv;sv.Capsule(in,cfg,w,{});
		in.maintenanceOnly=true;w.filterBusy=true;in.now+=.016;sv.Capsule(in,cfg,w,{});
		CHECK(sv.HasPending(),"FIN-SH06 filter refusal retains pending state");
		w.filterBusy=false;in.now+=.016;sv.Capsule(in,cfg,w,{});
		CHECK(!sv.HasPending() && w.Find(206)->group==806,"FIN-SH06 filter restore retries after refusal");
	}
	// A body that changes category/group/motion between query and write is never written as the old candidate.
	for(int mode=0;mode<3;++mode){
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;w.b.push_back({207,4,807,{100,0,60},6,1});w.Find(207)->motion=17;
		w.beforeWrite=[mode](SphereWorld& world){auto* b=world.Find(207);if(mode==0)b->group=100;if(mode==1)b->layer=30;if(mode==2)b->motion=18;};
		auto in=B2L3In(70+mode,far,mass);in.capVel={3000,0,0};S::Shover sv;sv.Capsule(in,cfg,w,{});
		CHECK(w.Find(207)->pushes==0 && w.Find(207)->filterWrites==0,"FIN-SH07 capsule revalidates changed category/group/motion (%d)",mode);
		w.beforeWrite={};w.Find(207)->group=807;w.Find(207)->layer=4;w.Find(207)->motion=17;w.us=0;
		std::vector<S::PartShape> sh{Sph(sp::body::kTorso,{0,0,60},{150,0,60},20,{9000,0,0})};in.now+=.1;in.shapes=sh;in.capOk=false;
		w.beforeWrite=[mode](SphereWorld& world){auto* b=world.Find(207);if(mode==0)b->group=100;if(mode==1)b->layer=30;if(mode==2)b->motion=18;};
		sv.Frame(in,cfg,w);
		CHECK(w.Find(207)->pushes==0,"FIN-SH07 ordinary contact revalidates changed category/group/motion (%d)",mode);
	}
	// The governor refuses a recycled/currently protected body and observes physical-pusher-only contacts.
	{
		SphereWorld w;w.comIsCentre=true;w.b.push_back({208,4,808,{50,0,60},6,1,{5000,0,0}});
		auto in=B2L3In(80,far,mass);in.capOk=false;in.pushersSolid=true;in.pusherLayers=S::kLayProps;
		std::vector<S::PartShape> sh{Sph(sp::body::kTorso,{0,0,60},{60,0,60},20,{3600,0,0})};in.shapes=sh;S::Shover sv;
		const auto frame=sv.Frame(in,cfg,w);const auto cap=sv.Capsule(in,cfg,w,frame);
		CHECK(frame.pushes==0 && frame.nWatched==1 && cap.impulseWrites==1 && std::abs(Length(w.Find(208)->v)-cfg.govV)<1e-6,"FIN-SH08 physical-pusher observation enters governor without fake contact push");
		w.Find(208)->group=100;w.Find(208)->v={5000,0,0};w.us=0;in.now+=.016;const int pushes=w.Find(208)->pushes;sv.Capsule(in,cfg,w,{});
		CHECK(w.Find(208)->pushes==pushes && w.Find(208)->v.x==5000,"FIN-SH08 governor refuses body whose current group became protected");
	}
	// One whole-feature allowance includes AABBs, narrow discovery, neighborhood checks and every impulse producer.
	{
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;
		w.b.push_back({209,4,809,{100,0,60},6,1});w.b.push_back({210,4,810,{110,5,60},6,1});
		auto c=cfg;c.maxQueries=3;c.maxPushes=1;auto in=B2L3In(90,far,mass);in.capVel={3000,0,0};
		std::vector<S::PartShape> sh{Sph(sp::body::kTorso,{0,0,60},{150,0,60},20,{9000,0,0})};in.shapes=sh;S::Shover sv;
		const auto f=sv.Frame(in,c,w);const auto p=sv.Capsule(in,c,w,f);
		CHECK(f.workQueries+p.workQueries<=c.maxQueries && w.closest+w.nears==f.workQueries+p.workQueries,"FIN-SH09 complete query count obeys one allowance");
		CHECK(f.impulseWrites+p.impulseWrites<=c.maxPushes && w.writes==f.impulseWrites+p.impulseWrites && w.writes>0,"FIN-SH09 contact/governor writes share one allowance and feature is exercised");
		CHECK(sv.St().wholeUsLast==w.Us() && sv.St().workQueries==static_cast<std::uint64_t>(w.closest+w.nears),"FIN-SH09 telemetry reports whole-pass work");
	}
	// Physical pushers remain active and follow a body even when engine velocity integration does nothing.
	for(int integrates=0;integrates<2;++integrates){
		S::PusherFollow follow;S::Cfg c;c.pDrive=1;S::PusherTarget target;target.ok=true;target.v={120,0,0};Vec3 body{};bool solid=false;int follows=0;bool trip=false;
		for(int k=0;k<300;++k){
			if(integrates)body+=target.v/60.0;target.c={k*2.0,0,0};
			S::DriveIn in;in.dt=1.0/60;in.wantSolid=true;in.solid=solid;in.n=1;in.now=&target;in.body=&body;
			const auto o=follow.Step(in,c);trip=trip||o.notFollowing;
			if(o.act==S::DriveAct::kSnapSolid || o.act==S::DriveAct::kFollow){body=target.c;solid=true;follow.Placed(in,true);++follows;}
			else if(o.act==S::DriveAct::kGoGhost){solid=false;follow.Unplaced();}
		}
		CHECK(!trip && follows==300 && Length(body-target.c)<1e-6,"FIN-SH10 positive pusher following with integration mode %d",integrates);
	}
	{
		S::PusherFollow follow;S::Cfg c;c.pFollowTolU=10;c.pFollowSec=.2;c.pSnapMaxU=1000;
		S::PusherTarget target;target.ok=true;target.c={200,0,0};target.v={0,6000,0};Vec3 body{};bool trip=false;
		S::DriveIn in;in.dt=1.0/60;in.wantSolid=true;in.solid=true;in.n=1;in.now=&target;in.body=&body;
		for(int k=0;k<30;++k){follow.Placed(in,true);trip=trip||follow.Step(in,c).notFollowing;}
		CHECK(trip,"FIN-SH11 contrary motion is not excused by a large unrelated velocity magnitude");
		target.c={2000,0,0};in.solid=true;const auto o=follow.Step(in,c);CHECK(o.act==S::DriveAct::kGoGhost && o.jump,"FIN-SH11 discontinuous solid move ghosts before placement");
	}
	// Missing either identity snapshot must fail closed even while Info still reports a dynamic object.
	{
		SphereWorld w;w.comIsCentre=true;w.b.push_back({211,4,811,{100,0,60},6,1});w.filterBusy=true;
		w.beforeWrite=[](SphereWorld& world){world.filterBusy=false;world.Find(211)->group=100;};
		auto in=B2L3In(100,far,mass);in.capOk=false;std::vector<S::PartShape> sh{Sph(sp::body::kTorso,{0,0,60},{150,0,60},20,{9000,0,0})};in.shapes=sh;
		S::Shover sv;const auto o=sv.Frame(in,cfg,w);
		CHECK(o.pushes==0 && w.Find(211)->pushes==0 && sv.St().recheckSkips>0,"FIN-SH12 failed initial filter cannot authorize a later write");
	}
	// Independent group-zero props are admitted using motion+shape identity; they are not mistaken for one compound group.
	{
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;w.b.push_back({212,4,0,{100,0,60},6,1});w.Find(212)->motion=212;
		w.b.push_back({213,4,0,{110,10,60},6,1});w.Find(213)->motion=213;
		auto in=B2L3In(110,far,mass);in.capVel={3000,0,0};S::Shover sv;const auto o=sv.Capsule(in,cfg,w,{});
		CHECK(sv.PassedNow()==2 && o.pushes>=1,"FIN-SH13 independent group-zero props use motion identity and retain useful contact");
		in.maintenanceOnly=true;in.now+=.016;sv.Capsule(in,cfg,w,{});
		CHECK(w.Find(212)->group==0 && w.Find(213)->group==0 && !sv.HasPending(),"FIN-SH13 exact group-zero filters restored");
	}

	// The shared hip reaction is capped once for the whole frame, even when both contact producers write.
	{
		SphereWorld w;w.comIsCentre=true;w.selfGroup=7;
		w.b.push_back({214,4,814,{30,0,60},6,1});w.b.push_back({215,4,815,{100,0,60},6,1});
		auto c=cfg;c.reactMaxDv=.1;auto in=B2L3In(120,far,mass);in.capVel={3000,0,0};
		std::vector<S::PartShape> sh{Sph(sp::body::kHip,{0,0,60},{40,0,60},20,{2400,0,0})};in.shapes=sh;S::Shover sv;
		const auto f=sv.Frame(in,c,w);const auto p=sv.Capsule(in,c,w,f);Vec3 reaction{};
		for(int k=0;k<f.nReact;++k)if(f.react[k].part==sp::body::kHip)reaction+=f.react[k].J;
		for(int k=0;k<p.nReact;++k)if(p.react[k].part==sp::body::kHip)reaction+=p.react[k].J;
		CHECK(f.pushes>=1 && p.pushes>=1 && Length(reaction)/mass[sp::body::kHip]<=c.reactMaxDv+1e-9,"FIN-SH14 summed Frame/Capsule hip reaction respects one per-frame cap");
	}
	// Mandatory pusher lifetime sampling is measured even when upstream preparation used the optional budget.
	{
		SphereWorld w;w.comIsCentre=true;w.b.push_back({216,4,816,{40,0,60},6,1});w.us=500;
		auto c=cfg;c.pushers=true;c.budgetUs=40;c.maxQueries=1;auto in=B2L3In(130,far,mass);in.capOk=false;
		std::vector<S::PartShape> sh{Sph(sp::body::kHip,{0,0,60},{0,0,60},20,{})};in.shapes=sh;S::Shover sv;const auto o=sv.Frame(in,c,w);
		CHECK(o.propsNear && o.workQueries==1 && o.pushes==0 && sv.St().mandatoryOverBudget==1,"FIN-SH15 one counted mandatory probe preserves physical lifetime despite optional budget exhaustion");
	}

}
