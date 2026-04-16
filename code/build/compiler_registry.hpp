#pragma once

#include "build/compiler.hpp"

#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Ctrn
{
class CCompilerRegistry final : private Tge::SNoCopyNoMove
{
public:

	CCompilerRegistry() = default;
	~CCompilerRegistry() = default;

	// Sets config path from g_dataDir, loads saved custom compilers.
	void Initialize();

	// Saves config.
	void Terminate();

	// Full PATH sweep — rebuilds system entries, preserves isRemovable=true entries.
	void Scan();

	// Scan a specific directory (and its bin/ subdir) for compiler executables.
	// Found compilers are added with isRemovable=true.
	// Returns the list of newly found compilers so the caller can present them.
	std::vector<SCompiler> ScanDirectory(std::string_view dir);

	[[nodiscard]] std::vector<SCompiler> const& GetCompilers() const;
	[[nodiscard]] bool Contains(std::string_view path) const;

	// Adds a single compiler by path (isRemovable=true). No-op if already registered or path absent.
	void AddCompiler(std::string_view path);

	// Removes a compiler by path.
	void RemoveCompiler(std::string_view path);

	bool LoadConfig();
	bool SaveConfig();

private:

	std::vector<SCompiler> m_compilers;
	std::string m_configFilePath;

	[[nodiscard]] std::string DetectVersion(std::string_view path, ECompilerKind kind) const;
	[[nodiscard]] bool IsAlreadyRegistered(std::string_view path) const;
	[[nodiscard]] SCompiler BuildCompilerEntry(std::string_view path, bool isRemovable) const;
};

extern CCompilerRegistry g_compilerRegistry;

// Derives the C compiler path from a C++ compiler path:
// /usr/bin/g++-13 → /usr/bin/gcc-13,  /path/to/clang++ → /path/to/clang
[[nodiscard]] std::string GetHostCompilerCPath(std::string_view cppCompilerPath);

} // namespace Ctrn
