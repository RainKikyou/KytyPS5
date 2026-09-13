#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPURESOURCEMANAGER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPURESOURCEMANAGER_H_

#include "common/abi.h"
#include "common/common.h"
#include "graphics/host_gpu/pageManager.h"
#include "graphics/host_gpu/renderer/cache/bufferCache.h"
#include "graphics/host_gpu/renderer/cache/textureCache.h"

#include <cstdint>
#include <shared_mutex>

namespace Libs::Graphics {

class CommandScheduler;
class GuestGpu;

class GpuResourceManager {
public:
	GpuResourceManager(GraphicContext& graphics, CommandScheduler& scheduler);
	~GpuResourceManager();
	KYTY_CLASS_NO_COPY(GpuResourceManager);

	[[nodiscard]] BufferCache&  GetBufferCache() { return m_buffer_cache; }
	[[nodiscard]] TextureCache& GetTextureCache() { return m_texture_cache; }
	[[nodiscard]] bool          HasReadWatchers(uint64_t vaddr, uint64_t size) const noexcept {
        return m_page_manager.HasReadWatchers(vaddr, size);
	}
	void                        SetGpu(GuestGpu* gpu) noexcept { m_gpu = gpu; }

	[[nodiscard]] bool HandleFault(PageFaultAccess access, uint64_t fault_vaddr) noexcept;
	[[nodiscard]] bool InvalidateMemory(uint64_t vaddr, uint64_t size);
	[[nodiscard]] bool IsMapped(uint64_t vaddr, uint64_t size) const noexcept;
	void               MapMemory(uint64_t vaddr, uint64_t size);
	void               UnmapMemory(uint64_t vaddr, uint64_t size);
	void               PrepareBda();
	void BeginAliasWrite() noexcept;
	void EndAliasWrite() noexcept;
	[[nodiscard]] uint64_t PreparationAliasEpoch() const noexcept;
	[[nodiscard]] uint64_t MappingEpoch() const noexcept {
		std::shared_lock lock(m_mapped_ranges_mutex);
		return m_mapping_epoch;
	}

	bool PrepareBdaReadRanges(std::span<const GuestRange> ranges);
	void               RunGarbageCollector();

private:
	[[nodiscard]] bool        TryInvalidateCpuWriteWindow(uint64_t fault);
	void RefreshBdaRanges();
	PageManager               m_page_manager;
	CommandScheduler&         m_scheduler;
	BufferCache               m_buffer_cache;
	TextureCache              m_texture_cache;
	mutable std::shared_mutex m_mapped_ranges_mutex;
	RangeSet                  m_mapped_ranges;
	uint64_t m_mapping_epoch = 1, m_bda_mapping_epoch = 0, m_bda_registration_epoch = 0;
	std::vector<BufferCache::SyncRegionRequest> m_bda_region_requests;
	GuestGpu*                 m_gpu = nullptr;
	std::atomic<uint64_t> m_preparation_alias_epoch {uint64_t{1} << 32};
	bool                      m_fault_process_pending = false;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPURESOURCEMANAGER_H_
