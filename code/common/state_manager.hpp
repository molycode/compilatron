#pragma once

#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>

namespace Ctrn
{
class CStateManager final : private Tge::SNoCopyNoMove
{
public:

	void Initialize();
	void Terminate();

	void SetActivePreset(std::string_view name);
	std::string const& GetActivePreset() const;

	void SetMainWindow(int x, int y, int width, int height);
	void GetMainWindow(int& x, int& y, int& width, int& height) const;

	void SetDepWindow(float x, float y, float width, float height);
	void GetDepWindow(float& x, float& y, float& width, float& height) const;

	// Takes a file path; stores its parent directory as the last browse location
	void SetLastBrowseDir(std::string_view filePath);
	std::string GetLastBrowseDir() const;

private:

	std::string m_activePreset{ "Default" };

	int m_mainWindowX{ 100 };
	int m_mainWindowY{ 100 };
	int m_mainWindowWidth{ 1600 };
	int m_mainWindowHeight{ 1050 };

	float m_depWindowX{ -1.0f };
	float m_depWindowY{ -1.0f };
	float m_depWindowWidth{ 1400.0f };
	float m_depWindowHeight{ 1000.0f };

	std::string m_lastBrowseDir;
};

extern CStateManager g_stateManager;
} // namespace Ctrn
