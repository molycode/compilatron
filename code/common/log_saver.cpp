#include "common/log_saver.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "common/process_executor.hpp"
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

namespace Ctrn
{

//////////////////////////////////////////////////////////////////////////
void CLogSaver::PollCompletion()
{
	if (m_active
		&& m_future.valid()
		&& m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		m_future.get();
		m_active = false;
	}
}

//////////////////////////////////////////////////////////////////////////
void CLogSaver::Save(std::string_view header, std::string_view defaultFilename, std::string content)
{
	std::filesystem::path const defaultPath{ std::filesystem::path(g_dataDir) / defaultFilename };

	// Zenity 4.x only pre-selects existing files, so pre-write to allow filename suggestion
	{
		std::ofstream prewrite(defaultPath);

		if (prewrite.is_open())
		{
			prewrite << header << content;
		}
		else
		{
			gLog.Warning("Failed to pre-write log file: {}", defaultPath.string());
		}
	}

	m_active = true;
	m_future = std::async(std::launch::async,
		[defaultPath, content = std::move(content)]()
		{
			std::string const zenityCmd{ std::format(
				"zenity --file-selection --save --confirm-overwrite"
				" --title=\"Save Log\" --filename=\"{}\" 2>/dev/null",
				defaultPath.string()) };

			auto const result{ CProcessExecutor::Execute(zenityCmd) };

			if (result.success && !result.output.empty())
			{
				std::string chosenPath{ result.output };

				while (!chosenPath.empty() && std::isspace(static_cast<unsigned char>(chosenPath.back())))
				{
					chosenPath.pop_back();
				}

				if (chosenPath != defaultPath.string())
				{
					std::error_code ec;
					std::filesystem::rename(defaultPath, chosenPath, ec);

					if (ec)
					{
						// rename fails across filesystems — copy then remove
						std::filesystem::copy_file(defaultPath, chosenPath,
							std::filesystem::copy_options::overwrite_existing, ec);

						if (!ec)
						{
							std::filesystem::remove(defaultPath, ec);
						}
						else
						{
							gLog.Warning("Failed to move log file to: {}", chosenPath);
						}
					}
				}

				gLog.Info(Tge::Logging::ETarget::Console, "Log saved to: {}", chosenPath);
			}
			else
			{
				// User cancelled — remove the pre-written file
				std::error_code ec;
				std::filesystem::remove(defaultPath, ec);
			}
		});
}

} // namespace Ctrn
