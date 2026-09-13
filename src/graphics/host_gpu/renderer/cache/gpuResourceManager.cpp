#include "graphics/host_gpu/renderer/cache/gpuResourceManager.h"

#include "common/assert.h"
#include "graphics/guest_gpu/graphicsRun.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "kernel/memory.h"
namespace Libs::Graphics {

GpuResourceManager::GpuResourceManager(GraphicContext& graphics, CommandScheduler& scheduler)
    : m_scheduler(scheduler), m_buffer_cache(graphics, scheduler, m_page_manager, m_texture_cache),
      m_texture_cache(graphics, scheduler, m_page_manager, m_buffer_cache) {}

GpuResourceManager::~GpuResourceManager() = default;

void GpuResourceManager::BeginAliasWrite() noexcept {
	{
		const auto old = m_preparation_alias_epoch.fetch_add((uint64_t{1} << 32u) + 1u,
		    std::memory_order_acq_rel);
		EXIT_IF(uint32_t(old >> 32u) == UINT32_MAX || uint32_t(old) == UINT32_MAX);
	}
}

void GpuResourceManager::EndAliasWrite() noexcept {
	{
		const auto old = m_preparation_alias_epoch.fetch_sub(1u, std::memory_order_release);
		EXIT_IF(uint32_t(old) == 0);
	}
}

uint64_t GpuResourceManager::PreparationAliasEpoch() const noexcept {
	const auto value = m_preparation_alias_epoch.load(std::memory_order_acquire);
	return uint32_t(value) == 0 ? value : 0;
}

bool GpuResourceManager::TryInvalidateCpuWriteWindow(uint64_t fault) {
	// A small window amortizes faults from sequential CPU writes. Expanding a
	// fault is allowed only for mapped, unaliased pages with no GPU/image owner.
	constexpr uint64_t window_size  = 4 * TRACKER_PAGE_SIZE;
	const auto         window_begin = fault & ~(window_size - 1);
	GuestRange         selected {};
	uint64_t           mapping_epoch;
	{
		std::shared_lock mapped_lock(m_mapped_ranges_mutex);
		mapping_epoch = m_mapping_epoch;
		m_mapped_ranges.ForEachIntersection(window_begin, window_size, [&](RangeSet::Range range) {
			if (fault >= range.address && fault - range.address < range.size) {
				const auto begin =
				    (range.address + TRACKER_PAGE_SIZE - 1) & ~(TRACKER_PAGE_SIZE - 1);
				const auto end = (range.address + range.size) & ~(TRACKER_PAGE_SIZE - 1);
				if (begin < end) selected = {begin, end - begin};
			}
		});
	}
	if (selected.size <= TRACKER_PAGE_SIZE || fault < selected.address ||
	    fault - selected.address >= selected.size ||
	    !LibKernel::Memory::IsUniqueGuestBackingRange(selected.address, selected.size))
		return false;
	// Kernel backing queries must not run while holding the resource-map lock.
	std::shared_lock mapped_lock(m_mapped_ranges_mutex);
	if (mapping_epoch != m_mapping_epoch ||
	    !m_mapped_ranges.Contains(selected.address, selected.size))
		return false;
	return m_buffer_cache.TryInvalidateCpuWriteWindow(fault, selected.address, selected.size);
}

bool GpuResourceManager::HandleFault(PageFaultAccess access, uint64_t fault_vaddr) noexcept {
	// The host reports the faulting byte, not the instruction's access width. Both caches
	// resolve its page; guessing a width can cross the end of a valid guest mapping.
	constexpr uint64_t fault_size = 1;
	if (!IsMapped(fault_vaddr, fault_size)) {
		return false;
	}
	if (access == PageFaultAccess::Write) {
		if (TryInvalidateCpuWriteWindow(fault_vaddr)) return true;
		m_buffer_cache.InvalidateMemory(fault_vaddr, fault_size);
		m_texture_cache.InvalidateMemory(fault_vaddr, fault_size);
	} else {
		m_buffer_cache.ReadMemory(fault_vaddr, fault_size);
	}
	return true;
}

bool GpuResourceManager::InvalidateMemory(uint64_t vaddr, uint64_t size) {
	if (!IsMapped(vaddr, size)) {
		return false;
	}
	m_buffer_cache.InvalidateMemory(vaddr, size);
	m_texture_cache.InvalidateMemory(vaddr, size);
	return true;
}

bool GpuResourceManager::IsMapped(uint64_t vaddr, uint64_t size) const noexcept {
	if (!GuestRange {vaddr, size}.Valid()) {
		return false;
	}
	std::shared_lock lock(m_mapped_ranges_mutex);
	return m_mapped_ranges.Contains(vaddr, size);
}

void GpuResourceManager::MapMemory(uint64_t vaddr, uint64_t size) {
	{
		std::lock_guard lock(m_mapped_ranges_mutex);
		m_mapped_ranges.Add(vaddr, size);
		++m_mapping_epoch;
	}
}

void GpuResourceManager::UnmapMemory(uint64_t vaddr, uint64_t size) {
	if (CommandScheduler::InDeferredOperation()) {
		EXIT("unsupported memory unmap from an asynchronous GPU completion, "
		     "addr=0x%016" PRIx64 " size=0x%016" PRIx64 "\n",
		     vaddr, size);
	}
	const auto unmap = [this, vaddr, size] {
		if (m_scheduler.Active()) {
			const auto tick = m_scheduler.CurrentTick();
			m_scheduler.Finish();
			m_scheduler.WaitPriorityOperations(tick);
		}
		m_buffer_cache.InvalidateMemory(vaddr, size);
		m_texture_cache.UnmapMemory(vaddr, size);
		std::lock_guard lock(m_mapped_ranges_mutex);
		m_mapped_ranges.Subtract(vaddr, size);
		++m_mapping_epoch;
	};
	if (m_gpu == nullptr) {
		unmap();
		return;
	}
	m_gpu->SendCommandSync(unmap);
}

void GpuResourceManager::RefreshBdaRanges() {
	const auto registered = m_buffer_cache.RegistrationEpoch();
	if (m_bda_mapping_epoch == m_mapping_epoch && m_bda_registration_epoch == registered) return;
	std::vector<RangeSet::Range> ranges;
	m_buffer_cache.CollectMappedRegisteredRanges(m_mapped_ranges, ranges);
	m_bda_region_requests.clear();
	for (const auto& range : ranges) {
		const auto end = range.address + range.size;
		for (auto start = range.address; start < end;) {
			const auto finish = std::min(end, (start / TRACKER_REGION_SIZE + 1) * TRACKER_REGION_SIZE);
			m_bda_region_requests.push_back({start, finish - start});
			start = finish;
		}
	}
	m_bda_mapping_epoch = m_mapping_epoch;
	m_bda_registration_epoch = registered;
}

bool GpuResourceManager::PrepareBdaReadRanges(std::span<const GuestRange> ranges) {
	if (ranges.empty() || ranges.size() > 128 ||
	    std::ranges::any_of(ranges, [](const auto& range) { return !range.Valid(); })) return false;
	std::shared_lock lock(m_mapped_ranges_mutex);
	RefreshBdaRanges();
	for (const auto range : ranges) {
		auto it = std::lower_bound(m_bda_region_requests.begin(), m_bda_region_requests.end(), range.address,
		    [](const auto& request, uint64_t begin) { return request.address + request.size <= begin; });
		for (; it != m_bda_region_requests.end() && it->address < range.End(); ++it)
			m_buffer_cache.SynchronizeRegionRequest(*it);
	}
	m_fault_process_pending = true;
	return true;
}

void GpuResourceManager::PrepareBda() {
	std::shared_lock lock(m_mapped_ranges_mutex);
	RefreshBdaRanges();
	for (auto& request : m_bda_region_requests) m_buffer_cache.SynchronizeRegionRequest(request);
	m_fault_process_pending = true;
}

void GpuResourceManager::RunGarbageCollector() {
	if (m_fault_process_pending) {
		m_fault_process_pending = false;
		m_buffer_cache.ProcessFaultBuffer();
	}
	m_texture_cache.ProcessDownloadImages();
	m_texture_cache.RunGarbageCollector();
	m_buffer_cache.RunGarbageCollector();
}

} // namespace Libs::Graphics
