#pragma once

#include "dependency/dependency_checker.hpp"
#include <tge/non_copyable.hpp>

namespace Ctrn
{
class CSimpleDependencyDialog final : private Tge::SNoCopyNoMove
{
public:

	explicit CSimpleDependencyDialog(CDependencyChecker& checker);

	bool Render();

	bool ShouldExit() const { return m_shouldExit || m_installSuccess; }

private:

	CDependencyChecker& m_checker;
	bool m_showDialog{ true };
	bool m_isInstalling{ false };
	bool m_installSuccess{ false };
	bool m_installFailed{ false };
	bool m_shouldExit{ false };
};
} // namespace Ctrn
