#pragma once

#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <chrono>
#include <mutex>

namespace Ctrn
{
// Tracks GitHub API rate limit usage and provides warnings to users
class CRateLimitTracker final : private Tge::SNoCopyNoMove
{
public:

	CRateLimitTracker() = default;
	~CRateLimitTracker() = default;

	// Call this after each API request to track usage
	void TrackRequest();

	int GetRequestsUsedThisHour() const;
	int GetRequestsRemaining(bool hasToken) const;
	int GetTotalLimit(bool hasToken) const;

	std::string GetResetTimeString() const;
	std::string GetStatusMessage(bool hasToken) const;

	// Set/get rate limit data (for persistence via VersionManager)
	void SetRateLimitData(int requestsUsed, std::string_view hourStartTime);
	std::pair<int, std::string> GetRateLimitData() const;

private:

	mutable std::mutex m_mutex;
	mutable std::chrono::system_clock::time_point m_currentHourStart{ std::chrono::system_clock::now() };
	mutable int m_requestsThisHour{ 0 };

	static constexpr int UnauthenticatedLimit = 60;
	static constexpr int AuthenticatedLimit = 5000;

	void ResetIfNewHour() const;
	[[nodiscard]] bool IsRateLimited(bool hasToken) const;
	std::chrono::minutes GetMinutesUntilReset() const;
};
} // namespace Ctrn
