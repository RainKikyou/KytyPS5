#include "loader/demonsSoulsIdle.h"

#include "common/logging/log.h"
#include "common/threads.h"
#include "common/virtualMemory.h"
#include "graphics/host_gpu/hostMemory.h"
#include "graphics/host_gpu/renderer/demonsSouls.h"
#include "kernel/memory.h"
#include "loader/runtimeLinker.h"

#include <algorithm>
#include <cstring>

namespace Loader::DemonsSoulsIdle {
namespace {
constexpr uint64_t     PageSize = 0x4000, CaveOffset = 0x8000000;
uint64_t               cave = 0, site = 0;
std::array<uint8_t, 5> installed_call {};

void KYTY_SYSV_ABI WaitForWork() {
	Common::Thread::SleepMicroWithoutSpinning(50);
}
} // namespace

void Install(Program* program) {
#if defined(__x86_64__) || defined(_M_X64)
	if (cave != 0 || program == nullptr || !Libs::Graphics::DemonsSouls::IsSupportedGame() ||
	    program->file_name.filename() != "eboot.bin" ||
	    program->mapped_size < PollOffset + PollBytes.size() || program->mapped_size > CaveOffset ||
	    program->base_vaddr > UINT64_MAX - CaveOffset - PageSize)
		return;
	const auto                            call = program->base_vaddr + CallOffset;
	const auto                            poll = program->base_vaddr + PollOffset;
	std::array<uint8_t, CallBytes.size()> call_bytes {};
	std::array<uint8_t, PollBytes.size()> poll_bytes {};
	// Executable modules are private runtime allocations, not GPU backing aliases.
	if (!Libs::Graphics::HostMemoryRangeIsReadable(call, call_bytes.size()) ||
	    !Libs::Graphics::HostMemoryRangeIsReadable(poll, poll_bytes.size()))
		return;
	std::memcpy(call_bytes.data(), reinterpret_cast<const void*>(call), call_bytes.size());
	std::memcpy(poll_bytes.data(), reinterpret_cast<const void*>(poll), poll_bytes.size());
	if (!Matches(call_bytes, poll_bytes)) {
		LOGF("Demon's Souls idle wait: code signature differs; retaining guest code\n");
		return;
	}
	const auto requested = program->base_vaddr + CaveOffset;
	const auto allocated = Libs::LibKernel::Memory::AllocateRuntimeMemory(
	    requested, PageSize, Common::VirtualMemory::Mode::ExecuteReadWrite,
	    "demons_souls_idle_wait", true);
	if (allocated != requested) {
		if (allocated) Libs::LibKernel::Memory::FreeGuestMemory(allocated, PageSize);
		return;
	}
	Xbyak::ClearError();
	Xbyak::CodeGenerator code(PageSize, reinterpret_cast<void*>(allocated));
	EmitThunk(code, reinterpret_cast<const void*>(poll),
	          reinterpret_cast<const void*>(&WaitForWork));
	code.ready();
	if (Xbyak::GetError() ||
	    !Common::VirtualMemory::FlushInstructionCache(allocated, code.getSize()) ||
	    !Libs::LibKernel::Memory::ProtectGuestMemory(
	        allocated, PageSize, Common::VirtualMemory::Mode::ExecuteRead, nullptr)) {
		Libs::LibKernel::Memory::FreeGuestMemory(allocated, PageSize);
		return;
	}
	installed_call[0]       = 0xe8;
	const auto displacement = static_cast<int32_t>(CaveOffset - CallOffset - 5);
	std::memcpy(installed_call.data() + 1, &displacement, sizeof(displacement));
	// Installation occurs before module initializers or guest worker threads run.
	// An external patch with different bytes is deliberately left untouched.
	// SetProgramMemoryProtection keeps executable code writable for loader patches.
	std::memcpy(reinterpret_cast<void*>(call), installed_call.data(), installed_call.size());
	if (!Common::VirtualMemory::FlushInstructionCache(call, installed_call.size())) {
		std::memcpy(reinterpret_cast<void*>(call), CallBytes.data(), installed_call.size());
		Common::VirtualMemory::FlushInstructionCache(call, installed_call.size());
		Libs::LibKernel::Memory::FreeGuestMemory(allocated, PageSize);
		return;
	}
	cave = allocated;
	site = call;
	LOGF("Demon's Souls idle wait: installed portable 50 us backoff\n");
#endif
}

void Clear() {
	if (!cave) return;
	std::array<uint8_t, 5> bytes {};
	std::memcpy(bytes.data(), reinterpret_cast<const void*>(site), bytes.size());
	if (bytes == installed_call) {
		std::memcpy(reinterpret_cast<void*>(site), CallBytes.data(), bytes.size());
		Common::VirtualMemory::FlushInstructionCache(site, bytes.size());
	}
	Libs::LibKernel::Memory::FreeGuestMemory(cave, PageSize);
	cave = site = 0;
}
} // namespace Loader::DemonsSoulsIdle
