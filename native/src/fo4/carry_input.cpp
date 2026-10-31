// OWNER: NATIVES (CARRY).  See carry_input.h.
#include "perf/cost_probe.h"
#include "fo4/carry_input.h"

#include "config.h"
#include "core/bookmark.h"  // R6 BOOKMARK

#include <atomic>
#include <cmath>
#include <cstring>
#include <format>

#include <REX/W32/KERNEL32.h>

namespace sp::fo4::carry_input
{
	namespace
	{
		using sp::carry::Edge;
		namespace C = sp::carry;

		double NowImpl()
		{
			static const double inv = [] {
				std::int64_t f = 0;
				REX::W32::QueryPerformanceFrequency(&f);
				return f > 0 ? 1.0 / static_cast<double>(f) : 0.0;
			}();
			std::int64_t c = 0;
			REX::W32::QueryPerformanceCounter(&c);
			return static_cast<double>(c) * inv;
		}

		std::atomic<bool> g_registered{ false };

		// held state: the observer writes, the pump reads
		std::atomic<std::uint32_t>                   g_held{ 0 };
		std::atomic<float>                           g_stickX{ 0 }, g_stickY{ 0 };
		std::atomic<std::uint32_t>                   g_lookN{ 0 };
		std::atomic<bool>                            g_padActive{ false };
		std::array<std::atomic<double>, C::kBtnCount> g_lastSeen{};
		std::array<std::atomic<bool>, C::kBtnCount>   g_repeats{};
		std::atomic<int>                             g_hotVk{ 0 }, g_modVk{ 0 };
		std::atomic<std::uint64_t>                   g_events{ 0 }, g_lost{ 0 }, g_menuDrops{ 0 }, g_disabled{ 0 }, g_edges{ 0 };
		std::atomic<bool>                            g_clearReq{ false };
		// two observation paths: 1 MenuControls handler, 2 PlayerControls pre-scan (auto selection, see carry_input.h)
		std::atomic<int>                             g_active{ 1 };
		std::atomic<std::uint64_t>                   g_srcSeen[3]{};
		std::atomic<bool>                            g_prescan{ false };
		std::uintptr_t                               g_origPerform{ 0 };
		double                                       g_lastTake{ 0 };  // pump
		std::atomic<bool>                            g_menuInput{ false };  // an input event arrived in menu mode
		bool                                         g_switchLogged{ false };

		// observer-thread state (the engine dispatches input on one thread)
		std::array<bool, C::kBtnCount>   s_down{};
		std::array<double, C::kBtnCount> s_downT{};

		// R6 BOOKMARK: keys (pump writes, observer reads), the trigger (observer thread only), the request (observer -> pump)
		std::atomic<int>           g_bmKey{ 0 }, g_bmMod{ 0 };
		std::atomic<std::uint32_t> g_bmPad{ 0 };
		std::atomic<double>        g_bmWindow{ 0.5 }, g_bmDebounce{ 2.0 };
		std::atomic<int>           g_bmReq{ 0 };
		sp::bookmark::Trigger      s_bm;

		void BookmarkEdge(bool a_pad, std::uint32_t a_code, bool a_down, bool a_menu, double a_t, bool a_fresh)
		{
			sp::bookmark::TriggerCfg c;
			c.key = g_bmKey.load(std::memory_order_relaxed);
			c.keyMod = g_bmMod.load(std::memory_order_relaxed);
			c.padMask = g_bmPad.load(std::memory_order_relaxed);
			c.padWindow = g_bmWindow.load(std::memory_order_relaxed);
			c.debounce = g_bmDebounce.load(std::memory_order_relaxed);
			if (c.key <= 0 && c.padMask == 0) return;
			s_bm.SetCfg(c);
			if (a_menu && a_down) return;  // never from a menu or the console (releases still count: the held state stays right)
			const bool fire = a_pad ? s_bm.Pad(a_code, a_down, a_t, a_fresh) : s_bm.Key(static_cast<int>(a_code), a_down, a_t, a_fresh);
			if (fire) g_bmReq.store(a_pad ? 2 : 1, std::memory_order_release);
		}

		// ---- lock-free ring of edges (multi-producer safe, single consumer = the pump) ----
		template <class T, std::uint32_t N>
		struct Ring
		{
			struct Slot
			{
				std::atomic<std::uint32_t> seq{ 0 };
				T                          v{};
			};
			std::array<Slot, N>        slot{};
			std::atomic<std::uint32_t> head{ 0 };
			std::uint32_t              tail{ 0 };  // consumer only

			void Push(const T& a_v)
			{
				const std::uint32_t i = head.fetch_add(1, std::memory_order_acq_rel);
				auto&               s = slot[i % N];
				s.seq.store(0, std::memory_order_release);
				s.v = a_v;
				s.seq.store(i + 1, std::memory_order_release);
			}
			// false = empty. a_lost counts entries that were overwritten before they were read.
			bool Pop(T& a_out, std::uint64_t& a_lost)
			{
				for (int guard = 0; guard < 4; ++guard) {
					const std::uint32_t h = head.load(std::memory_order_acquire);
					if (tail == h) return false;
					if (h - tail > N) {  // lapped
						a_lost += h - tail - N;
						tail = h - N;
					}
					auto&               s = slot[tail % N];
					const std::uint32_t s1 = s.seq.load(std::memory_order_acquire);
					if (s1 != tail + 1) {
						if (s1 == 0 || s1 < tail + 1) return false;  // being written right now: next pump
						++a_lost;
						++tail;
						continue;
					}
					a_out = s.v;
					std::atomic_thread_fence(std::memory_order_acquire);
					if (s.seq.load(std::memory_order_relaxed) != s1) {
						++a_lost;
						++tail;
						continue;
					}
					++tail;
					return true;
				}
				return false;
			}
			void Drop() { tail = head.load(std::memory_order_acquire); }
		};
		Ring<Edge, 128> g_ring;

		struct RawRec
		{
			char  name[24]{};
			int   type{ 0 };
			int   dev{ 0 };
			int   code{ 0 };
			float v{ 0 }, held{ 0 }, x{ 0 }, y{ 0 };
			bool  disabled{ false };
			bool  menu{ false };
		};
		Ring<RawRec, 64>  g_raw;
		std::atomic<int>  g_logBudget{ 0 };

		// ---- DebugCarryInput override (VM thread writes, pump reads) ----
		std::atomic<float>         g_ovX{ 0 }, g_ovY{ 0 }, g_ovPitch{ 0 };
		std::atomic<int>           g_ovButtons{ 0 };
		std::atomic<double>        g_ovUntil{ 0 };
		std::atomic<std::uint32_t> g_ovGen{ 0 };
		std::uint32_t              g_ovGenTaken{ 0 };  // pump

		int BtnOf(const char* n)
		{
			if (!n || !*n) return -1;
			switch (std::strlen(n)) {
			case 4:
				if (!_stricmp(n, "Back")) return C::kBack;
				if (!_stricmp(n, "Jump")) return C::kJump;
				break;
			case 5:
				if (!_stricmp(n, "Sneak")) return C::kSneak;
				break;
			case 6:
				if (!_stricmp(n, "Sprint")) return C::kSprint;
				break;
			case 7:
				if (!_stricmp(n, "Forward")) return C::kForward;
				break;
			case 10:
				if (!_stricmp(n, "StrafeLeft")) return C::kStrafeL;
				break;
			case 11:
				if (!_stricmp(n, "StrafeRight")) return C::kStrafeR;
				break;
			default:
				break;
			}
			return -1;
		}

		void Button(int b, bool downNow, float held, bool pad, double t, bool menu)
		{
			if (b < 0 || b >= C::kBtnCount) return;
			if (downNow) {
				g_lastSeen[b].store(t, std::memory_order_relaxed);
				if (!s_down[b]) {
					s_down[b] = true;
					s_downT[b] = t;
					g_held.fetch_or(1u << b, std::memory_order_acq_rel);
					if (menu) {
						g_menuDrops.fetch_add(1, std::memory_order_relaxed);  // console typing, menus: no press edge
					} else {
						g_ring.Push({ b, true, pad, t, 0.0 });
						g_edges.fetch_add(1, std::memory_order_relaxed);
					}
				} else if (held > 0.0f) {
					g_repeats[b].store(true, std::memory_order_relaxed);  // the engine repeats held buttons
				}
			} else if (s_down[b]) {
				s_down[b] = false;
				g_held.fetch_and(~(1u << b), std::memory_order_acq_rel);
				g_ring.Push({ b, false, pad, t, held > 0.0f ? static_cast<double>(held) : t - s_downT[b] });
				g_edges.fetch_add(1, std::memory_order_relaxed);
			}
		}

		void Record(const RE::IDEvent* e, int type, float v, float held, float x, float y, bool menu)
		{
			if (g_logBudget.load(std::memory_order_relaxed) <= 0) return;
			if (g_logBudget.fetch_sub(1, std::memory_order_relaxed) <= 0) return;
			RawRec r;
			const char* n = e->QRawUserEvent().c_str();
			if (n) strncpy_s(r.name, n, _TRUNCATE);
			r.type = type;
			r.dev = static_cast<int>(e->device.get());
			r.code = e->idCode;
			r.v = v;
			r.held = held;
			r.x = x;
			r.y = y;
			r.disabled = e->disabled;
			r.menu = menu;
			g_raw.Push(r);
		}

		void Observe(const RE::InputEvent* ev, int src)
		{
			if (!ev) return;
			{  // count the movement / button events each path sees; only the active path records them
				const auto ty = ev->eventType.get();
				if (ty == RE::INPUT_EVENT_TYPE::kButton || ty == RE::INPUT_EVENT_TYPE::kThumbstick) g_srcSeen[src].fetch_add(1, std::memory_order_relaxed);
				if (src != g_active.load(std::memory_order_relaxed)) return;
			}
			g_events.fetch_add(1, std::memory_order_relaxed);
			if (g_clearReq.exchange(false, std::memory_order_acq_rel)) {
				s_down = {};
				g_held.store(0);
			}
			const auto  type = ev->eventType.get();
			const auto  dev = ev->device.get();
			const bool  pad = dev == RE::INPUT_DEVICE::kGamepad;
			const auto* m = RE::Main::GetSingleton();
			const bool  menu = m && m->inMenuMode;
			if (menu) g_menuInput.store(true, std::memory_order_relaxed);
			if (type == RE::INPUT_EVENT_TYPE::kButton) {
				const auto* be = static_cast<const RE::ButtonEvent*>(ev);
				const double t = NowImpl();
				if (be->disabled) g_disabled.fetch_add(1, std::memory_order_relaxed);
				const bool down = be->value != 0.0f;
				const int  b = BtnOf(be->QRawUserEvent().c_str());
				if (b >= 0) {
					Button(b, down, be->heldDownSecs, pad, t, menu);
					g_padActive.store(pad, std::memory_order_relaxed);
				}
				if (dev == RE::INPUT_DEVICE::kKeyboard) {
					const int code = be->idCode;
					if (const int hv = g_hotVk.load(std::memory_order_relaxed); hv && code == hv) Button(C::kHotkey, down, be->heldDownSecs, false, t, menu);
					if (const int mv = g_modVk.load(std::memory_order_relaxed); mv && code == mv) Button(C::kHotMod, down, be->heldDownSecs, false, t, menu);
					BookmarkEdge(false, static_cast<std::uint32_t>(code), down, menu, t, down && be->heldDownSecs == 0.0f);  // R6 BOOKMARK
				} else if (pad) {
					BookmarkEdge(true, static_cast<std::uint32_t>(be->idCode), down, menu, t, down && be->heldDownSecs == 0.0f);  // R6 BOOKMARK (XInput button bit)
				}
				if (b >= 0 || (be->value != 0.0f && be->heldDownSecs == 0.0f)) Record(be, 0, be->value, be->heldDownSecs, 0, 0, menu);
			} else if (type == RE::INPUT_EVENT_TYPE::kThumbstick) {
				const auto* ts = static_cast<const RE::ThumbstickEvent*>(ev);
				const char* n = ts->QRawUserEvent().c_str();
				const bool  isMove = (n && !_stricmp(n, "Move")) || ((!n || !*n) && ts->idCode == RE::ThumbstickEvent::kLeft);
				if (isMove) {
					g_stickX.store(std::isfinite(ts->xValue) ? ts->xValue : 0.0f, std::memory_order_relaxed);
					g_stickY.store(std::isfinite(ts->yValue) ? ts->yValue : 0.0f, std::memory_order_relaxed);
					g_padActive.store(true, std::memory_order_relaxed);
					Record(ts, 4, 0, 0, ts->xValue, ts->yValue, menu);
				} else {
					g_lookN.fetch_add(1, std::memory_order_relaxed);
				}
			} else if (type == RE::INPUT_EVENT_TYPE::kMouseMove) {
				g_lookN.fetch_add(1, std::memory_order_relaxed);
			}
		}

		class Observer final : public RE::BSInputEventUser
		{
		public:
			// Record and decline: the engine never routes the event to us, nothing is consumed.
			bool ShouldHandleEvent(const RE::InputEvent* a_event) override
			{
			SP_COST_SCOPE(Input);
				Observe(a_event, 1);
				return false;
			}
		};

		// PlayerControls::PerformInputProcessing(queueHead) pre-scan (the live vtable's slot 0)
		using PerformFn = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		void PrescanHook(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_head)
		{
			SP_COST_SCOPE(Input);
			int n = 0;
			for (const RE::InputEvent* e = a_head; e && n < 512; e = e->next, ++n) Observe(e, 2);
			SP_COST_PAUSE();
			reinterpret_cast<PerformFn*>(g_origPerform)(a_this, a_head);
		}

		bool ColName(std::uintptr_t a_vtbl, std::uint32_t& a_off, char* a_out, std::size_t a_cap)
		{
			const auto col = *reinterpret_cast<const std::uintptr_t*>(a_vtbl - sizeof(void*));
			if (!col) return false;
			a_off = *reinterpret_cast<const std::uint32_t*>(col + 4);
			const auto td = *reinterpret_cast<const std::uint32_t*>(col + 0x0C);
			const auto self = *reinterpret_cast<const std::uint32_t*>(col + 0x14);
			const char* nm = reinterpret_cast<const char*>(col - self + td + 0x10);
			std::size_t i = 0;
			for (; i + 1 < a_cap && nm[i]; ++i) a_out[i] = nm[i];
			a_out[i] = 0;
			return true;
		}
		bool SehColName(std::uintptr_t a_vtbl, std::uint32_t* a_off, char* a_out, std::size_t a_cap) noexcept
		{
			__try {
				return ColName(a_vtbl, *a_off, a_out, a_cap);
			} __except (1) {
				return false;
			}
		}

		void InstallPrescan()
		{
			if (g_prescan.load()) return;
			auto* pc = RE::PlayerControls::GetSingleton();
			if (!pc) {
				REX::WARN("SPN|W|carry input: no PlayerControls: pre-scan path off");
				return;
			}
			auto*         vtbl = *reinterpret_cast<std::uintptr_t**>(pc);
			std::uint32_t off = 0xFFFFFFFF;
			char          name[96]{};
			if (!vtbl || !SehColName(reinterpret_cast<std::uintptr_t>(vtbl), &off, name, sizeof(name)) || off != 0 || !std::strstr(name, "PlayerControls@@")) {
				REX::WARN("SPN|W|carry input: PlayerControls vtable RTTI check failed (offset 0x{:X} type {}): pre-scan path off", off, name);
				return;
			}
			g_origPerform = vtbl[0];
			REL::WriteSafeData(reinterpret_cast<std::uintptr_t>(&vtbl[0]), reinterpret_cast<std::uintptr_t>(&PrescanHook));
			g_prescan.store(true);
			REX::INFO("SPN|I|carry input pre-scan installed on {} (live vtable slot 0)", name);
		}

		std::uint32_t g_lookTaken{ 0 };
	}

	double Now() { return NowImpl(); }
	bool   Registered() { return g_registered.load(); }

	void Register()
	{
		if (g_registered.load()) return;
		auto* mc = RE::MenuControls::GetSingleton();
		if (!mc) {
			REX::WARN("SPN|W|carry input: no MenuControls at kGameDataReady (hotkey / chord / steering off)");
			return;
		}
		const int path = static_cast<int>(sp::cfg::Tunable("carry.inputPath", 0.0));
		g_active.store(path == 2 ? 2 : 1);
		if (path != 2) {
			static Observer* s_obs = new Observer();  // never freed: the engine keeps the pointer for the session
			mc->RegisterHandler(s_obs);
		}
		if (path != 1) InstallPrescan();
		g_registered.store(true);
		REX::INFO("SPN|I|carry input observer registered (path {}: {}; MenuControls {} handlers)", path, path == 1 ? "MenuControls" : path == 2 ? "PlayerControls pre-scan" : "auto",
			mc->handlers.size());
	}

	void SetKeys(int a_hot, int a_mod)
	{
		g_hotVk.store(a_hot, std::memory_order_relaxed);
		g_modVk.store(a_mod, std::memory_order_relaxed);
	}

	void Clear()
	{
		g_clearReq.store(true);
		g_held.store(0);
		g_stickX.store(0);
		g_stickY.store(0);
		g_ring.Drop();
		g_raw.Drop();
		g_ovUntil.store(0);
	}

	void SetOverride(float mx, float my, int buttons, float pitchDeg, float seconds)
	{
		g_ovX.store(std::isfinite(mx) ? std::clamp(mx, -1.0f, 1.0f) : 0.0f);
		g_ovY.store(std::isfinite(my) ? std::clamp(my, -1.0f, 1.0f) : 0.0f);
		g_ovButtons.store(buttons);
		g_ovPitch.store(std::isfinite(pitchDeg) ? std::clamp(pitchDeg, -89.0f, 89.0f) : 0.0f);
		g_ovUntil.store(seconds > 0.0f ? NowImpl() + seconds : 0.0);
		g_ovGen.fetch_add(1);
	}

	bool OverrideActive() { return NowImpl() < g_ovUntil.load(std::memory_order_relaxed); }

	void Take(Taken& o)
	{
		o = Taken{};
		o.now = NowImpl();
		// auto path: in play (the pump ticks), MenuControls saw nothing while the pre-scan saw 20 movement / button events
		// -> the pre-scan path records from now on (any MenuControls event restarts the window)
		{
			static std::uint64_t s_last1 = 0, s_last2 = 0, s_w2 = 0;
			const auto           n1 = g_srcSeen[1].load(), n2 = g_srcSeen[2].load();
			const auto           d1 = n1 - s_last1, d2 = n2 - s_last2;
			s_last1 = n1;
			s_last2 = n2;
			s_w2 = d1 ? 0 : s_w2 + d2;
			if (g_active.load() == 1 && g_prescan.load() && s_w2 >= 20 && sp::cfg::Tunable("carry.inputPath", 0.0) == 0.0) {
				g_active.store(2);
				g_clearReq.store(true);
				if (!g_switchLogged) REX::WARN("SPN|W|carry input: MenuControls delivered no game input (pre-scan saw {}) -> PlayerControls pre-scan path", s_w2);
				g_switchLogged = true;
			}
		}
		// a menu gap (the pump did not tick and input arrived in menu mode) may have eaten releases: forget held buttons (a
		// still-held key comes back with its next repeat event). A plain hitch keeps them.
		const bool menuInput = g_menuInput.exchange(false, std::memory_order_relaxed);
		if (menuInput && g_lastTake > 0 && o.now - g_lastTake > sp::cfg::Tunable("carry.gapClear", 0.3)) {
			g_clearReq.store(true);
			g_held.store(0);
		}
		g_lastTake = o.now;
		const std::uint32_t gen = g_ovGen.load();
		const bool          newGen = gen != g_ovGenTaken;
		g_ovGenTaken = gen;
		const std::uint32_t look = g_lookN.load(std::memory_order_relaxed);
		o.lookInput = look != g_lookTaken;
		g_lookTaken = look;
		o.padActive = g_padActive.load(std::memory_order_relaxed);
		if (o.now < g_ovUntil.load()) {
			o.overridden = true;
			std::uint64_t lost = 0;
			Edge          e;
			while (g_ring.Pop(e, lost)) {}  // the real input is ignored while overridden
			const int b = g_ovButtons.load();
			if (b & 1) o.heldMask |= 1u << C::kJump;
			if (b & 2) o.heldMask |= 1u << C::kSneak;
			if (b & 4) o.heldMask |= 1u << C::kSprint;
			if (b & 8) o.heldMask |= 1u << C::kHotMod;
			o.stickX = g_ovX.load();
			o.stickY = g_ovY.load();
			o.pitchOverride = (b & 16) != 0;
			o.pitchDeg = g_ovPitch.load();
			if (newGen) {
				auto tap = [&](int btn) {
					if (o.nEdges + 2 > static_cast<int>(o.edges.size())) return;
					o.edges[o.nEdges++] = { btn, true, false, o.now, 0.0 };
					o.edges[o.nEdges++] = { btn, false, false, o.now, 0.05 };
				};
				if (b & 8) tap(C::kHotkey);
				if (b & 32) tap(C::kSneak);
				if (b & 64) tap(C::kSprint);
			}
			return;
		}
		std::uint32_t held = g_held.load(std::memory_order_acquire);
		const double  stale = sp::cfg::Tunable("carry.heldStale", 0.5);
		for (int b = 0; b < C::kBtnCount; ++b)
			if ((held >> b) & 1u)
				if (stale > 0 && g_repeats[b].load(std::memory_order_relaxed) && o.now - g_lastSeen[b].load(std::memory_order_relaxed) > stale) held &= ~(1u << b);
		o.heldMask = held;
		o.stickX = g_stickX.load(std::memory_order_relaxed);
		o.stickY = g_stickY.load(std::memory_order_relaxed);
		if (std::abs(o.stickX) < 0.05 && std::abs(o.stickY) < 0.05) o.stickX = o.stickY = 0;  // tiny drift is no input
		std::uint64_t lost = 0;
		Edge          e;
		while (o.nEdges < static_cast<int>(o.edges.size()) && g_ring.Pop(e, lost)) o.edges[o.nEdges++] = e;
		if (lost) g_lost.fetch_add(lost, std::memory_order_relaxed);
	}

	void ArmLog(int a_n) { g_logBudget.store(std::max(0, a_n)); }

	void LogPending()
	{
		std::uint64_t lost = 0;
		RawRec        r;
		int           n = 0;
		while (n < 64 && g_raw.Pop(r, lost)) {
			++n;
			const char* dev = r.dev == 0 ? "kb" : r.dev == 1 ? "mouse" : r.dev == 2 ? "pad" : "?";
			if (r.type == 4)
				REX::INFO("SPN|CIN|{} dev={} code=0x{:X} x={:.2f} y={:.2f} dis={} menu={}", r.name, dev, r.code, r.x, r.y, r.disabled, r.menu);
			else
				REX::INFO("SPN|CIN|{} dev={} code=0x{:X} v={:.2f} held={:.2f} dis={} menu={}", r.name, dev, r.code, r.v, r.held, r.disabled, r.menu);
		}
	}

	void SetBookmarkKeys(int a_vk, int a_modVk, std::uint32_t a_padMask, double a_padWindow, double a_debounce)
	{
		g_bmKey.store(a_vk, std::memory_order_relaxed);
		g_bmMod.store(a_modVk, std::memory_order_relaxed);
		g_bmPad.store(a_padMask, std::memory_order_relaxed);
		g_bmWindow.store(std::isfinite(a_padWindow) ? std::clamp(a_padWindow, 0.05, 3.0) : 0.5, std::memory_order_relaxed);
		g_bmDebounce.store(std::isfinite(a_debounce) ? std::clamp(a_debounce, 0.0, 60.0) : 2.0, std::memory_order_relaxed);
	}

	int TakeBookmark() { return g_bmReq.exchange(0, std::memory_order_acq_rel); }

	std::string Describe()
	{
		return std::format("cin(reg={} path={} seen menu/prescan={}/{} ev={} edges={} lost={} menuDrop={} disabled={} held=0x{:X} stick=({:.2f},{:.2f}) pad={} ov={})", g_registered.load(),
			g_active.load(), g_srcSeen[1].load(), g_srcSeen[2].load(), g_events.load(), g_edges.load(), g_lost.load(), g_menuDrops.load(), g_disabled.load(), g_held.load(), g_stickX.load(),
			g_stickY.load(), g_padActive.load(), OverrideActive());
	}
}
