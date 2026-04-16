#pragma once

namespace Ctrn
{
enum class EOptimizationLevel
{
	O0,
	O1,
	O2,
	O3,
	Os,    // Optimize for size
	Ofast  // Unsafe optimizations
};
} // namespace Ctrn
