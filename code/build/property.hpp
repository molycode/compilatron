#pragma once

#include <format>
#include <ostream>
#include <string_view>
#include <utility>

namespace Ctrn
{
template<typename T>
struct SProperty final
{
	std::string_view internalName;
	std::string_view uiName;
	T value{};
	T defaultValue{};

	explicit SProperty(std::string_view name, std::string_view ui, T def)
		: internalName(name), uiName(ui), value(def), defaultValue(def) {}

	operator T const&() const { return value; }
	operator T&()             { return value; }

	SProperty& operator=(T const& v) { value = v; return *this; }
	SProperty& operator=(T&& v)      { value = std::move(v); return *this; }

	void Reset() { value = defaultValue; }

	bool operator==(SProperty const& o) const { return value == o.value; }
};

template<typename T>
std::ostream& operator<<(std::ostream& os, SProperty<T> const& prop)
{
	return os << prop.value;
}
} // namespace Ctrn

template<typename T, typename TChar>
struct std::formatter<Ctrn::SProperty<T>, TChar> : std::formatter<T, TChar>
{
	auto format(Ctrn::SProperty<T> const& prop, std::format_context& ctx) const
	{
		return std::formatter<T, TChar>::format(prop.value, ctx);
	}
};
