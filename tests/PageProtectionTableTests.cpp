#include "common/pageProtectionTable.h"
#include "common/platform/sysVirtual.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>

#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX
#include <unistd.h>
#endif

namespace {
void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "PageProtectionTableTests: %s\n", message);
		std::abort();
	}
}

void CheckSparseTable() {
	Common::PageProtectionTable table;
	constexpr uintptr_t         high = uintptr_t {0x7f0012345000} >> 12;
	Check(table.Get(0) == -1 && table.Get(high) == -1, "empty table has permissions");
	table.Set(1023, 1024, 3);
	table.Set(high, high + 2, 0); // Known PROT_NONE is distinct from an absent page.
	Check(table.Get(1022) == -1 && table.Get(1023) == 3 && table.Get(1024) == 3 &&
	          table.Get(1025) == -1 && table.Get(high) == 0 && table.RegionCount() == 3,
	      "region boundary, high address or known no-access page was lost");
	table.Erase(1023, 1023);
	Check(table.Get(1023) == -1 && table.Get(1024) == 3 && table.RegionCount() == 2,
	      "partial erase affected a neighboring region");
	table.Set(1024, 2047, 5);
	table.Erase(1025, 2046);
	Check(table.Get(1024) == 5 && table.Get(1025) == -1 && table.Get(2047) == 5,
	      "middle erase lost region endpoints");
	table.Erase(0, std::numeric_limits<uintptr_t>::max());
	Check(table.RegionCount() == 0, "full sparse erase retained a region");
	const auto top = std::numeric_limits<uintptr_t>::max();
	table.Set(top - 2, top, 7);
	Check(table.Get(top) == 7 && table.Get(top - 3) == -1, "maximum page number wrapped");
	table.Erase(top - 1, top);
	Check(table.Get(top - 2) == 7 && table.Get(top) == -1, "maximum page erase wrapped");
	table.Erase(top - 2, top - 2);
	Check(table.RegionCount() == 0, "empty final region was not reclaimed");
}

void CheckRandomTransactions() {
	Common::PageProtectionTable table;
	std::map<uintptr_t, int>    reference;
	uint32_t                    random = 0x91abcd34;
	const auto                  next   = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
	};
	for (uint32_t iteration = 0; iteration < 10000; ++iteration) {
		const auto first = next() % 8192;
		const auto last  = first + next() % 3072;
		if (next() % 3 == 0) {
			table.Erase(first, last);
			reference.erase(reference.lower_bound(first), reference.upper_bound(last));
		} else {
			const auto mode = next() & 7;
			table.Set(first, last, mode);
			for (uintptr_t page = first; page <= last; ++page)
				reference[page] = mode;
		}
		for (const auto page: {first - uintptr_t(first != 0), uintptr_t(first), uintptr_t(last),
		                       uintptr_t(last + 1), uintptr_t(next() % 12288)}) {
			const auto found = reference.find(page);
			Check(table.Get(page) == (found == reference.end() ? -1 : found->second),
			      "random range update differs from per-page map");
		}
	}
	for (uintptr_t page = 0; page < 12288; ++page) {
		const auto found = reference.find(page);
		Check(table.Get(page) == (found == reference.end() ? -1 : found->second),
		      "final full comparison differs from per-page map");
	}
}

void CheckPlatformProtection() {
#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX
	// This metadata table serves the POSIX backend. Use the host page size on macOS too.
	const auto page = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
	using namespace Common;
	using Mode = VirtualMemory::Mode;
	SysVirtualInit();
	const auto address = SysVirtualAllocAligned(0, (page * 9), Mode::ReadWrite, (page * 16));
	Check(address && address != UINT64_MAX, "platform allocation failed");
	*reinterpret_cast<volatile uint32_t*>(address) = 0xabcddcba;
	Mode old                                       = Mode::NoAccess;
	Check(SysVirtualProtect(address + 1, page, Mode::Read, &old) && old == Mode::ReadWrite,
	      "unaligned protection lost prior permissions");
	Check(SysVirtualProtect(address + page, 1, Mode::ReadWrite, &old) && old == Mode::Read,
	      "rounded second page did not retain its permissions");
	Check(SysVirtualProtect(address + (page * 2), 1, Mode::ReadWrite, &old) &&
	          old == Mode::ReadWrite,
	      "protection changed an adjacent page");
	Check(SysVirtualProtect(address, page, Mode::ReadWrite, &old) && old == Mode::Read &&
	          *reinterpret_cast<volatile uint32_t*>(address) == 0xabcddcba,
	      "restoring access lost permissions or memory contents");
	Check(SysVirtualFreeRange(address + (page * 4), page), "middle unmap failed");
	Check(!SysVirtualProtect(address + (page * 4), page, Mode::Read, &old) && old == Mode::NoAccess,
	      "unmapped page retained metadata or mprotect succeeded");
	Check(SysVirtualAllocFixed(address + (page * 4), page, Mode::NoAccess), "middle remap failed");
	Check(SysVirtualProtect(address + (page * 4), page, Mode::ReadWrite, &old) &&
	          old == Mode::NoAccess,
	      "new allocation inherited retired permissions");
	*reinterpret_cast<volatile uint32_t*>(address + (page * 4)) = 0x12345678;
	Check(SysVirtualFreeRange(address, (page * 9)), "split allocation release failed");
#endif
}
} // namespace

int main() {
	CheckSparseTable();
	CheckRandomTransactions();
	CheckPlatformProtection();
	std::puts("PageProtectionTableTests: all cases passed");
}
