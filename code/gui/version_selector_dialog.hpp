#pragma once

#include "build/compiler_kind.hpp"
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Ctrn
{
// GitHub-style version selector with tabbed Branches/Tags interface and real-time filtering
class CVersionSelectorDialog final : private Tge::SNoCopyNoMove
{
public:

	CVersionSelectorDialog() = default;
	~CVersionSelectorDialog() = default;

	void Open(std::vector<std::string> const& allVersions, std::string_view currentSelection, ECompilerKind kind);

	// Preferred overload: accepts pre-separated branches and tags
	void Open(std::vector<std::string> const& branches, std::vector<std::string> const& tags,
	          std::string_view currentSelection, ECompilerKind kind);

	// Call each frame; returns selected version string, or empty if no selection made
	[[nodiscard]] std::string Render();

	void Close();

	[[nodiscard]] bool IsOpen() const { return m_isOpen; }

private:

	bool m_isOpen{ false };
	int m_activeTab{ 0 }; // 0=Branches, 1=Tags
	char m_filterBuffer[64]{};
	std::string m_selectedVersion;
	ECompilerKind m_kind{ ECompilerKind::Gcc };

	std::vector<std::string> m_branches;
	std::vector<std::string> m_tags;

	std::vector<std::string> m_filteredBranches;
	std::vector<std::string> m_filteredTags;

	void CategorizeVersions(std::vector<std::string> const& allVersions);
	void UpdateFilteredLists();
	void RenderFilterInput();
	void RenderTabBar();
	std::string RenderVersionList();
	bool MatchesFilter(std::string_view version) const;
	float CalculateOptimalWidth() const;
};

} // namespace Ctrn