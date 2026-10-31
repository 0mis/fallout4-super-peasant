#pragma once
// OWNER: R6 (round 6 step 3, CORE lane).  Pure: the load-time self-checks of the engine functions step 3 CALLS and the
// fields it READS (steps\s3\HOOKS.md section C). Step 3 adds no hooks, so a check compares the Address Library RVA of an
// id and the first bytes at (function + off) with the bytes seen on the 1.11.240 exe copy ("xx" = any byte: a rip-relative
// displacement). A mismatch switches off only the named feature (the fallback of HOOKS.md D), with one log line:
//   SPN|R6|check <name> id=<id> rva=<hex>: ok
//   SPN|R6|check <name> id=<id> rva=<hex>: REFUSED why=<rva|bytes|unreadable> -> feat <feat> off
// The reader is injectable so the desktop test feeds right / wrong bytes / a wrong RVA (refusal path, no game).
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace sp::r6check
{
	struct Proof
	{
		const char*   name;
		std::uint64_t id;
		std::uint32_t rva;   // expected RVA of the id (VA - 0x140000000 on the exe copy)
		std::uint32_t off;   // bytes start at id + off
		const char*   hex;   // "8B 81 3C 04 .." ("xx" = any); nullptr / "" = RVA only (data)
		const char*   feat;  // the feature switched off on a mismatch
	};

	// HOOKS.md (step 3) section C, verified on the exe copy 2026-10-14
	inline constexpr Proof kProofs[] = {
		{ "Actor::GetHostileToActor", 2229968, 0xC7BBA0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48", "percept.hitAsk" },
		{ "Main::WorldRootCamera", 2228956, 0xC37DD0, 0, "48 8B 05 xx xx xx xx 48 8B 80 40 01 00 00 C3", "percept.cam" },
		{ "NiCamera::WorldPtToScreenPt3", 2270344, 0x16D2440, 0, "48 83 EC 18 F3 41 0F 10 60 04 F3 41 0F 10 28 0F", "percept.cam" },
		{ "TESObjectREFR::GetOwner", 2202616, 0x561E20, 0, "40 53 48 83 EC 20 48 8B D9 48 8B 89 00 01 00 00", "percept.workshop" },
		{ "PlayerCamera singleton", 4796065, 0x30E6E58, 0, "", "percept.cam" },
		{ "killMove bit +0x43C/14", 2231153, 0xCD59B0, 0, "8B 81 3C 04 00 00 C1 E8 0E 24 01 C3", "percept.killRole" },
		{ "killMove bit set", 2231151, 0xCD57F0, 0x45, "81 8B 3C 04 00 00 00 40 00 00", "percept.killRole" },
		{ "partner handle +0x384 (a)", 2231151, 0xCD57F0, 0x63, "89 83 84 03 00 00", "percept.killRole" },
		{ "partner handle +0x384 (b)", 2231151, 0xCD57F0, 0x7A, "89 83 84 03 00 00", "percept.killRole" },
		{ "race pointer +0x418", 2229906, 0xC73EC0, 0x2E, "48 8B 8B 18 04 00 00", "percept.immobile" },
		{ "race flags +0x1A0 bit 9", 2229906, 0xC73EC0, 0x3A, "8B 81 A0 01 00 00 C1 E8 09 24 01", "percept.immobile" },
		{ "NiCamera at WorldRootNode +0x140", 2228956, 0xC37DD0, 0x07, "48 8B 80 40 01 00 00 C3", "percept.cam" },
		// R6 step 4 (steps\s4\HOOKS.md C, FINAL 12:33): the defend jump's controller write, the lost transfer's space reads,
		// the damage path (log only). The RB vtable slots 0x33 / 0x34 are compared with the function addresses at run time
		// (warden_glue RunChecks: ASLR moves both).
		{ "bhkCharRigidBodyController::SetPositionImpl", 2278671, 0x18BAC20, 0, "48 8B C4 41 56 48 81 EC 80 00 00 00 0F 28 0D xx xx xx xx 4C 8B F1", "tele.jump" },
		{ "parentCell +0xB8 (GetWorldSpace)", 2201167, 0x5172E0, 0x04, "48 8B 81 B8 00 00 00", "tele.lost" },
		{ "cell interior flag +0x40 bit 0", 2201167, 0x5172E0, 0x1E, "F6 40 40 01", "tele.lost" },
		{ "cell worldSpace +0xC8", 2201167, 0x5172E0, 0x2B, "48 8B 80 C8 00 00 00", "tele.lost" },
		{ "Actor::HandleHealthDamage", 2231144, 0xCD2AE0, 0, "48 89 5C 24 08 57 48 83 EC 40 48 8B 01 48 8B FA", "probe.kill" },
	};

	// Parses "8B 81 xx .."; returns the count (<= a_cap); a_any[i] = 1 for "xx". -1 = malformed.
	inline int ParseHex(std::string_view a_hex, std::uint8_t* a_out, std::uint8_t* a_any, int a_cap)
	{
		int n = 0;
		std::size_t i = 0;
		while (i < a_hex.size()) {
			while (i < a_hex.size() && a_hex[i] == ' ') ++i;
			if (i >= a_hex.size()) break;
			if (i + 1 >= a_hex.size() || n >= a_cap) return -1;
			const char a = a_hex[i], b = a_hex[i + 1];
			if ((a == 'x' || a == 'X') && (b == 'x' || b == 'X')) {
				a_out[n] = 0;
				a_any[n] = 1;
			} else {
				auto v = [](char c) -> int {
					if (c >= '0' && c <= '9') return c - '0';
					c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
					if (c >= 'A' && c <= 'F') return c - 'A' + 10;
					return -1;
				};
				const int hi = v(a), lo = v(b);
				if (hi < 0 || lo < 0) return -1;
				a_out[n] = static_cast<std::uint8_t>(hi * 16 + lo);
				a_any[n] = 0;
			}
			++n;
			i += 2;
		}
		return n;
	}

	// a_read(id, rva&, off, out, n) -> false = unreadable. Returns true when the RVA and the bytes match; a_why otherwise.
	template <class Read>
	bool CheckProof(const Proof& p, Read&& a_read, std::string& a_why)
	{
		std::uint8_t want[32]{}, any[32]{}, got[32]{};
		const int    n = (p.hex && p.hex[0]) ? ParseHex(p.hex, want, any, 32) : 0;
		if (n < 0) {
			a_why = "bad proof text";
			return false;
		}
		std::uint32_t rva = 0;
		if (!a_read(p.id, rva, p.off, got, n)) {
			a_why = "unreadable";
			return false;
		}
		if (rva != p.rva) {
			char b[64];
			std::snprintf(b, sizeof(b), "rva %X != %X", rva, p.rva);
			a_why = b;
			return false;
		}
		for (int i = 0; i < n; ++i)
			if (!any[i] && got[i] != want[i]) {
				char b[64];
				std::snprintf(b, sizeof(b), "bytes (+%u: %02X != %02X)", p.off + static_cast<unsigned>(i), got[i], want[i]);
				a_why = b;
				return false;
			}
		return true;
	}

	// Feature state after the checks (true = every proof of that feature passed)
	struct Features
	{
		bool hitAsk{ false }, cam{ false }, workshop{ false }, killRole{ false }, immobile{ false };
		bool ran{ false };
		bool teleJump{ false }, teleLost{ false }, probeKill{ false };  // R6 step 4
	};
	inline void Apply(Features& a_f, std::string_view a_feat, bool a_ok)
	{
		auto set = [&](bool& b) { b = b && a_ok; };
		if (a_feat == "percept.hitAsk") set(a_f.hitAsk);
		else if (a_feat == "percept.cam") set(a_f.cam);
		else if (a_feat == "percept.workshop") set(a_f.workshop);
		else if (a_feat == "percept.killRole") set(a_f.killRole);
		else if (a_feat == "percept.immobile") set(a_f.immobile);
		else if (a_feat == "tele.jump") set(a_f.teleJump);
		else if (a_feat == "tele.lost") set(a_f.teleLost);
		else if (a_feat == "probe.kill") set(a_f.probeKill);
	}
	// Runs every proof with the reader; a_log(name, id, rva, ok, why, feat) per proof. All features start on.
	template <class Read, class Log>
	Features RunAll(Read&& a_read, Log&& a_log)
	{
		Features f{ true, true, true, true, true, true, true, true, true };
		for (const auto& p : kProofs) {
			std::string why;
			const bool  ok = CheckProof(p, a_read, why);
			Apply(f, p.feat, ok);
			a_log(p, ok, why);
		}
		return f;
	}
}
