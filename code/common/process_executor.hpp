#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Ctrn
{
class CProcessExecutor final
{
public:

	struct SProcessResult final
	{
		int exitCode{};
		std::string output;       // merged stdout + stderr
		bool success{};
		std::string errorMessage; // executor-level failure only (spawn failed, pipe error, etc.)
	};

	// Return false from callback to cancel (sends SIGTERM to child process)
	using OutputCallback = std::function<bool(std::string_view chunk)>;

	[[nodiscard]] static SProcessResult Execute(
		std::string_view command,
		std::string_view workingDir = {},
		OutputCallback callback = nullptr);

	[[nodiscard]] static SProcessResult ExecuteArgs(
		std::string_view executable,
		std::vector<std::string> const& args,
		std::string_view workingDir = {},
		OutputCallback callback = nullptr);

private:

	static SProcessResult ExecuteInternal(
		char const* executable,
		char* const* argv,
		char* const* envp,
		std::string_view workingDir,
		OutputCallback const& callback);
};
} // namespace Ctrn
