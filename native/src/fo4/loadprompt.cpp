// OWNER: INTEGRATOR (POLISH round).  DEBUG ONLY: see loadprompt.h. Nothing here runs unless [Debug] AcceptLoadPrompt=1.
#include "perf/cost_probe.h"
#include "fo4/loadprompt.h"

#include "config.h"

#include <atomic>
#include <string>
#include <string_view>

namespace sp::fo4::loadprompt
{
	namespace
	{
		std::atomic<bool> g_armed{ false };
		std::atomic<bool> g_taskQueued{ false };
		std::atomic<int>  g_accepted{ 0 };  // callbacks fired (at most 1 per process)
		std::atomic<int>  g_rehides{ 0 };   // the same prompt seen again after it was accepted
		std::atomic<int>  g_misses{ 0 };    // MessageBoxMenu opened without the load prompt in the manager

		bool IsLoadPrompt(const RE::MessageBoxData* a_data)
		{
			if (!a_data) return false;
			const std::string_view body = a_data->bodyText;
			return body.find("Achievements") != std::string_view::npos || body.find("Creations are currently loaded") != std::string_view::npos;
		}

		std::string Clip(std::string_view a_text)
		{
			std::string s(a_text.substr(0, 70));
			for (auto& c : s)
				if (c == '\r' || c == '\n' || c == '|') c = ' ';
			return s;
		}

		void HideBox()
		{
			if (auto* q = RE::UIMessageQueue::GetSingleton())
				q->AddMessage(RE::MessageBoxMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
		}

		// UI task (F4SE AddUITask): the same pump the menu's own button handling runs in.
		void AcceptTask()
		{
			SP_COST_SCOPE(Event);
			g_taskQueued.store(false);
			auto* mgr = RE::MessageMenuManager::GetSingleton();
			if (!mgr) {
				REX::WARN("SPN|W|debug: load prompt: no MessageMenuManager");
				return;
			}
			RE::BSTSmartPointer<RE::IMessageBoxCallback> cb;
			RE::MessageBoxData*                          hit = nullptr;
			std::string                                  seen;
			int                                          n = 0;
			mgr->messageRWLock.lock_write();
			for (auto it = mgr->messages.begin(); it != mgr->messages.end(); ++it) {
				auto* d = *it;
				++n;
				if (IsLoadPrompt(d)) {
					hit = d;
					cb = d->callback;
					// Seen again after we answered it: the manager kept it, so drop it from the queue (debug runs only;
					// the data object itself is left alone, the menu may still point at it).
					if (g_accepted.load() > 0) mgr->messages.erase(it);
					break;
				}
				if (d && seen.size() < 200) seen += "[" + Clip(std::string_view(d->bodyText)) + "]";
			}
			mgr->messageRWLock.unlock_write();

			if (!hit) {
				if (g_misses.fetch_add(1) < 5) REX::INFO("SPN|I|debug: MessageBoxMenu without the load prompt (n={} {})", n, seen);
				return;
			}
			if (g_accepted.load() > 0) {
				const int r = g_rehides.fetch_add(1) + 1;
				if (r <= 3) {
					HideBox();
					REX::WARN("SPN|W|debug: load prompt shown again after it was accepted -> removed + hidden ({})", r);
				}
				return;
			}
			g_accepted.fetch_add(1);
			REX::WARN("SPN|W|debug: accepted the load prompt (Yes; callback={} n={} body='{}')", cb ? "yes" : "none", n, Clip(std::string_view(hit->bodyText)));
			if (cb) (*cb)(0);  // button 0 = Yes
			HideBox();
		}

		class Sink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				SP_COST_SCOPE(Event);
				if (a_event.opening && a_event.menuName == RE::MessageBoxMenu::MENU_NAME && !g_taskQueued.exchange(true)) {
					if (const auto* t = F4SE::GetTaskInterface())
						t->AddUITask(AcceptTask);
					else
						g_taskQueued.store(false);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Arm()
	{
		if (sp::cfg::Tunable("Debug.acceptloadprompt", 0.0) == 0.0) return;  // shipped: no [Debug] section
		if (g_armed.exchange(true)) return;
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			g_armed.store(false);
			REX::WARN("SPN|W|debug: AcceptLoadPrompt set but no UI singleton yet");
			return;
		}
		static Sink sink;
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
		REX::WARN("SPN|W|debug: AcceptLoadPrompt=1 (harness only): the 'Creations are currently loaded' load prompt is answered Yes");
	}
}
