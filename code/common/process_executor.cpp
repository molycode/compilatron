#include "common/process_executor.hpp"
#include <format>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <sstream>
#include <vector>

extern char** environ;

namespace Ctrn
{

//////////////////////////////////////////////////////////////////////////
CProcessExecutor::SProcessResult CProcessExecutor::Execute(
	std::string_view command,
	std::string_view workingDir,
	OutputCallback callback)
{
	return ExecuteArgs("/bin/sh", {"-c", std::string(command)}, workingDir, std::move(callback));
}

//////////////////////////////////////////////////////////////////////////
CProcessExecutor::SProcessResult CProcessExecutor::ExecuteArgs(
	std::string_view executable,
	std::vector<std::string> const& args,
	std::string_view workingDir,
	OutputCallback callback)
{
	std::vector<std::unique_ptr<char[]>> argStorage;
	std::vector<char*> argv;

	auto addArg = [&](std::string_view s)
	{
		argStorage.emplace_back(new char[s.size() + 1]);
		s.copy(argStorage.back().get(), s.size());
		argStorage.back().get()[s.size()] = '\0';
		argv.push_back(argStorage.back().get());
	};

	addArg(executable);

	for (auto const& arg : args)
	{
		addArg(arg);
	}

	argv.push_back(nullptr);

	return ExecuteInternal(argv[0], argv.data(), environ, workingDir, callback);
}

//////////////////////////////////////////////////////////////////////////
CProcessExecutor::SProcessResult CProcessExecutor::ExecuteInternal(
	char const* executable,
	char* const* argv,
	char* const* envp,
	std::string_view workingDir,
	OutputCallback const& callback)
{
	SProcessResult result;

	// Single pipe — both stdout and stderr merged into read end [0], write end [1]
	int pipeFds[2];

	if (pipe(pipeFds) == -1)
	{
		result.errorMessage = "Failed to create pipe";
		return result;
	}

	posix_spawn_file_actions_t fileActions;
	posix_spawn_file_actions_init(&fileActions);

	// Redirect both stdout and stderr to the write end of our pipe
	posix_spawn_file_actions_adddup2(&fileActions, pipeFds[1], STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&fileActions, pipeFds[1], STDERR_FILENO);
	posix_spawn_file_actions_addclose(&fileActions, pipeFds[0]);
	posix_spawn_file_actions_addclose(&fileActions, pipeFds[1]);

	// Place the child in its own process group so kill(-pid) reaches all descendants
	posix_spawnattr_t spawnAttr;
	posix_spawnattr_init(&spawnAttr);
	posix_spawnattr_setflags(&spawnAttr, POSIX_SPAWN_SETPGROUP);
	posix_spawnattr_setpgroup(&spawnAttr, 0); // 0 = child's own pid becomes the pgid

	if (!workingDir.empty())
	{
		if (chdir(std::string(workingDir).c_str()) != 0)
		{
			result.errorMessage = std::format("Failed to change directory to: {}", workingDir);
			posix_spawn_file_actions_destroy(&fileActions);
			posix_spawnattr_destroy(&spawnAttr);
			close(pipeFds[0]);
			close(pipeFds[1]);
			return result;
		}
	}

	pid_t pid;
	int const spawnErr{ posix_spawn(&pid, executable, &fileActions, &spawnAttr, argv, envp) };

	posix_spawn_file_actions_destroy(&fileActions);
	posix_spawnattr_destroy(&spawnAttr);
	close(pipeFds[1]); // Close write end in parent — child has its own copy via dup2

	if (spawnErr != 0)
	{
		result.errorMessage = std::string("posix_spawn failed: ") + strerror(spawnErr);
		close(pipeFds[0]);
		return result;
	}

	char buffer[4096];
	std::ostringstream accumulated;
	bool cancelled{ false };
	bool done{ false };
	int status{ -1 };

	// Use poll+WNOHANG so we detect when the main process exits even while
	// parallel child processes (e.g. make -jN jobs) still hold the pipe write
	// end open — a blocking read() would hang indefinitely in that case.
	while (!done)
	{
		pollfd pfd{ pipeFds[0], POLLIN, 0 };
		poll(&pfd, 1, 50);

		if ((pfd.revents & POLLIN) != 0)
		{
			ssize_t const bytesRead{ read(pipeFds[0], buffer, sizeof(buffer)) };

			if (bytesRead > 0)
			{
				if (callback != nullptr)
				{
					if (!callback(std::string_view(buffer, static_cast<size_t>(bytesRead))))
					{
						kill(-pid, SIGTERM);
						cancelled = true;
						done = true;
					}
				}
				else
				{
					accumulated.write(buffer, bytesRead);
				}
			}
			else
			{
				done = true; // EOF: all pipe writers have closed naturally
			}
		}
		else if (!done && callback != nullptr)
		{
			// Poll timeout with no data — probe callback for cancellation so silent
			// processes (e.g. ninja in a quiet compile phase) are killed promptly
			if (!callback({}))
			{
				kill(-pid, SIGTERM);
				cancelled = true;
				done = true;
			}
		}

		if (!done)
		{
			// Check if the main process has exited. If so, kill the process group
			// to release any parallel child processes still holding the pipe write end.
			int childStatus{ 0 };
			int const waited{ waitpid(pid, &childStatus, WNOHANG) };

			if (waited > 0)
			{
				status = childStatus;
				kill(-pid, SIGKILL);
				done = true;
			}
		}
	}

	// Drain output buffered before the process group was killed
	{
		bool draining{ true };

		while (draining)
		{
			pollfd drainPfd{ pipeFds[0], POLLIN, 0 };
			poll(&drainPfd, 1, 0);

			if ((drainPfd.revents & POLLIN) != 0)
			{
				ssize_t const bytesRead{ read(pipeFds[0], buffer, sizeof(buffer)) };

				if (bytesRead > 0)
				{
					if (callback != nullptr)
					{
						callback(std::string_view(buffer, static_cast<size_t>(bytesRead)));
					}
					else
					{
						accumulated.write(buffer, bytesRead);
					}
				}
				else
				{
					draining = false;
				}
			}
			else
			{
				draining = false;
			}
		}
	}

	close(pipeFds[0]);

	if (status == -1)
	{
		// Cancelled path: main process not yet reaped — wait for it to exit after SIGTERM
		waitpid(pid, &status, 0);
	}

	if (cancelled)
	{
		result.exitCode = -1;
		result.errorMessage = "Process cancelled";
	}
	else
	{
		result.output = accumulated.str();
		result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		result.success = (result.exitCode == 0);

		if (!result.success && result.errorMessage.empty())
		{
			result.errorMessage = std::format("Process exited with code {}", result.exitCode);
		}
	}

	return result;
}

} // namespace Ctrn
