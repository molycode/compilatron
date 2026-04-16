#pragma once

#include "build/optimization_level.hpp"
#include "build/build_type.hpp"
#include "build/cpp_standard.hpp"
#include "build/cmake_generator.hpp"

#include <array>
#include <string_view>

namespace Ctrn
{
template<typename T>
struct SEnumMeta;

template<>
struct SEnumMeta<EOptimizationLevel>
{
	static constexpr auto items = std::to_array<std::string_view>(
	{
		"O0", "O1", "O2", "O3", "Os", "Ofast"
	});
};

template<>
struct SEnumMeta<EBuildType>
{
	static constexpr auto items = std::to_array<std::string_view>(
	{
		"Debug", "Release", "RelWithDebInfo", "MinSizeRel"
	});
};

template<>
struct SEnumMeta<ECppStandard>
{
	static constexpr auto items = std::to_array<std::string_view>(
	{
		"C++11", "C++14", "C++17", "C++20", "C++23", "C++26", "C++29"
	});
};

template<>
struct SEnumMeta<ECMakeGenerator>
{
	static constexpr auto items = std::to_array<std::string_view>(
	{
		"Unix Makefiles", "Ninja"
	});
};
} // namespace Ctrn
