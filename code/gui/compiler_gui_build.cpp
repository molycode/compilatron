#include "gui/compiler_gui.hpp"
#include "build/compiler_builder.hpp"
#include "build/clang_unit.hpp"
#include "build/gcc_unit.hpp"
#include "common/loggers.hpp"
#include "common/common.hpp"
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#include <imgui.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic pop
#endif
#include <filesystem>
#include <format>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::StartBuild()
{
	if (!m_isBuilding)
	{
		m_isBuilding = true;
		m_buildProgress = 0.0f;

		std::vector<CCompilerUnit*> units;

		for (auto const& tab : m_compilerTabs)
		{
			if (tab.isOpen && !tab.name.empty() && !tab.folderName.empty())
			{
				if (IsLldAvailableForTab(tab))
				{
					auto unitIt = m_compilerUnits.find(tab.id);

					if (unitIt != m_compilerUnits.end() && unitIt->second != nullptr)
					{
						CCompilerUnit& unit = *unitIt->second;
						unit.UpdateBuildConfig(BuildConfigFromTab(tab));

						if (tab.kind == ECompilerKind::Gcc)
						{
							static_cast<CGccUnit*>(&unit)->SetGccSettings(tab.gccSettings);
						}
						else
						{
							static_cast<CClangUnit*>(&unit)->SetClangSettings(tab.clangSettings);
						}

						unit.GenerateCommands();
						ClearUnitLog(tab.id);

						std::string folderName{ tab.folderName };
						std::string name{ tab.name };

						unit.SetCompletionCallback([this, folderName, name](std::string const& unitName, bool success, std::string const&)
						{
							gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Unit completed: {} (success: {})", unitName, success ? "yes" : "no");

							if (success)
							{
								std::string const installPath{ GetResolvedInstallPath() + "/" + folderName + "/bin" };
								gLog.Info(Tge::Logging::ETarget::Console, "Compiler {} built successfully at: {}", name, installPath);
								gLog.Info(Tge::Logging::ETarget::Console, "Use 'Find Compilers' in the dependency manager to register it.");
							}
						});

						units.emplace_back(&unit);
					}
					else
					{
						gLog.Warning(Tge::Logging::ETarget::File, "CompilerGUI: StartBuild: No unit found for tab '{}'", tab.name);
					}
				}
				else
				{
					gLog.Warning(Tge::Logging::ETarget::Console, "Skipping '{}' — lld selected but ld.lld not found", tab.name);
				}
			}
		}

		auto progressCb = [this](SBuildProgress const& progress)
		{
			m_buildProgress = progress.overallProgress;
		};

		auto completionCb = [this](bool success, std::string const&)
		{
			m_isBuilding = false;

			if (success)
			{
				m_buildProgress = 1.0f;
			}
		};

		SBuildSettings tabBasedSettings = CreateBuildSettingsFromTabs();
		m_compilerBuilder->StartBuild(std::move(units), tabBasedSettings, progressCb, completionCb);
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::StartSingleCompilerBuild(SCompilerTab const& tab)
{
	if (!m_isBuilding)
	{
		auto validationResult = ValidateCompilerForBuild(tab);

		bool const validationFailed = validationResult != ECompilerValidationResult::Valid &&
		                              validationResult != ECompilerValidationResult::CompilerNotPresent;

		if (validationFailed)
		{
			std::string actualCompiler{ GetActualCompilerForTab(tab) };
			std::string errorMsg{ GetValidationErrorMessage(validationResult, actualCompiler,
			                        GetResolvedInstallPath() + "/" + tab.folderName) };
			gLog.Error(Tge::Logging::ETarget::Console, "CompilerGUI: Build blocked for '{}': {}", tab.name, errorMsg);
		}
		else if (WouldInstallToSystemDirectory(tab))
		{
			std::string folderName{ tab.folderName.empty() ? GetFolderNameFromCompilerName(tab.name) : tab.folderName };
			std::string plannedPath{ GetResolvedInstallPath() + "/" + folderName };
			gLog.Error(Tge::Logging::ETarget::Console, "CompilerGUI: Build blocked for '{}': would install to system directory '{}'", tab.name, plannedPath);
		}
		else
		{
			if (!IsLldAvailableForTab(tab))
			{
				gLog.Error(Tge::Logging::ETarget::Console, "CompilerGUI: Build blocked for '{}': lld selected but ld.lld not found", tab.name);
			}
			else
			{
				m_isBuilding = true;
				m_buildProgress = 0.0f;

				std::vector<CCompilerUnit*> units;
				auto unitIt = m_compilerUnits.find(tab.id);

			if (unitIt != m_compilerUnits.end() && unitIt->second != nullptr)
			{
				CCompilerUnit& unit = *unitIt->second;
				unit.UpdateBuildConfig(BuildConfigFromTab(tab));

				if (tab.kind == ECompilerKind::Gcc)
				{
					static_cast<CGccUnit*>(&unit)->SetGccSettings(tab.gccSettings);
				}
				else
				{
					static_cast<CClangUnit*>(&unit)->SetClangSettings(tab.clangSettings);
				}

				unit.GenerateCommands();
				ClearUnitLog(tab.id);

				std::string folderName{ tab.folderName };
				std::string name{ tab.name };

				unit.SetCompletionCallback([this, folderName, name](std::string const& unitName, bool success, std::string const&)
				{
					gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Unit completed: {} (success: {})", unitName, success ? "yes" : "no");

					if (success)
					{
						std::string installPath{ GetResolvedInstallPath() + "/" + folderName + "/bin" };

						if (std::filesystem::exists(installPath))
						{
							bool isAlreadyRegistered{ false };

							for (auto const& compiler : g_compilerRegistry.GetCompilers())
							{
								if (!isAlreadyRegistered && compiler.isRemovable)
								{
									std::string const compilerDir{
										std::filesystem::path(compiler.path).parent_path().string() };
									isAlreadyRegistered = (compilerDir == installPath);
								}
							}

							if (isAlreadyRegistered)
							{
								gLog.Info(Tge::Logging::ETarget::Console, "Compiler {} built successfully. Rescanning its install directory since it's registered with this app.", name);
								g_compilerRegistry.ScanDirectory(installPath);
								g_dependencyManager.ScanAllDependencies();
								gLog.Info(Tge::Logging::ETarget::Console, "Registered compiler directory rescanned. Updated compiler version is now available.");
							}
							else
							{
								gLog.Info(Tge::Logging::ETarget::Console, "Compiler {} built successfully at: {}", name, installPath);
								gLog.Info(Tge::Logging::ETarget::Console, "Directory not registered with app - use 'Find Compilers' to add it if desired.");
							}
						}
					}
				});

				units.emplace_back(&unit);
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::File, "CompilerGUI: StartSingleCompilerBuild: No unit found for tab '{}'", tab.name);
			}

			auto progressCb = [this](SBuildProgress const& progress)
			{
				m_buildProgress = progress.overallProgress;
			};

			auto completionCb = [this](bool success, std::string const&)
			{
				m_isBuilding = false;

				if (success)
				{
					m_buildProgress = 1.0f;
				}
			};

			SBuildSettings singleCompilerSettings = g_buildSettings;
			singleCompilerSettings.compilerEntries.clear();

			SCompilerEntry entry;
			entry.name = tab.name;
			entry.folderName = tab.folderName;
			entry.numJobs = (tab.numJobs == 0) ? g_cpuInfo.GetDefaultNumJobs() : tab.numJobs;
			entry.keepDependencies = tab.keepDependencies;
			entry.keepSources = tab.keepSources;
			entry.hostCompiler = tab.hostCompiler;
			entry.compilerType = tab.kind == ECompilerKind::Gcc ? "gcc" : "clang";
			entry.clangSettings = tab.clangSettings;
			entry.gccSettings = tab.gccSettings;

			singleCompilerSettings.compilerEntries.push_back(std::move(entry));

			m_compilerBuilder->StartBuild(std::move(units), singleCompilerSettings, progressCb, completionCb);

				gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Started single compiler build for {}", tab.name);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::StopBuild()
{
	if (m_isBuilding)
	{
		m_compilerBuilder->StopBuild();
		m_isBuilding = false;
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::UpdateBuildStatus()
{
	if (m_compilerBuilder)
	{
		m_isBuilding = m_compilerBuilder->IsBuilding();

		if (m_isBuilding)
		{
			auto progress = m_compilerBuilder->GetProgress();
			m_buildProgress = progress.overallProgress;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::DeleteCompilerSources(SCompilerTab const& tab)
{
	namespace fs = std::filesystem;

	std::string sourcesPath{ "./sources/" + tab.folderName };

	if (fs::exists(sourcesPath))
	{
		std::error_code ec;
		fs::remove_all(sourcesPath, ec);

		if (!ec)
		{
			gLog.Info(Tge::Logging::ETarget::Console, "Deleted sources for {} ({})", tab.name, tab.folderName);
			gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Deleted sources: {}", sourcesPath);
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::Console, "Failed to delete sources for {}: {}", tab.name, ec.message());
			gLog.Warning(Tge::Logging::ETarget::File, "CompilerGUI: Source deletion error: {}", ec.message());
		}
	}
	else
	{
		gLog.Info(Tge::Logging::ETarget::Console, "No sources directory found for {}", tab.name);
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::DeleteCompilerBuild(SCompilerTab const& tab)
{
	namespace fs = std::filesystem;

	std::string buildPath{ "./build_compilers/" + tab.folderName };

	if (fs::exists(buildPath))
	{
		std::error_code ec;
		fs::remove_all(buildPath, ec);

		if (!ec)
		{
			gLog.Info(Tge::Logging::ETarget::Console, "Deleted build artifacts for {} ({})", tab.name, tab.folderName);
			gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Deleted build: {}", buildPath);
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::Console, "Failed to delete build artifacts for {}: {}", tab.name, ec.message());
			gLog.Warning(Tge::Logging::ETarget::File, "CompilerGUI: Build deletion error: {}", ec.message());
		}
	}
	else
	{
		gLog.Info(Tge::Logging::ETarget::Console, "No build directory found for {}", tab.name);
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderProgressBarWithStatus(CCompilerUnit& unit)
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4 defaultProgressColor = style.Colors[ImGuiCol_PlotHistogram];

	ECompilerStatus const status{ unit.GetStatus() };
	float const progress{ unit.GetProgress() };

	ImVec4 barColor;
	std::string statusText;

	switch (status)
	{
		case ECompilerStatus::NotStarted:
			barColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
			statusText = "";
			break;
		case ECompilerStatus::Cloning:
			barColor = ImVec4(0.0f, 0.75f, 1.0f, 1.0f);
			statusText = " Cloning...";
			break;
		case ECompilerStatus::Waiting:
			barColor = ImVec4(0.85f, 0.62f, 0.0f, 1.0f);
			statusText = " Waiting...";
			break;
		case ECompilerStatus::Building:
			barColor = defaultProgressColor;
			statusText = " Building...";
			break;
		case ECompilerStatus::Success:
			barColor = ImVec4(0.18f, 0.72f, 0.28f, 1.0f);
			statusText = " Success";
			break;
		case ECompilerStatus::Failed:
			barColor = ImVec4(0.85f, 0.15f, 0.15f, 1.0f);
			statusText = " Failed";
			break;
		case ECompilerStatus::Aborted:
			barColor = ImVec4(0.85f, 0.15f, 0.15f, 1.0f);
			statusText = " Stopped";
			break;
	}

	// Custom progress bar: dark rounded track + colored fill + drop-shadowed label
	{
		ImDrawList* drawList{ ImGui::GetWindowDrawList() };
		ImVec2 const pos{ ImGui::GetCursorScreenPos() };
		float const barHeight{ ImGui::GetFrameHeight() };
		float const rounding{ barHeight * 0.3f };

		// Track
		drawList->AddRectFilled(
			pos,
			ImVec2(pos.x + g_elementWidth, pos.y + barHeight),
			IM_COL32(30, 30, 30, 220),
			rounding);

		// Fill
		float const fillWidth{ g_elementWidth * progress };

		if (fillWidth > rounding)
		{
			drawList->AddRectFilled(
				pos,
				ImVec2(pos.x + fillWidth, pos.y + barHeight),
				ImGui::ColorConvertFloat4ToU32(barColor),
				rounding);
		}

		// Percentage label centered on bar with 1px drop shadow for readability
		std::string const progressLabel{ std::format("{:.0f}%", progress * 100.0f) };
		ImVec2 const textSize{ ImGui::CalcTextSize(progressLabel.c_str()) };
		ImVec2 const textPos{
			pos.x + (g_elementWidth - textSize.x) * 0.5f,
			pos.y + (barHeight - textSize.y) * 0.5f
		};
		drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 180), progressLabel.c_str());
		drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), progressLabel.c_str());

		ImGui::Dummy(ImVec2(g_elementWidth, barHeight));
	}

	ImGui::SameLine();

	auto const [glyph, iconColor] = GetStatusIcon(status);
	ImGui::TextColored(iconColor, "%s", glyph);

	if (!statusText.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(barColor, "%s", statusText.c_str());
	}

	ImGui::SameLine();
	std::string buttonLabel{ std::format("Open##{}", unit.GetName()) };

	ImVec2 buttonPos = ImGui::GetCursorScreenPos();

	if (ImGui::SmallButton(buttonLabel.c_str()))
	{
		std::string folderName{ unit.GetFolderName().empty() ? GetFolderNameFromCompilerName(unit.GetName()) : unit.GetFolderName() };
		std::string folderPath{ GetResolvedInstallPath() + "/" + folderName };
		auto const openResult = OpenFolder(folderPath);

		if (!openResult.has_value())
		{
			unit.ShowNotification("Failed to open folder: " + openResult.error());
		}
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::Text("Open installation folder");
		ImGui::EndTooltip();
	}

	RenderCompilerNotification(unit, buttonPos);
}
} // namespace Ctrn
