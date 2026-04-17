#include "gui/version_manager.hpp"
#include "build/build_settings.hpp"
#include "common/loggers.hpp"
#include "common/common.hpp"
#include "common/process_executor.hpp"
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <charconv>
#include <expected>
#include <regex>
#include <chrono>
#include <array>
#include <optional>


namespace Ctrn
{
namespace fs = std::filesystem;

void CVersionManager::Initialize()
{
	InitializeDefaults();
	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: About to call LoadFromCache");

	if (!LoadFromCache())
	{
		gLog.Info(Tge::Logging::ETarget::File, "VersionManager: No cache found, will fetch on demand");
	}

	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: LoadFromCache completed");
}

//////////////////////////////////////////////////////////////////////////
CVersionManager::~CVersionManager()
{
	// Futures from std::async block on destruction — vector cleanup waits for all in-flight refreshes
	m_refreshFutures.clear();
}

//////////////////////////////////////////////////////////////////////////
void CVersionManager::InitializeDefaults()
{
	SCompilerVersions clangVersions;
	clangVersions.baseUrl = std::string{ ClangSourceUrl };
	clangVersions.versions = {"main"}; // Fallback if fetching fails

	SCompilerVersions gccVersions;
	gccVersions.baseUrl = std::string{ GccSourceUrl };
	gccVersions.versions = {"master"}; // Fallback if fetching fails

	std::lock_guard<std::mutex> lock(m_dataMutex);
	m_compilerVersions[ECompilerKind::Clang] = clangVersions;
	m_compilerVersions[ECompilerKind::Gcc] = gccVersions;

	m_refreshInProgress[ECompilerKind::Clang] = false;
	m_refreshInProgress[ECompilerKind::Gcc] = false;
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CVersionManager::GetVersions(ECompilerKind kind) const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	auto it = m_compilerVersions.find(kind);

	if (it != m_compilerVersions.end())
	{
		return it->second.versions;
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CVersionManager::GetBranches(ECompilerKind kind) const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	auto it = m_compilerVersions.find(kind);

	if (it != m_compilerVersions.end())
	{
		return it->second.branches;
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CVersionManager::GetTags(ECompilerKind kind) const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	auto it = m_compilerVersions.find(kind);

	if (it != m_compilerVersions.end())
	{
		return it->second.tags;
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::GetBaseUrl(ECompilerKind kind) const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	auto it = m_compilerVersions.find(kind);

	if (it != m_compilerVersions.end())
	{
		return it->second.baseUrl;
	}
	return "";
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::ConstructUrl(ECompilerKind kind, std::string_view version) const
{
	std::string baseUrl{ GetBaseUrl(kind) };

	if (baseUrl.empty() || version.empty())
	{
		return baseUrl;
	}

	// For GitHub URLs, ensure proper .git clone URL format
	if (baseUrl.find("github.com") != std::string::npos)
	{
		// Convert browsing URLs to clone URLs if needed
		std::string cloneUrl{ baseUrl };

		if (!cloneUrl.ends_with(".git"))
		{
			cloneUrl += ".git";
		}
		return cloneUrl;
	}

	// For other URLs, return base URL (could be enhanced for other hosting services)
	return baseUrl;
}

//////////////////////////////////////////////////////////////////////////
bool CVersionManager::IsRefreshing(ECompilerKind kind) const
{
	auto it = m_refreshInProgress.find(kind);

	if (it != m_refreshInProgress.end())
	{
		return it->second.load();
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
void CVersionManager::SetGitHubToken(std::string_view token)
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	SetGitHubTokenInternal(token);
}

//////////////////////////////////////////////////////////////////////////
void CVersionManager::SetGitHubTokenInternal(std::string_view token)
{
	// This method assumes m_dataMutex is already held
	m_githubToken = token;
	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: GitHub token {}", token.empty() ? "cleared" : "set (authentication enabled)");
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::GetGitHubToken() const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	return m_githubToken;
}

//////////////////////////////////////////////////////////////////////////
bool CVersionManager::HasGitHubToken() const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	return !m_githubToken.empty();
}

//////////////////////////////////////////////////////////////////////////
void CVersionManager::ClearGitHubToken()
{
	SetGitHubToken("");
}

//////////////////////////////////////////////////////////////////////////
void CVersionManager::RefreshVersionsAsync(ECompilerKind kind, std::function<void(bool success, std::string const& error)> callback)
{
	if (IsRefreshing(kind))
	{
		if (callback)
		{
			callback(false, "Refresh already in progress");
		}
		return;
	}

	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Starting async refresh for {}", kind == ECompilerKind::Gcc ? "gcc" : "clang");

	// Prune completed futures before launching a new one
	m_refreshFutures.erase(
		std::remove_if(m_refreshFutures.begin(), m_refreshFutures.end(),
			[](std::future<void> const& f)
			{
				return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
			}),
		m_refreshFutures.end());

	m_refreshFutures.emplace_back(std::async(std::launch::async, &CVersionManager::RefreshWorker, this, kind, callback));
}

//////////////////////////////////////////////////////////////////////////
void CVersionManager::RefreshWorker(ECompilerKind kind, std::function<void(bool, std::string const&)> callback)
{
	m_refreshInProgress[kind] = true;

	std::vector<std::string> branches;
	std::vector<std::string> tags;

	if (kind == ECompilerKind::Clang)
	{
		FetchGitHubVersions("llvm", "llvm-project", branches, tags);
	}
	else
	{
		FetchGitHubVersions("gcc-mirror", "gcc", branches, tags);
	}

	std::vector<std::string> combinedVersions = CombineVersions(branches, tags);

	if (combinedVersions.empty())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "VersionManager: No versions found for {}", kind == ECompilerKind::Gcc ? "gcc" : "clang");

		if (callback)
		{
			callback(false, "No versions found");
			RequestRedraw();
		}

		m_refreshInProgress[kind] = false;
		return;
	}

	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Fetched {} branches and {} tags ({} total) for {}", branches.size(), tags.size(), combinedVersions.size(), kind == ECompilerKind::Gcc ? "gcc" : "clang");

	if (!branches.empty())
	{
		gLog.Info(Tge::Logging::ETarget::File, "VersionManager: First few branches: {}, {}, {}...",
			branches.size() > 0 ? branches[0] : "none",
			branches.size() > 1 ? branches[1] : "none",
			branches.size() > 2 ? branches[2] : "none");
	}

	if (!tags.empty())
	{
		gLog.Info(Tge::Logging::ETarget::File, "VersionManager: First few tags: {}, {}, {}...",
			tags.size() > 0 ? tags[0] : "none",
			tags.size() > 1 ? tags[1] : "none",
			tags.size() > 2 ? tags[2] : "none");
	}
	else
	{
		gLog.Info(Tge::Logging::ETarget::File, "VersionManager: No tags found - this might indicate an issue with tag fetching");
	}

	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		auto& compilerVersions = m_compilerVersions[kind];
		compilerVersions.branches = branches;
		compilerVersions.tags = tags;
		compilerVersions.versions = combinedVersions;

		auto const now = std::chrono::system_clock::now();
		compilerVersions.lastUpdated = std::format("{:%Y-%m-%dT%T}Z", now);
	}

	if (!SaveToCache())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "Failed to save version cache after refresh for {}", kind == ECompilerKind::Gcc ? "gcc" : "clang");
	}

	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Successfully refreshed {} versions for {}", combinedVersions.size(), kind == ECompilerKind::Gcc ? "gcc" : "clang");

	if (callback)
	{
		callback(true, "");
		RequestRedraw();
	}

	m_refreshInProgress[kind] = false;
}

//////////////////////////////////////////////////////////////////////////
void CVersionManager::FetchGitHubVersions(std::string_view owner, std::string_view repo,
                                         std::vector<std::string>& branches, std::vector<std::string>& tags) const
{
	branches.clear();
	tags.clear();

	// Fetch ALL branches with pagination
	int branchPage{ 1 };
	bool hasMoreBranches{ true };
	bool branchFetchOk{ true };

	while (hasMoreBranches && branchFetchOk && branchPage <= 50) // Max 5000 branches
	{
		std::string branchesUrl{ std::format("https://api.github.com/repos/{}/{}/branches?per_page=100&page={}", owner, repo, branchPage) };
		auto branchesResult = HttpGet(branchesUrl);

		if (!branchesResult)
		{
			gLog.Warning(Tge::Logging::ETarget::File, "VersionManager: Failed to fetch branches page {}: {}", branchPage, branchesResult.error());
			branchFetchOk = false;
		}
		else
		{
			std::string const& branchesResponse = branchesResult.value();
			std::regex branchRegex("\"name\"\\s*:\\s*\"([^\"]+)\"");
			std::sregex_iterator branchIter(branchesResponse.begin(), branchesResponse.end(), branchRegex);
			std::sregex_iterator branchEnd;

			int branchesOnThisPage{ 0 };

			for (std::sregex_iterator i = branchIter; i != branchEnd; ++i)
			{
				branches.emplace_back((*i)[1].str());
				branchesOnThisPage++;
			}

			hasMoreBranches = (branchesOnThisPage == 100);
			branchPage++;
		}
	}

	// Fetch all available tags using pagination
	int page{ 1 };
	bool hasMorePages{ true };
	bool tagFetchOk{ true };

	while (hasMorePages && tagFetchOk && page <= 50) // Max 5000 tags
	{
		std::string pagedUrl{ std::format("https://api.github.com/repos/{}/{}/tags?per_page=100&page={}", owner, repo, page) };
		auto tagsResult = HttpGet(pagedUrl);

		if (!tagsResult)
		{
			gLog.Warning(Tge::Logging::ETarget::File, "VersionManager: Failed to fetch tags page {}: {}", page, tagsResult.error());
			tagFetchOk = false;
		}
		else
		{
			std::string const& tagsResponse = tagsResult.value();
			std::regex tagRegex("\"name\"\\s*:\\s*\"([^\"]+)\"");
			std::sregex_iterator tagIter(tagsResponse.begin(), tagsResponse.end(), tagRegex);
			std::sregex_iterator tagEnd;

			int tagsOnThisPage{ 0 };

			for (std::sregex_iterator i = tagIter; i != tagEnd; ++i)
			{
				tags.emplace_back((*i)[1].str());
				tagsOnThisPage++;
			}

			hasMorePages = (tagsOnThisPage == 100);
			page++;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CVersionManager::CombineVersions(std::vector<std::string> const& branches, std::vector<std::string> const& tags) const
{
	std::vector<std::string> sortedBranches = branches;
	std::vector<std::string> sortedTags = tags;

	// Sort branches: main/master first, then alphabetically
	std::sort(sortedBranches.begin(), sortedBranches.end(), [](std::string const& a, std::string const& b) {
		if (a == "main" || a == "master") return true;
		if (b == "main" || b == "master") return false;
		return a < b;
	});

	// Sort tags: reverse alphabetical to get newer versions first
	std::sort(sortedTags.begin(), sortedTags.end(), [](std::string const& a, std::string const& b) {
		return a > b;
	});

	std::vector<std::string> combined;
	combined.reserve(sortedBranches.size() + sortedTags.size());
	combined.insert(combined.end(), sortedBranches.begin(), sortedBranches.end());
	combined.insert(combined.end(), sortedTags.begin(), sortedTags.end());

	return combined;
}

//////////////////////////////////////////////////////////////////////////
std::expected<std::string, std::string> CVersionManager::HttpGet(std::string_view url) const
{
	std::string command{ "curl -s -H \"User-Agent: Compilatron/1.0\"" };

	// Add GitHub token if available (increases rate limit from 60 to 5,000 requests/hour)
	if (HasGitHubToken())
	{
		std::string token{ GetGitHubToken() };
		command += std::format(" -H \"Authorization: Bearer {}\"", token);
	}

	command += std::format(" \"{}\"", url);

	auto const curlResult = CProcessExecutor::Execute(command);

	if (!curlResult.success)
	{
		return std::unexpected("curl command failed with exit code: " + std::to_string(curlResult.exitCode));
	}

	std::string const& result = curlResult.output;

	if (result.find("API rate limit exceeded") != std::string::npos)
	{
		if (HasGitHubToken())
		{
			return std::unexpected("GitHub API rate limit exceeded even with authentication. Please wait and try again.");
		}
		else
		{
			return std::unexpected("GitHub API rate limit exceeded (60/hour). Add GitHub token for 5,000/hour limit.");
		}
	}

	if (result.find("Bad credentials") != std::string::npos)
	{
		return std::unexpected("Invalid GitHub token. Please check your token in settings.");
	}

	if (result.find("{\"message\":") == 0 && result.find("documentation_url") != std::string::npos)
	{
		std::regex messageRegex("\"message\"\\s*:\\s*\"([^\"]+)\"");
		std::smatch match;

		if (std::regex_search(result, match, messageRegex))
		{
			return std::unexpected("GitHub API error: " + match[1].str());
		}
		else
		{
			return std::unexpected("GitHub API returned an error response");
		}
	}

	m_rateLimitTracker.TrackRequest();

	if (!SaveToCache())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "Failed to save version cache after GitHub API request");
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::GetCacheDirectory() const
{
	return (fs::path(g_dataDir) / "config").string();
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::GetConfigFilePath() const
{
	return (fs::path(GetCacheDirectory()) / "compiler_versions.json").string();
}

//////////////////////////////////////////////////////////////////////////
bool CVersionManager::LoadFromCache()
{
	std::string configPath{ GetConfigFilePath() };
	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: LoadFromCache: Starting to load from {}", configPath);

	if (!fs::exists(configPath))
	{
		gLog.Info(Tge::Logging::ETarget::File, "VersionManager: No cache file found at {}", configPath);
		return false;
	}

	std::ifstream file(configPath);

	if (!file.is_open())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "VersionManager: Failed to open cache file: {}", configPath);
		return false;
	}

	int rateLimitRequests{ 0 };
	std::string rateLimitHourStart;
	std::string line;
	std::optional<ECompilerKind> currentKind;
	bool isConfigSection{ false };

	std::lock_guard<std::mutex> lock(m_dataMutex);

	while (std::getline(file, line))
	{
		if (!line.empty() && line[0] != '#')
		{
			if (line.length() >= 2 && line[0] == '[' && line.back() == ']')
			{
				std::string const section{ line.substr(1, line.length() - 2) };
				isConfigSection = (section == "config");
				currentKind = std::nullopt;

				if (section == "gcc")
				{
					currentKind = ECompilerKind::Gcc;
				}
				else if (section == "clang")
				{
					currentKind = ECompilerKind::Clang;
				}
			}
			else
			{
				size_t equalPos{ line.find('=') };

				if (equalPos != std::string::npos)
				{
					std::string key{ line.substr(0, equalPos) };
					std::string value{ line.substr(equalPos + 1) };

					if (isConfigSection)
					{
						if (key == "githubToken")
						{
							SetGitHubTokenInternal(value);
						}
						else if (key == "rateLimitRequests")
						{
							auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), rateLimitRequests);

							if (ec != std::errc{})
							{
								rateLimitRequests = 0;
							}
							else
							{
								gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Found rateLimitRequests in config: {}", rateLimitRequests);
							}
						}
						else if (key == "rateLimitHourStart")
						{
							rateLimitHourStart = value;
							gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Found rateLimitHourStart in config: {}", rateLimitHourStart);
						}
					}
					else if (currentKind.has_value())
					{
						auto& compilerVersions = m_compilerVersions[*currentKind];

						if (key == "baseUrl")
						{
							compilerVersions.baseUrl = value;
						}
						else if (key == "lastUpdated")
						{
							compilerVersions.lastUpdated = value;
						}
						else if (key == "branches")
						{
							compilerVersions.branches.clear();
							std::stringstream ss(value);
							std::string branch;

							while (std::getline(ss, branch, ','))
							{
								branch.erase(0, branch.find_first_not_of(" \t"));
								branch.erase(branch.find_last_not_of(" \t") + 1);

								if (!branch.empty())
								{
									compilerVersions.branches.emplace_back(std::move(branch));
								}
							}
						}
						else if (key == "tags")
						{
							compilerVersions.tags.clear();
							std::stringstream ss(value);
							std::string tag;

							while (std::getline(ss, tag, ','))
							{
								tag.erase(0, tag.find_first_not_of(" \t"));
								tag.erase(tag.find_last_not_of(" \t") + 1);

								if (!tag.empty())
								{
									compilerVersions.tags.emplace_back(std::move(tag));
								}
							}
						}
						else if (key == "versions")
						{
							compilerVersions.versions.clear();
							std::stringstream ss(value);
							std::string version;

							while (std::getline(ss, version, ','))
							{
								version.erase(0, version.find_first_not_of(" \t"));
								version.erase(version.find_last_not_of(" \t") + 1);

								if (!version.empty())
								{
									compilerVersions.versions.emplace_back(std::move(version));
								}
							}
						}
					}
				}
			}
		}
	}

	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Loading rate limit data from cache: {} requests, hour start: {}", rateLimitRequests, rateLimitHourStart);
	m_rateLimitTracker.SetRateLimitData(rateLimitRequests, rateLimitHourStart);

	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Successfully loaded cache from {}", configPath);
	return true;
}

//////////////////////////////////////////////////////////////////////////
bool CVersionManager::SaveToCache() const
{
	std::string cacheDir{ GetCacheDirectory() };
	std::error_code ec;

	if (!fs::exists(cacheDir))
	{
		fs::create_directories(cacheDir, ec);

		if (ec)
		{
			gLog.Warning(Tge::Logging::ETarget::File, "VersionManager: Failed to create cache directory {}: {}", cacheDir, ec.message());
			return false;
		}
	}

	std::string configPath{ GetConfigFilePath() };
	std::ofstream file(configPath);

	if (!file.is_open())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "VersionManager: Failed to create cache file: {}", configPath);
		return false;
	}

	file << "# Compiler versions cache - generated by Compilatron\n";
	file << "# This file is automatically managed\n\n";

	file << "[config]\n";

	if (!m_githubToken.empty())
	{
		file << "githubToken=" << m_githubToken << "\n";
	}

	auto const [requestsUsed, hourStart] = m_rateLimitTracker.GetRateLimitData();
	file << "rateLimitRequests=" << requestsUsed << "\n";
	file << "rateLimitHourStart=" << hourStart << "\n";
	file << "\n";

	std::lock_guard<std::mutex> lock(m_dataMutex);

	for (auto const& [kind, versions] : m_compilerVersions)
	{
		file << "[" << (kind == ECompilerKind::Gcc ? "gcc" : "clang") << "]\n";
		file << "baseUrl=" << versions.baseUrl << "\n";
		file << "lastUpdated=" << versions.lastUpdated << "\n";

		file << "branches=";
		for (size_t i = 0; i < versions.branches.size(); ++i)
		{
			if (i > 0)
			{
				file << ",";
			}
			file << versions.branches[i];
		}
		file << "\n";

		file << "tags=";
		for (size_t i = 0; i < versions.tags.size(); ++i)
		{
			if (i > 0)
			{
				file << ",";
			}
			file << versions.tags[i];
		}
		file << "\n";

		file << "versions=";
		for (size_t i = 0; i < versions.versions.size(); ++i)
		{
			if (i > 0)
			{
				file << ",";
			}
			file << versions.versions[i];
		}
		file << "\n\n";
	}

	gLog.Info(Tge::Logging::ETarget::File, "VersionManager: Successfully saved cache to {}", configPath);
	return true;
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::GetLastSyncTime(ECompilerKind kind) const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	return GetLastSyncTimeInternal(kind);
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::GetLastSyncTimeInternal(ECompilerKind kind) const
{
	// This method assumes m_dataMutex is already held
	auto it = m_compilerVersions.find(kind);

	if (it != m_compilerVersions.end())
	{
		return it->second.lastUpdated;
	}
	return "";
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionManager::GetLastSyncDisplay(ECompilerKind kind) const
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	std::string isoTime{ GetLastSyncTimeInternal(kind) };

	if (isoTime.empty())
	{
		return "Never";
	}

	std::string displayString;

	// Parse ISO timestamp: YYYY-MM-DDTHH:MM:SSZ
	if (isoTime.length() >= 19)
	{
		auto parseAt = [&](int start, int len) -> int
		{
			int value{ -1 };
			auto [ptr, ec] = std::from_chars(isoTime.data() + start, isoTime.data() + start + len, value);
			return (ec == std::errc{}) ? value : -1;
		};

		int year{ parseAt(0, 4) };
		int mon{ parseAt(5, 2) };
		int mday{ parseAt(8, 2) };
		int hour{ parseAt(11, 2) };
		int min{ parseAt(14, 2) };
		int sec{ parseAt(17, 2) };

		if (year >= 0 && mon >= 0 && mday >= 0 && hour >= 0 && min >= 0 && sec >= 0)
		{
			std::tm stored_utc = {};
			stored_utc.tm_year = year - 1900;
			stored_utc.tm_mon  = mon - 1;
			stored_utc.tm_mday = mday;
			stored_utc.tm_hour = hour;
			stored_utc.tm_min  = min;
			stored_utc.tm_sec  = sec;

			time_t stored_time{ std::mktime(&stored_utc) };
			stored_time -= timezone;

			auto now = std::time(nullptr);
			double elapsed_seconds{ std::difftime(now, stored_time) };
			auto local_tm = std::localtime(&stored_time);

			if (local_tm != nullptr)
			{
				constexpr std::array<char const*, 13> months = {
					"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
					"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
				};

				std::string day_str{ std::format("{:02d}", local_tm->tm_mday) };
				std::string year_str{ std::format("{}", local_tm->tm_year + 1900) };
				std::string hour_str{ std::format("{:02d}", local_tm->tm_hour) };
				std::string min_str{ std::format("{:02d}", local_tm->tm_min) };
				int monthNum{ local_tm->tm_mon + 1 };

				if (monthNum >= 1 && monthNum <= 12)
				{
					std::string relative_time;

					if (elapsed_seconds < 60)
					{
						relative_time = "just now";
					}
					else if (elapsed_seconds < 3600)
					{
						int minutes{ static_cast<int>(elapsed_seconds / 60) };
						relative_time = std::format("{} min ago", minutes);
					}
					else if (elapsed_seconds < 86400)
					{
						int hours{ static_cast<int>(elapsed_seconds / 3600) };
						int minutes{ static_cast<int>((elapsed_seconds - hours * 3600) / 60) };
						relative_time = std::format("{} hour{}", hours, hours == 1 ? "" : "s");

						if (minutes > 0)
						{
							relative_time += std::format(" {} minute{}", minutes, minutes == 1 ? "" : "s");
						}

						relative_time += " ago";
					}
					else
					{
						int days{ static_cast<int>(elapsed_seconds / 86400) };
						int remaining_seconds{ static_cast<int>(elapsed_seconds) % 86400 };
						int hours{ remaining_seconds / 3600 };
						int minutes{ (remaining_seconds % 3600) / 60 };
						relative_time = std::format("{} day{}", days, days == 1 ? "" : "s");

						if (hours > 0)
						{
							relative_time += std::format(" {} hour{}", hours, hours == 1 ? "" : "s");
						}

						if (minutes > 0)
						{
							relative_time += std::format(" {} minute{}", minutes, minutes == 1 ? "" : "s");
						}

						relative_time += " ago";
					}

					displayString = std::format("{} {} {} {}:{} ({})", day_str, months[monthNum], year_str, hour_str, min_str, relative_time);
				}
				else
				{
					displayString = isoTime;
				}
			}
			else
			{
				displayString = isoTime;
			}
		}
		else
		{
			displayString = isoTime;
		}
	}
	else
	{
		displayString = isoTime;
	}

	return displayString;
}
} // namespace Ctrn
