// OWNER: TESTS lane, round 6 step 5. All step-5 desktop cases: include once and call s5_tests::Run(). Needs CHECK, g_fail,
// g_pass and g_quick from the host (tests/s5_tests.cpp).
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace s5_tests
{
	struct Row
	{
		int         pass{ 0 }, fail{ 0 };
		std::string firstFail, last;
	};
	inline std::map<std::string, Row>& Rows()
	{
		static std::map<std::string, Row> r;
		return r;
	}
	// the case id = the first word of the message ("S5-B1 ...")
	inline bool Note(bool a_ok, const char* a_fmt, ...)
	{
		char    b[1024];
		va_list ap;
		va_start(ap, a_fmt);
		std::vsnprintf(b, sizeof b, a_fmt, ap);
		va_end(ap);
		std::string msg(b), id = msg.substr(0, msg.find(' '));
		Row&        row = Rows()[id];
		(a_ok ? row.pass : row.fail)++;
		if (!a_ok && row.firstFail.empty()) row.firstFail = msg;
		row.last = msg;
		CHECK(a_ok, "%s", b);
		return a_ok;
	}
}
#define S5CHECK(cond, ...) s5_tests::Note(static_cast<bool>(cond), __VA_ARGS__)

#include "s5_body.inc"
#include "s5_core.inc"
#include "s5_fix2.inc"  // s5 fix lane 2 (REVIEW W2): the backstop only behind a veto, the documented switches, the load window

namespace s5_tests
{
	inline void Run()
	{
		std::printf("s5_tests: core = %s, Body::AddVelocity = %s\n", s5api::Impl(), s5api::kBodyAddVelocity ? "present" : "absent (AddImpulse(m x dv))");
		s5_core::Run();
		s5_body::Run();
		s5_fix2::Run();
	}
	inline void WriteReport(const char* a_path)
	{
		std::ofstream o(a_path, std::ios::binary);
		o << "| case | result | checks | detail |\n|---|---|---|---|\n";
		for (const auto& [id, r] : Rows()) {
			std::string d = r.fail ? r.firstFail : r.last;
			for (char& ch : d)
				if (ch == '|') ch = '/';
			o << "| " << id << " | " << (r.fail ? "FAIL" : "PASS") << " | " << r.pass << " / " << (r.pass + r.fail) << " | " << d << " |\n";
		}
		o << "\ns5_tests: " << g_pass << " passed, " << g_fail << " failed\n";
	}
}
