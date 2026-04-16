#include "common/sleep_inhibitor.hpp"
#include "common/loggers.hpp"

#ifdef CTRN_PLATFORM_LINUX
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif // CTRN_PLATFORM_LINUX

namespace Ctrn
{

//////////////////////////////////////////////////////////////////////////
CSleepInhibitor::~CSleepInhibitor()
{
	Release();
}

//////////////////////////////////////////////////////////////////////////
void CSleepInhibitor::Acquire()
{
#ifdef CTRN_PLATFORM_LINUX
	if (m_pid != -1)
	{
		return;
	}

	pid_t const pid{ fork() };

	if (pid == 0)
	{
		// Child: run systemd-inhibit holding a delay lock against sleep.
		// "sleep infinity" keeps the child alive until we send SIGTERM.
		char const* args[] = {
			"systemd-inhibit",
			"--what=sleep",
			"--who=Compilatron",
			"--why=Building compiler",
			"--mode=delay",
			"sleep",
			"infinity",
			nullptr
		};
		execvp("systemd-inhibit", const_cast<char* const*>(args));
		// execvp only returns on failure — systemd-inhibit unavailable, exit silently
		_exit(1);
	}

	if (pid > 0)
	{
		m_pid = static_cast<int>(pid);
		gLog.Info(Tge::Logging::ETarget::File, "Sleep inhibitor acquired (PID: {})", m_pid);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "Failed to acquire sleep inhibitor: {}", strerror(errno));
	}
#endif // CTRN_PLATFORM_LINUX
}

//////////////////////////////////////////////////////////////////////////
void CSleepInhibitor::Release()
{
#ifdef CTRN_PLATFORM_LINUX
	if (m_pid == -1)
	{
		return;
	}

	kill(static_cast<pid_t>(m_pid), SIGTERM);
	waitpid(static_cast<pid_t>(m_pid), nullptr, 0);
	gLog.Info(Tge::Logging::ETarget::File, "Sleep inhibitor released (PID: {})", m_pid);
	m_pid = -1;
#endif // CTRN_PLATFORM_LINUX
}

} // namespace Ctrn
