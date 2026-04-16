#pragma once

#include "build/compiler_kind.hpp"
#include "gui/rate_limit_tracker.hpp"
#include <tge/non_copyable.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <future>
#include <atomic>
#include <mutex>

namespace Ctrn
{
struct SCompilerVersions final
{
	std::string baseUrl;
	std::vector<std::string> branches;
	std::vector<std::string> tags;
	std::vector<std::string> versions; // Combined list (for backwards compatibility)
	std::string lastUpdated;           // ISO timestamp
};

class CVersionManager final : private Tge::SNoCopyNoMove
{
public:

	CVersionManager() = default;
	~CVersionManager();

	void Initialize();

	std::vector<std::string> GetVersions(ECompilerKind kind) const;
	std::vector<std::string> GetBranches(ECompilerKind kind) const;
	std::vector<std::string> GetTags(ECompilerKind kind) const;
	std::string GetBaseUrl(ECompilerKind kind) const;
	std::string ConstructUrl(ECompilerKind kind, std::string_view version) const;

	void RefreshVersionsAsync(ECompilerKind kind, std::function<void(bool success, std::string const& error)> callback = nullptr);

	[[nodiscard]] bool IsRefreshing(ECompilerKind kind) const;

	void SetGitHubToken(std::string_view token);
	std::string GetGitHubToken() const;
	[[nodiscard]] bool HasGitHubToken() const;
	void ClearGitHubToken();

	CRateLimitTracker& GetRateLimitTracker() const { return m_rateLimitTracker; }

	std::string GetLastSyncTime(ECompilerKind kind) const;
	std::string GetLastSyncDisplay(ECompilerKind kind) const;

	[[nodiscard]] bool LoadFromCache();
	[[nodiscard]] bool SaveToCache() const;

private:

	std::string GetConfigFilePath() const;
	std::string GetCacheDirectory() const;

	void FetchGitHubVersions(std::string_view owner, std::string_view repo,
	                        std::vector<std::string>& branches, std::vector<std::string>& tags) const;
	std::vector<std::string> CombineVersions(std::vector<std::string> const& branches, std::vector<std::string> const& tags) const;

	std::expected<std::string, std::string> HttpGet(std::string_view url) const;

	void RefreshWorker(ECompilerKind kind, std::function<void(bool, std::string const&)> callback);

	std::unordered_map<ECompilerKind, SCompilerVersions> m_compilerVersions;
	mutable std::mutex m_dataMutex;

	std::string m_githubToken; // GitHub personal access token (optional)

	// Internal helpers (assume mutex is already held)
	void SetGitHubTokenInternal(std::string_view token);
	std::string GetLastSyncTimeInternal(ECompilerKind kind) const;

	mutable CRateLimitTracker m_rateLimitTracker;

	std::map<ECompilerKind, std::atomic<bool>> m_refreshInProgress;
	std::vector<std::future<void>> m_refreshFutures;

	void InitializeDefaults();
};
} // namespace Ctrn
