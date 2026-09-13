#include "loader/demonsSoulsIdle.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void Check(bool pass, const char* text) {
	if (!pass) {
		std::fprintf(stderr, "idle wait: %s\n", text);
		std::abort();
	}
}
} // namespace

int main() {
	using namespace Loader::DemonsSoulsIdle;
	Check(Matches(CallBytes, PollBytes), "known version signatures");
	auto changed = CallBytes;
	changed[20] ^= 1;
	Check(!Matches(changed, PollBytes), "modified code beyond the call must be rejected");
	Check(!Matches(CallBytes, std::span(PollBytes).first(5)), "short poll signature");
#if defined(__x86_64__) || defined(_M_X64)
	using namespace Xbyak::util;
	uint32_t             waits = 0;
	Xbyak::CodeGenerator code(4096);
	const auto*          sleep = code.getCurr();
	code.mov(rax, reinterpret_cast<uint64_t>(&waits));
	code.inc(dword[rax]);
	for (const auto& r: {rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11})
		code.xor_(r, r);
	code.pxor(xmm0, xmm0);
	code.pxor(xmm15, xmm15);
	code.ret();
	const auto* poll = code.getCurr();
	code.mov(rax, UINT64_C(0x1122334455667700));
	code.or_(rax, rdi); // The success value is AL, not the full return register.
	unsigned i = 1;
	for (const auto& r: {rcx, rdx, rsi, rdi, r8, r9, r10, r11})
		code.mov(r, 0xabcdef00u + i++);
	code.movq(xmm0, rax);
	code.pcmpeqd(xmm15, xmm15);
	code.ret();
	const auto* thunk = code.getCurr();
	EmitThunk(code, poll, sleep);
	// SysV runner: (output, busy, target). Save host callee-saved registers,
	// including Win64's nonvolatile XMM registers through the compiler bridge.
	const auto* run = code.getCurr();
	code.push(r12);
	code.mov(r12, rdi);
	code.mov(rdi, rsi);
	code.call(rdx);
	unsigned offset = 0;
	for (const auto& r: {rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11}) {
		code.mov(qword[r12 + offset], r);
		offset += 8;
	}
	code.movdqu(ptr[r12 + offset], xmm0);
	code.movdqu(ptr[r12 + offset + 16], xmm15);
	code.pop(r12);
	code.ret();
	code.ready();
	Check(!Xbyak::GetError(), "thunk generation");
	using Run         = KYTY_SYSV_ABI void (*)(void*, uint64_t, const void*);
	const auto runner = reinterpret_cast<Run>(reinterpret_cast<uintptr_t>(run));
	for (uint64_t busy: {0, 1}) {
		std::array<uint8_t, 104> expected {}, actual {};
		runner(expected.data(), busy, poll);
		const auto before = waits;
		runner(actual.data(), busy, thunk);
		Check(expected == actual, "poll GPR and SIMD state must survive the host callback");
		Check(waits == before + (busy == 0), "only an empty poll may wait");
	}
#endif
	std::puts("Demon's Souls idle wait tests passed");
}
