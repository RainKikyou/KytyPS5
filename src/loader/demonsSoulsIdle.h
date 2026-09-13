#pragma once

#include "common/abi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#if defined(__x86_64__) || defined(_M_X64)
#ifndef XBYAK_NO_EXCEPTION
#define XBYAK_NO_EXCEPTION
#endif
#include <xbyak/xbyak.h>
#endif

namespace Loader {
struct Program;
namespace DemonsSoulsIdle {
void Install(Program* program);
void Clear();

inline constexpr uint64_t                CallOffset = 0x83b59b, PollOffset = 0x83b880;
inline constexpr std::array<uint8_t, 24> CallBytes {0xe8, 0xe0, 0x02, 0x00, 0x00, 0x84, 0xc0, 0x74,
                                                    0xd7, 0x4c, 0x8b, 0x7d, 0xc8, 0x49, 0x8b, 0xb7,
                                                    0x90, 0x00, 0x00, 0x00, 0x48, 0x85, 0xf6, 0x74};
inline constexpr std::array<uint8_t, 24> PollBytes {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                                    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xec,
                                                    0x88, 0x00, 0x00, 0x00, 0x4c, 0x8b, 0x35, 0x25};

inline bool Matches(std::span<const uint8_t> call, std::span<const uint8_t> poll) {
	return std::ranges::equal(call, CallBytes) && std::ranges::equal(poll, PollBytes);
}

#if defined(__x86_64__) || defined(_M_X64)
// The poll's observable register state is retained even on the idle path.
// In particular this is not an ordinary C++ function replacement: its caller
// may rely on registers the original poll was known not to modify.
inline void EmitThunk(Xbyak::CodeGenerator& c, const void* poll, const void* sleep) {
	using namespace Xbyak::util;
	Xbyak::Label done;
	c.sub(rsp, 8);
	c.call(poll);
	c.add(rsp, 8);
	c.test(al, al);
	c.jnz(done, Xbyak::CodeGenerator::T_NEAR);
	c.pushfq();
	for (const auto& reg: {rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11})
		c.push(reg);
	// Entry RSP is 8 mod 16; the 80-byte save above retains that alignment.
	c.sub(rsp, 520);
	constexpr uint8_t save_fp[] {0x48, 0x0f, 0xae, 0x04, 0x24}; // FXSAVE64 [rsp]
	c.db(save_fp, sizeof(save_fp));
	c.mov(rax, reinterpret_cast<uint64_t>(sleep));
	c.call(rax); // Explicit SysV callback bridges to the platform sleep API.
	c.fxrstor64(ptr[rsp]);
	c.add(rsp, 520);
	for (const auto& reg: {r11, r10, r9, r8, rdi, rsi, rdx, rcx, rax})
		c.pop(reg);
	c.popfq();
	c.L(done);
	c.ret();
}
#endif
} // namespace DemonsSoulsIdle
} // namespace Loader
