#pragma once

#include <tge/non_copyable.hpp>
#include <future>
#include <string>
#include <string_view>

namespace Ctrn
{

class CLogSaver final : private Tge::SNoCopyNoMove
{
public:

	CLogSaver() = default;
	~CLogSaver() = default;

	// Call once per frame before rendering buttons — resets active flag when done
	void PollCompletion();

	// Launches the async save dialog. Caller must check !IsActive() first.
	void Save(std::string_view header, std::string_view defaultFilename, std::string content);

	[[nodiscard]] bool IsActive() const { return m_active; }

private:

	std::future<void> m_future;
	bool m_active{ false };
};

} // namespace Ctrn
