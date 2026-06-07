#pragma once

#include <string>
#include <string_view>

namespace Ctrn
{
// Returns the input with leading and trailing whitespace removed; internal whitespace is preserved.
// A whitespace-only (or empty) input yields an empty string.
[[nodiscard]] inline std::string Trimmed(std::string_view text)
{
	constexpr std::string_view whitespace{ " \t\n\r\f\v" };
	size_t const first{ text.find_first_not_of(whitespace) };

	if (first == std::string_view::npos)
	{
		return std::string{};
	}

	size_t const last{ text.find_last_not_of(whitespace) };
	return std::string{ text.substr(first, last - first + 1) };
}
} // namespace Ctrn
