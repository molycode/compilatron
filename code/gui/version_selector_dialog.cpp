#include "gui/version_selector_dialog.hpp"
#include "common/loggers.hpp"

#include <imgui.h>
#include <algorithm>
#include <regex>
#include <cctype>

namespace Ctrn
{
void CVersionSelectorDialog::Open(std::vector<std::string> const& allVersions, std::string_view currentSelection, ECompilerKind kind)
{
	m_isOpen = true;
	m_selectedVersion = currentSelection;
	m_kind = kind;
	m_filterBuffer[0] = '\0'; // Clear filter
	m_activeTab = 0; // Start with Branches tab

	CategorizeVersions(allVersions);
	UpdateFilteredLists();

	gLog.Info(Tge::Logging::ETarget::File, "VersionSelectorDialog: Opened with {} total versions ({} branches, {} tags)",
		allVersions.size(), m_branches.size(), m_tags.size());
}

//////////////////////////////////////////////////////////////////////////
void CVersionSelectorDialog::Open(std::vector<std::string> const& branches, std::vector<std::string> const& tags,
                                   std::string_view currentSelection, ECompilerKind kind)
{
	m_isOpen = true;
	m_selectedVersion = currentSelection;
	m_kind = kind;
	m_filterBuffer[0] = '\0'; // Clear filter
	m_activeTab = 0; // Start with Branches tab

	// Use pre-separated branches and tags (no heuristic needed!)
	m_branches = branches;
	m_tags = tags;
	UpdateFilteredLists();

	gLog.Info(Tge::Logging::ETarget::File, "VersionSelectorDialog: Opened with {} branches and {} tags ({} total)",
		branches.size(), tags.size(), branches.size() + tags.size());
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionSelectorDialog::Render()
{
	if (!m_isOpen)
	{
		return "";
	}

	std::string selectedVersion;

	// Create popup window with adaptive sizing
	float windowWidth{ CalculateOptimalWidth() };
	ImGui::SetNextWindowSize(ImVec2(windowWidth, 500), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Select Version##VersionSelector", &m_isOpen))
	{
		RenderFilterInput();
		RenderTabBar();

		// Separator
		ImGui::Separator();

		std::string result{ RenderVersionList() };

		if (!result.empty())
		{
			selectedVersion = result;
			Close();
		}

		ImGui::EndPopup();
	}

	return selectedVersion;
}

//////////////////////////////////////////////////////////////////////////
void CVersionSelectorDialog::Close()
{
	m_isOpen = false;
	ImGui::CloseCurrentPopup();
}

//////////////////////////////////////////////////////////////////////////
void CVersionSelectorDialog::CategorizeVersions(std::vector<std::string> const& allVersions)
{
	m_branches.clear();
	m_tags.clear();

	// SHOW ALL VERSIONS - No artificial filtering like GitHub web interface
	// The VersionManager already separated branches from tags, so we use that categorization
	// This assumes the VersionManager provides branches first, then tags (which it now does)

	for (auto const& version : allVersions)
	{
		// Use the same heuristic as VersionManager to categorize
		bool isTag{ false };

		if (m_kind == ECompilerKind::Clang)
		{
			// LLVM tags: llvmorg-*, swift-*, or anything with version numbers
			isTag = (version.find("llvmorg-") == 0 ||
			        version.find("swift-") == 0 ||
			        std::regex_match(version, std::regex(".*\\d+\\.\\d+.*")));
		}
		else
		{
			// GCC tags: releases/gcc-*, basepoints/gcc-*, or anything with version numbers
			isTag = (version.find("releases/gcc-") == 0 ||
			        version.find("basepoints/gcc-") == 0 ||
			        std::regex_match(version, std::regex(".*\\d+\\.\\d+.*")));
		}

		if (isTag)
		{
			m_tags.push_back(version);
		}
		else
		{
			m_branches.push_back(version);
		}
	}

	gLog.Info(Tge::Logging::ETarget::File, "VersionSelectorDialog: Categorized {}: {} branches, {} tags",
		m_kind == ECompilerKind::Gcc ? "gcc" : "clang", m_branches.size(), m_tags.size());
}

//////////////////////////////////////////////////////////////////////////
void CVersionSelectorDialog::UpdateFilteredLists()
{
	m_filteredBranches.clear();
	m_filteredTags.clear();

	for (auto const& branch : m_branches)
	{
		if (MatchesFilter(branch))
		{
			m_filteredBranches.push_back(branch);
		}
	}

	for (auto const& tag : m_tags)
	{
		if (MatchesFilter(tag))
		{
			m_filteredTags.push_back(tag);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CVersionSelectorDialog::RenderFilterInput()
{
	ImGui::Text("Filter versions:");
	ImGui::SetNextItemWidth(-1.0f);

	bool filterChanged{ ImGui::InputText("##filter", m_filterBuffer, sizeof(m_filterBuffer), ImGuiInputTextFlags_AutoSelectAll) };

	if (filterChanged)
	{
		UpdateFilteredLists();
	}

	if (ImGui::IsWindowAppearing())
	{
		ImGui::SetKeyboardFocusHere(-1);
	}
}

//////////////////////////////////////////////////////////////////////////
void CVersionSelectorDialog::RenderTabBar()
{
	if (ImGui::BeginTabBar("##VersionTabs"))
	{
		if (ImGui::BeginTabItem("Branches"))
		{
			m_activeTab = 0;
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Tags"))
		{
			m_activeTab = 1;
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CVersionSelectorDialog::RenderVersionList()
{
	std::string selectedVersion;

	std::vector<std::string> const* currentList = nullptr;

	if (m_activeTab == 0)
	{
		currentList = &m_filteredBranches;
	}
	else
	{
		currentList = &m_filteredTags;
	}

	if (ImGui::BeginChild("##VersionList", ImVec2(0, 0), true))
	{
		if (currentList->empty())
		{
			ImGui::TextDisabled("No versions match the filter");
		}
		else
		{
			for (auto const& version : *currentList)
			{
				bool isSelected{ (version == m_selectedVersion) };

				if (ImGui::Selectable(version.c_str(), isSelected))
				{
					selectedVersion = version;
				}

				if (isSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
		}
	}
	ImGui::EndChild();

	return selectedVersion;
}

//////////////////////////////////////////////////////////////////////////
bool CVersionSelectorDialog::MatchesFilter(std::string_view version) const
{
	if (m_filterBuffer[0] == '\0')
	{
		return true; // Empty filter matches everything
	}

	std::string filter{ m_filterBuffer };
	std::string lowerVersion{version};

	// Convert to lowercase for case-insensitive matching
	std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
	std::transform(lowerVersion.begin(), lowerVersion.end(), lowerVersion.begin(), ::tolower);

	return lowerVersion.find(filter) != std::string::npos;
}

//////////////////////////////////////////////////////////////////////////
float CVersionSelectorDialog::CalculateOptimalWidth() const
{
	float const minWidth = 400.0f;
	float const maxWidth = 800.0f;
	float const padding = 80.0f; // Extra space for scrollbars, borders, etc.

	float maxTextWidth{ 0.0f };

	for (auto const& version : m_branches)
	{
		float textWidth{ ImGui::CalcTextSize(version.c_str()).x };
		maxTextWidth = std::max(maxTextWidth, textWidth);
	}

	for (auto const& version : m_tags)
	{
		float textWidth{ ImGui::CalcTextSize(version.c_str()).x };
		maxTextWidth = std::max(maxTextWidth, textWidth);
	}

	float optimalWidth{ maxTextWidth + padding };
	return std::clamp(optimalWidth, minWidth, maxWidth);
}
} // namespace Ctrn
