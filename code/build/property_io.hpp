#pragma once

#include "build/enum_labels.hpp"
#include "build/property.hpp"

#include <charconv>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Ctrn
{
// ---------------------------------------------------------------------------
// PropertySerialize — converts a value to its preset-file string
// Enums are stored as integer indices for backward compatibility
// ---------------------------------------------------------------------------

template<typename T>
std::string PropertySerialize(T const& v)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		return v ? "true" : "false";
	}
	else if constexpr (std::is_same_v<T, int>)
	{
		return std::to_string(v);
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		return v;
	}
	else if constexpr (std::is_enum_v<T>)
	{
		return std::to_string(static_cast<int>(v));
	}
}

// ---------------------------------------------------------------------------
// PropertyDeserialize — parses a preset-file string back into a value
// Returns false if parsing fails; value is left unchanged on failure
// ---------------------------------------------------------------------------

template<typename T>
bool PropertyDeserialize(std::string_view s, T& v)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		v = (s == "true");
		return true;
	}
	else if constexpr (std::is_same_v<T, int>)
	{
		int result{};
		auto const [ptr, ec]{ std::from_chars(s.data(), s.data() + s.size(), result) };

		if (ec == std::errc{})
		{
			v = result;
			return true;
		}

		return false;
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		v = std::string{ s };
		return true;
	}
	else if constexpr (std::is_enum_v<T>)
	{
		int idx{};
		auto const [ptr, ec]{ std::from_chars(s.data(), s.data() + s.size(), idx) };

		if (ec == std::errc{})
		{
			v = static_cast<T>(idx);
			return true;
		}

		return false;
	}
}

// ---------------------------------------------------------------------------
// PropertyDisplayString — human-readable string for display in the diff panel
// Enums use SEnumMeta labels; bools show "on" / "off"
// ---------------------------------------------------------------------------

template<typename T>
std::string PropertyDisplayString(T const& v)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		return v ? "on" : "off";
	}
	else if constexpr (std::is_same_v<T, int>)
	{
		return std::to_string(v);
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		return v.empty() ? std::string{ "(none)" } : v;
	}
	else if constexpr (std::is_enum_v<T>)
	{
		return std::string{ SEnumMeta<T>::items[static_cast<size_t>(v)] };
	}
}
// ---------------------------------------------------------------------------
// SerializeSettings — writes all SProperty fields of a settings struct to an
// ostream as "prefix + internalName = serialized_value\n"
// ---------------------------------------------------------------------------

template<typename T>
void SerializeSettings(std::ostream& out, T const& s, std::string_view prefix)
{
	std::apply([&](auto const&... props)
	{
		((out << prefix << props.internalName << "="
		      << PropertySerialize(props.value) << "\n"), ...);
	}, s.Properties());
}

// ---------------------------------------------------------------------------
// DeserializeSettings — reads keys from a string→string map into all
// SProperty fields of a settings struct; prefix is prepended to internalName
// ---------------------------------------------------------------------------

template<typename T>
void DeserializeSettings(std::unordered_map<std::string, std::string> const& values,
                          T& s, std::string_view prefix)
{
	std::apply([&](auto&... props)
	{
		([&]()
		{
			auto const key{ std::string{prefix} + std::string{props.internalName} };
			auto const it{ values.find(key) };

			if (it != values.end())
			{
				PropertyDeserialize(it->second, props.value);
			}
		}(), ...);
	}, s.Properties());
}

// ---------------------------------------------------------------------------
// DiffSettings — appends one line per changed field to out:
// "linePrefix + uiName: displayString(old) → displayString(new)"
// ---------------------------------------------------------------------------

template<typename T>
void DiffSettings(std::vector<std::string>& out, T const& oldS, T const& newS,
                   std::string_view linePrefix)
{
	auto const oldTuple{ oldS.Properties() };
	auto const newTuple{ newS.Properties() };

	[&]<size_t... Is>(std::index_sequence<Is...>)
	{
		([&]()
		{
			auto const& oldP{ std::get<Is>(oldTuple) };
			auto const& newP{ std::get<Is>(newTuple) };

			if (oldP.value != newP.value)
			{
				out.emplace_back(std::format("{}{}: {} \u2192 {}", linePrefix, newP.uiName,
				    PropertyDisplayString(oldP.value), PropertyDisplayString(newP.value)));
			}
		}(), ...);
	}(std::make_index_sequence<std::tuple_size_v<decltype(oldTuple)>>{});
}
} // namespace Ctrn
