#include "common/profiler.h"

#include "common/emulatorConfig.h"

#include <common/TracyProtocol.hpp>
#include <common/TracyVersion.hpp>
#include <cstdio>
#include <tracy/Tracy.hpp>

namespace Profiler {

void SetThreadName(const char* name) {
	if (tracy::ProfilerAvailable() && name != nullptr) {
		tracy::SetThreadName(name);
	}
}

void Initialize() {
	switch (Config::GetProfilerDirection()) {
		case Config::ProfilerDirection::Network:
			if (!tracy::ProfilerAvailable()) {
				tracy::StartupProfiler();
				TracySetProgramName("KytyPS5");
				::printf("Tracy profiler enabled: client %d.%d.%d, protocol %u, "
				         "broadcast %u, connect to 127.0.0.1:8086\n",
				         tracy::Version::Major, tracy::Version::Minor, tracy::Version::Patch,
				         tracy::ProtocolVersion, tracy::BroadcastVersion);
			}
			break;
		case Config::ProfilerDirection::None:
		default: break;
	}
}

void Shutdown() {
	if (tracy::ProfilerAvailable()) {
		tracy::ShutdownProfiler();
	}
}

} // namespace Profiler
