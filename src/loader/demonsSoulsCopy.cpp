#include "loader/demonsSoulsCopy.h"

#include "common/logging/log.h"
#include "common/virtualMemory.h"
#include "graphics/host_gpu/hostMemory.h"
#include "graphics/host_gpu/renderer/demonsSouls.h"
#include "kernel/memory.h"
#include "loader/runtimeLinker.h"

#include <xxhash.h>

namespace Loader::DemonsSoulsCopy {
namespace {
uint64_t                site = 0;
std::array<uint8_t, 13> original {}, patch {};
void* KYTY_SYSV_ABI     CoherentMove(void* destination, const void* source, size_t size) {
    return Move(destination, source, size, &Libs::LibKernel::Memory::TryPrepareHostWrite);
}
} // namespace

void Install(Program* program) {
#if defined(__x86_64__) || defined(_M_X64)
	constexpr uint64_t offset = 0x3c36, verify_size = 2048;
	if (site || !program || !Libs::Graphics::DemonsSouls::IsSupportedGame() ||
	    program->file_name.filename() != "libc.prx" ||
	    program->mapped_size < offset + verify_size ||
	    program->base_vaddr > UINT64_MAX - offset - verify_size)
		return;
	const auto address = program->base_vaddr + offset;
	if (!Libs::Graphics::HostMemoryRangeIsReadable(address, verify_size)) return;
	const auto* bytes = reinterpret_cast<const uint8_t*>(address);
	// Hash the complete audited shared memmove/bcopy body, not just the prologue.
	// This entry follows the guest's argument/stack checks and has one saved RBP.
	// Changed module versions, relocations, or platform loader patches fail closed.
	if (XXH3_64bits(bytes, verify_size) != 0xe654325b8be848a9ull) return;
	std::memcpy(original.data(), bytes, original.size());
	patch = Tailcall(reinterpret_cast<uint64_t>(&CoherentMove));
	std::memcpy(reinterpret_cast<void*>(address), patch.data(), patch.size());
	if (!Common::VirtualMemory::FlushInstructionCache(address, patch.size())) {
		std::memcpy(reinterpret_cast<void*>(address), original.data(), original.size());
		Common::VirtualMemory::FlushInstructionCache(address, original.size());
		return;
	}
	site = address;
	LOGF("Demon's Souls copy: installed verified coherent memmove\n");
#endif
}

void Clear() {
	if (!site) return;
	if (std::memcmp(reinterpret_cast<const void*>(site), patch.data(), patch.size()) == 0) {
		std::memcpy(reinterpret_cast<void*>(site), original.data(), original.size());
		Common::VirtualMemory::FlushInstructionCache(site, original.size());
	}
	site = 0;
}
} // namespace Loader::DemonsSoulsCopy
