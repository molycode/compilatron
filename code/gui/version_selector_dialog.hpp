#pragma once

#include "build/compiler_kind.hpp"
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Ctrn
{
// Result of a selection: a branch/tag name, or a pinned commit SHA when isCommit is true
struct SVersionSelection final
{
	std::string value;
	bool isCommit{ false };
};

// GitHub-style version selector with tabbed Branches/Tags/Commit interface and real-time filtering
class CVersionSelectorDialog final : private Tge::SNoCopyNoMove
{
public:

	CVersionSelectorDialog() = default;
	~CVersionSelectorDialog() = default;

	void Open(std::vector<std::string> const& allVersions, std::string_view currentSelection, ECompilerKind kind);

	// Preferred overload: accepts pre-separated branches and tags
	void Open(std::vector<std::string> const& branches, std::vector<std::string> const& tags,
	          std::string_view currentSelection, ECompilerKind kind);

	// Call each frame; returns the selection, or an empty value if no selection was made
	[[nodiscard]] SVersionSelection Render();

	void Close();

	[[nodiscard]] bool IsOpen() const { return m_isOpen; }

private:

	bool m_isOpen{ false };
	int m_activeTab{ 0 }; // 0=Branches, 1=Tags, 2=Commit
	char m_filterBuffer[64]{};
	char m_commitBuffer[64]{};
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
	SVersionSelection RenderCommitInput();
	bool MatchesFilter(std::string_view version) const;
	float CalculateOptimalWidth() const;
};

} // namespace Ctrn