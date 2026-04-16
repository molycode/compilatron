#include "gui/rate_limit_tracker.hpp"
#include <charconv>
#include <format>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CRateLimitTracker::TrackRequest()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ResetIfNewHour();
	m_requestsThisHour++;
}

//////////////////////////////////////////////////////////////////////////
int CRateLimitTracker::GetRequestsUsedThisHour() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ResetIfNewHour();
	return m_requestsThisHour;
}

//////////////////////////////////////////////////////////////////////////
int CRateLimitTracker::GetRequestsRemaining(bool hasToken) const
{
	int used{ GetRequestsUsedThisHour() };
	int limit{ GetTotalLimit(hasToken) };
	return std::max(0, limit - used);
}

//////////////////////////////////////////////////////////////////////////
int CRateLimitTracker::GetTotalLimit(bool hasToken) const
{
	return hasToken ? AuthenticatedLimit : UnauthenticatedLimit;
}

//////////////////////////////////////////////////////////////////////////
std::string CRateLimitTracker::GetResetTimeString() const
{
	auto minutesLeft = GetMinutesUntilReset();

	if (minutesLeft.count() <= 1)
	{
		return "1 min";
	}
	else
	{
		return std::format("{} min", minutesLeft.count());
	}
}

//////////////////////////////////////////////////////////////////////////
std::chrono::minutes CRateLimitTracker::GetMinutesUntilReset() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto now = std::chrono::system_clock::now();
	auto hourEnd = m_currentHourStart + std::chrono::hours(1);
	auto timeLeft = std::chrono::duration_cast<std::chrono::minutes>(hourEnd - now);
	return std::max(timeLeft, std::chrono::minutes(0));
}

//////////////////////////////////////////////////////////////////////////
bool CRateLimitTracker::IsRateLimited(bool hasToken) const
{
	int remaining{ GetRequestsRemaining(hasToken) };
	return remaining < 10; // Need at least 10 requests for basic operation
}

//////////////////////////////////////////////////////////////////////////
std::string CRateLimitTracker::GetStatusMessage(bool hasToken) const
{
	int used{ GetRequestsUsedThisHour() };
	int limit{ GetTotalLimit(hasToken) };
	std::string result;

	if (IsRateLimited(hasToken))
	{
		result = std::format("GitHub: Rate limited (resets in {})", GetResetTimeString());
	}
	else
	{
		result = std::format("GitHub: {}/{} requests used", used, limit);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CRateLimitTracker::ResetIfNewHour() const
{
	auto now = std::chrono::system_clock::now();

	if (now - m_currentHourStart >= std::chrono::hours(1))
	{
		m_currentHourStart = now;
		m_requestsThisHour = 0;
	}
}

//////////////////////////////////////////////////////////////////////////
void CRateLimitTracker::SetRateLimitData(int requestsUsed, std::string_view hourStartTime)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_requestsThisHour = requestsUsed;

	if (!hourStartTime.empty() && hourStartTime.length() >= 19)
	{
		auto parseAt = [&](int start, int len) -> int
		{
			int value{ -1 };
			auto [ptr, ec] = std::from_chars(hourStartTime.data() + start, hourStartTime.data() + start + len, value);
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
			std::tm tm = {};
			tm.tm_year = year - 1900;
			tm.tm_mon  = mon - 1;
			tm.tm_mday = mday;
			tm.tm_hour = hour;
			tm.tm_min  = min;
			tm.tm_sec  = sec;

			auto time_t_val = std::mktime(&tm);

			if (time_t_val != -1)
			{
				time_t_val -= timezone;
				m_currentHourStart = std::chrono::system_clock::from_time_t(time_t_val);
			}
		}
	}

	ResetIfNewHour();
}

//////////////////////////////////////////////////////////////////////////
std::pair<int, std::string> CRateLimitTracker::GetRateLimitData() const
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto time_t_val = std::chrono::system_clock::to_time_t(m_currentHourStart);
	std::tm* tm_ptr = std::gmtime(&time_t_val);

	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", tm_ptr);

	return {m_requestsThisHour, std::string(buffer)};
}
} // namespace Ctrn
