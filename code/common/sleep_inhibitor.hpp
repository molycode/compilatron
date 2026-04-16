#pragma once

#include <tge/non_copyable.hpp>

namespace Ctrn
{

// Prevents automatic system suspend for the lifetime of this object.
// On Linux, acquires a systemd sleep-inhibitor lock via systemd-inhibit.
// No-op on other platforms or when systemd-inhibit is unavailable.
class CSleepInhibitor final : private Tge::SNoCopyNoMove
{
public:

	CSleepInhibitor() = default;
	~CSleepInhibitor();

	void Acquire();
	void Release();

private:

#ifdef CTRN_PLATFORM_LINUX
	int m_pid{ -1 };
#endif
};

} // namespace Ctrn
