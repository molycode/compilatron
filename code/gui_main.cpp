#include "dependency/dependency_checker.hpp"
#include "common/loggers.hpp"
#include "gui/compiler_gui.hpp"
#include "gui/simple_dependency_dialog.hpp"
#include "common/common.hpp"
#include <GLFW/glfw3.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#include <imgui.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic pop
#endif
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <IconsFontAwesome6.h>
#include <tge/logging/log_system.hpp>
#include <memory>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif // _WIN32

static void GlfwErrorCallback(int error, char const* description)
{
	// No console output - GUI only
}

int main(int argc, char* argv[])
{
	std::error_code ec;
	std::filesystem::path executablePath{ std::filesystem::read_symlink("/proc/self/exe", ec) };
	std::filesystem::path executableDir;

	if (!ec)
	{
		executableDir = executablePath.parent_path();
		Ctrn::g_rootDir = executableDir.string();
	}
	else if (argc > 0 && argv[0] != nullptr)
	{
		std::filesystem::path exePath(argv[0]);

		if (exePath.is_absolute())
		{
			executableDir = exePath.parent_path();
		}
		else
		{
			std::filesystem::path currentPath{ std::filesystem::current_path(ec) };

			if (!ec)
			{
				executableDir = (currentPath / exePath).parent_path();
			}
		}

		Ctrn::g_rootDir = std::filesystem::weakly_canonical(executableDir).string();
	}

	Ctrn::g_dataDir = Ctrn::g_rootDir + "/compilatron-data";

	Tge::Logging::GetLogSystem().Initialize("compilatron", Ctrn::g_dataDir + "/logs", Ctrn::g_dataDir + "/configs", Tge::Logging::ETimestampMode::WallClock);

	// Remove any leftover debug.log files from previous sessions
	std::filesystem::path dataDirPath(Ctrn::g_dataDir);
	std::string debugLogPath{ (dataDirPath / "debug.log").string() };
	std::error_code removeEc;

	if (std::filesystem::exists(debugLogPath, removeEc))
	{
		std::filesystem::remove(debugLogPath, removeEc);
	}

	Ctrn::gLog.Info(Tge::Logging::ETarget::File, "Main: Compilatron starting up...");
	Ctrn::gLog.Info(Tge::Logging::ETarget::File, "Main: g_rootDir: {}", Ctrn::g_rootDir);
	Ctrn::gLog.Info(Tge::Logging::ETarget::File, "Main: g_dataDir: {}", Ctrn::g_dataDir);

	// Update PATH with any locally installed dependencies before CDependencyChecker runs.
	// Dep manager is not yet initialized here — this only calls UpdateEnvironmentPaths().
	Ctrn::g_dependencyManager.ScanAllDependencies();

	Ctrn::CDependencyChecker depChecker;

	if (!depChecker.SetupLocalEnvironment())
	{
		Ctrn::gLog.Warning("Failed to set up local dependency environment");
	}

	if (!depChecker.CheckAllDependencies())
	{
		Ctrn::gLog.Warning("One or more dependencies failed checks");
	}

	bool hasMissingDeps{ !depChecker.CanRunGui() };

	// Initialize GLFW regardless - we need it for the dependency dialog too
	glfwSetErrorCallback(GlfwErrorCallback);

	if (!glfwInit())
	{
		return -1;
	}

	char const* glslVersion{ "#version 130" };
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	// Try to load saved window state directly to globals (create temporary preset manager for this)
	// We create it here temporarily just to load window state before GUI creation
	Ctrn::CPresetManager tempPresetManager;

	if (!tempPresetManager.LoadWindowState())
	{
		Ctrn::gLog.Info(Tge::Logging::ETarget::File, "WindowState: Using default window state: pos({},{}) size({}x{})",
			Ctrn::g_mainWindowPosX.load(), Ctrn::g_mainWindowPosY.load(),
			Ctrn::g_mainWindowWidth.load(), Ctrn::g_mainWindowHeight.load());
	}

	GLFWwindow* window = glfwCreateWindow(Ctrn::g_mainWindowWidth.load(), Ctrn::g_mainWindowHeight.load(), "Compilatron", nullptr, nullptr);

	if (window == nullptr)
	{
		glfwTerminate();
		return -1;
	}

	glfwSetWindowPos(window, Ctrn::g_mainWindowPosX.load(), Ctrn::g_mainWindowPosY.load());

	// Set up window callbacks to update globals immediately when user changes window
	glfwSetWindowPosCallback(window, [](GLFWwindow*, int x, int y) {
		Ctrn::g_mainWindowPosX = x;
		Ctrn::g_mainWindowPosY = y;
	});

	glfwSetWindowSizeCallback(window, [](GLFWwindow*, int width, int height) {
		Ctrn::g_mainWindowWidth = width;
		Ctrn::g_mainWindowHeight = height;
	});

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// Disable imgui.ini file creation
	io.IniFilename = nullptr;

	ImGui::StyleColorsDark();

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 5.0f;

	ImFontConfig config;
	config.OversampleH = 2;
	config.OversampleV = 1;
	config.PixelSnapH = true;

	float const fontSize{ 15.0f };
	std::string const robotoPath{ Ctrn::g_dataDir + "/fonts/Roboto-Medium.ttf" };
	std::string const faPath{ Ctrn::g_dataDir + "/fonts/fa-solid-900.ttf" };

	ImFont* mainFont{ io.Fonts->AddFontFromFileTTF(robotoPath.c_str(), fontSize, &config) };

	if (mainFont != nullptr)
	{
		static constexpr ImWchar iconRanges[]{ ICON_MIN_FA, ICON_MAX_FA, 0 };
		ImFontConfig iconConfig;
		iconConfig.MergeMode = true;
		iconConfig.GlyphMinAdvanceX = fontSize;
		iconConfig.PixelSnapH = true;
		ImFont* iconFont{ io.Fonts->AddFontFromFileTTF(faPath.c_str(), fontSize, &iconConfig, iconRanges) };

		if (iconFont != nullptr)
		{
			Ctrn::gLog.Info(Tge::Logging::ETarget::File, "FontInit: Loaded Roboto-Medium + Font Awesome 6 ({}px)", fontSize);
		}
		else
		{
			Ctrn::gLog.Warning("FontInit: Font Awesome not found at {} — icons will not render", faPath);
		}
	}
	else
	{
		Ctrn::gLog.Warning("FontInit: Roboto-Medium not found at {} — using default font", robotoPath);
		io.Fonts->AddFontDefault(&config);
	}

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glslVersion);

	std::unique_ptr<Ctrn::CSimpleDependencyDialog> depDialog;

	if (hasMissingDeps)
	{
		depDialog = std::make_unique<Ctrn::CSimpleDependencyDialog>(depChecker);
	}

	std::unique_ptr<Ctrn::CCompilerGUI> mainGui;

	if (!hasMissingDeps)
	{
		mainGui = std::make_unique<Ctrn::CCompilerGUI>();
		mainGui->Initialize();
	}

	while (!glfwWindowShouldClose(window))
	{
		glfwWaitEventsTimeout(1.0 / 60.0);

		// Handle window resize requests from other parts of the application
		if (Ctrn::g_mainWindowNeedsResize.load())
		{
			int targetWidth{ Ctrn::g_mainWindowWidth.load() };
			int targetHeight{ Ctrn::g_mainWindowHeight.load() };

			glfwSetWindowSize(window, targetWidth, targetHeight);
			Ctrn::g_mainWindowNeedsResize.store(false);

			Ctrn::gLog.Info(Tge::Logging::ETarget::File, "WindowState: Resized main window to {}x{}", targetWidth, targetHeight);
		}

		Tge::Logging::GetLogSystem().DispatchListeners();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (depDialog)
		{
			depDialog->Render();

			if (depDialog->ShouldExit())
			{
				glfwSetWindowShouldClose(window, true);
			}
		}
		else if (mainGui)
		{
			mainGui->Render();
		}
		else
		{
			// Fallback - should not happen
			ImGui::Begin("Error");
			ImGui::Text("Application error - no UI available");

			if (ImGui::Button("Exit"))
			{
				glfwSetWindowShouldClose(window, true);
			}

			ImGui::End();
		}

		ImGui::Render();

		int displayWidth, displayHeight;
		glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// No multi-viewport support in this ImGui version

		glfwSwapBuffers(window);
	}

	// Save window state before cleanup
	if (mainGui) // Only save if main GUI was created (not just dependency dialog)
	{
		int currentX, currentY, currentWidth, currentHeight;
		glfwGetWindowPos(window, &currentX, &currentY);
		glfwGetWindowSize(window, &currentWidth, &currentHeight);

		Ctrn::gLog.Info(Tge::Logging::ETarget::File, "WindowState: Saving window state: pos({},{}) size({}x{})", currentX, currentY, currentWidth, currentHeight);

		mainGui->SaveWindowState(currentX, currentY, currentWidth, currentHeight);
	}
	else
	{
		Ctrn::gLog.Info(Tge::Logging::ETarget::File, "WindowState: Not saving window state - mainGui not created");
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	Ctrn::gLog.Info(Tge::Logging::ETarget::File, "Main: Compilatron shutting down...");
	Tge::Logging::GetLogSystem().Terminate();

	return 0;
}
