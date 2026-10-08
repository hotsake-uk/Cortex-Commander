#include "ConsoleMan.h"
#include "SettingsMan.h"

#include "LuaMan.h"
#include "UInputMan.h"
#include "WindowMan.h"
#include "FrameMan.h"
#include "PresetMan.h"

#include "GUI.h"
#include "AllegroBitmap.h"
#include "AllegroScreen.h"
#include "GUIInputWrapper.h"
#include "GUICollectionBox.h"
#include "GUITextBox.h"
#include "GUILabel.h"
#include "System.h"

using namespace RTE;

ConsoleMan::ConsoleMan() {
	Clear();
}

ConsoleMan::~ConsoleMan() {
	Destroy();
}

void ConsoleMan::Clear() {
	m_ConsoleState = ConsoleState::Disabled;
	m_ReadOnly = false;
	m_ConsoleScreenRatio = 0.3F;
	m_GUIScreen = nullptr;
	m_GUIInput = nullptr;
	m_GUIControlManager = nullptr;
	m_ParentBox = nullptr;
	m_ConsoleText = nullptr;
	m_InputTextBox = nullptr;
	m_ConsoleTextMaxNumLines = 10;
	m_OutputLog.clear();
	m_DroppedOutputLines = 0;
	m_InputLog.clear();
	m_InputLogPosition = m_InputLog.begin();
	m_LastInputString.clear();
	m_LastLogMove = 0;

	m_ConsoleUseMonospaceFont = false;
}

int ConsoleMan::Initialize() {
	if (!m_GUIScreen) {
		m_GUIScreen = new AllegroScreen(g_FrameMan.GetBackBuffer32());
	}
	if (!m_GUIInput) {
		m_GUIInput = new GUIInputWrapper(-1);
	}
	if (!m_GUIControlManager) {
		m_GUIControlManager = new GUIControlManager();
	}

	if (!m_GUIControlManager->Create(m_GUIScreen, m_GUIInput, "Base.rte/GUIs/Skins/Menus", m_ConsoleUseMonospaceFont ? "ConsoleMonospaceSkin.ini" : "ConsoleSkin.ini")) {
		RTEAbort("Failed to create GUI Control Manager and load it from Base.rte/GUIs/Skins/Menus/ConsoleSkin.ini");
	}

	m_GUIControlManager->Load("Base.rte/GUIs/ConsoleGUI.ini");
	m_GUIControlManager->EnableMouse(false);

	// Stretch the invisible root box to fill the screen
	dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("base"))->SetSize(g_WindowMan.GetResX(), g_WindowMan.GetResY());

	if (!m_ParentBox) {
		m_ParentBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("ConsoleGUIBox"));
		m_ParentBox->SetDrawType(GUICollectionBox::Color);
	}
	m_ConsoleText = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("ConsoleLabel"));
	m_InputTextBox = dynamic_cast<GUITextBox*>(m_GUIControlManager->GetControl("InputTB"));

	SetConsoleScreenSize(m_ConsoleScreenRatio);

	m_ParentBox->SetPositionAbs(0, -m_ParentBox->GetHeight());
	m_ParentBox->SetEnabled(false);
	m_ParentBox->SetVisible(false);

	if (!g_WindowMan.ResolutionChanged()) {
		m_OutputLog.emplace_back("- RTE Lua Console -\nSee the Data Realms Wiki for commands: http://www.datarealms.com/wiki/\nPress F1 for a list of helpful shortcuts\n-------------------------------------");
	}

	return 0;
}

void ConsoleMan::Destroy() {
	if (!g_WindowMan.ResolutionChanged()) {
		SaveAllText(System::InstanceFile("LogConsole.txt"));
	}

	delete m_GUIControlManager;
	delete m_GUIInput;
	delete m_GUIScreen;

	if (g_WindowMan.ResolutionChanged()) {
		m_GUIScreen = nullptr;
		m_GUIInput = nullptr;
		m_GUIControlManager = nullptr;
		m_ParentBox = nullptr;
	} else {
		Clear();
	}
}

void ConsoleMan::SetEnabled(bool enable) {
	if (enable && m_ConsoleState != ConsoleState::Enabled && m_ConsoleState != ConsoleState::Enabling) {
		m_ConsoleState = ConsoleState::Enabling;
		g_GUISound.EnterMenuSound()->Play();
	} else if (!enable && m_ConsoleState != ConsoleState::Disabled && m_ConsoleState != ConsoleState::Disabling) {
		m_ConsoleState = ConsoleState::Disabling;
		g_GUISound.ExitMenuSound()->Play();
	}
}

void ConsoleMan::SetReadOnly() {
	if (!m_ReadOnly) {
		// Save the current input string before changing to the read-only notice so we can restore it when switching back
		m_LastInputString = m_InputTextBox->GetText();
		m_InputTextBox->SetText("- CONSOLE IN READ-ONLY MODE! -");
		m_ReadOnly = true;
		SetEnabled(true);
	}
}

void ConsoleMan::SetConsoleScreenSize(float screenRatio) {
	m_ConsoleScreenRatio = Limit(screenRatio, 1.0F, 0.1F);

	if (!m_ParentBox || !m_ConsoleText || !m_InputTextBox) {
		return;
	}

	m_ParentBox->SetSize(g_WindowMan.GetResX(), g_WindowMan.GetResY() * m_ConsoleScreenRatio);
	m_ConsoleText->SetSize(m_ParentBox->GetWidth() - 4, m_ParentBox->GetHeight() - m_InputTextBox->GetHeight() - 2);
	m_ConsoleTextMaxNumLines = 5 + (m_ConsoleText->GetHeight() / m_GUIControlManager->GetSkin()->GetFont("FontSmall.png")->GetFontHeight());
	m_InputTextBox->SetPositionRel(m_InputTextBox->GetRelXPos(), m_ConsoleText->GetHeight());
	m_InputTextBox->Resize(m_ParentBox->GetWidth() - 3, m_InputTextBox->GetHeight());
}

void ConsoleMan::SetConsoleUseMonospaceFont(bool useFont) {
	m_ConsoleUseMonospaceFont = useFont;
	if (m_GUIControlManager) {
		m_GUIControlManager->ChangeSkin("Base.rte/GUIs/Skins/Menus", m_ConsoleUseMonospaceFont ? "ConsoleMonospaceSkin.ini" : "ConsoleSkin.ini");
	}
}

void ConsoleMan::AddLoadWarningLogExtensionMismatchEntry(const std::string& pathToLog, const std::string& readerPosition, const std::string& altFileExtension) {
	const std::string pathAndAccessLocation = "\"" + pathToLog + "\" referenced " + readerPosition + ". ";
	std::string newEntry = pathAndAccessLocation + (!altFileExtension.empty() ? "Found and loaded a file with \"" + altFileExtension + "\" extension." : "The file was not loaded.");
	if (g_PresetMan.GetReloadEntityPresetCalledThisUpdate()) {
		PrintString(newEntry);
	} else {
		std::transform(newEntry.begin(), newEntry.end(), newEntry.begin(), ::tolower);
		// Emplace inserts only if there is no existing entry.
		m_LoadWarningLog.emplace(newEntry);
	}
}

void ConsoleMan::SaveLoadWarningLog(const std::string& filePath) {
	Writer logWriter(filePath.c_str());
	if (logWriter.WriterOK()) {
		logWriter << "// Warnings produced during loading:";
		logWriter.NewLine(false);
		for (const std::string& logEntry: m_LoadWarningLog) {
			logWriter.NewLineString(logEntry, false);
		}
		PrintString("SYSTEM: Loading warning log saved to " + filePath);
	}
}

void ConsoleMan::SaveInputLog(const std::string& filePath) {
	Writer logWriter(filePath.c_str());
	if (logWriter.WriterOK()) {
		for (std::deque<std::string>::reverse_iterator logItr = m_InputLog.rbegin(); logItr != m_InputLog.rend(); ++logItr) {
			logWriter << *logItr;
			// Add semicolon so the line input becomes a statement
			if (!logItr->empty() && (*logItr)[logItr->length() - 1] != ';') {
				logWriter << ";";
			}
			logWriter << "\n";
		}
		PrintString("SYSTEM: Console input log saved to " + filePath);
	}
}

bool ConsoleMan::SaveAllText(const std::string& filePath) {
	Writer logWriter(filePath.c_str());
	if (logWriter.WriterOK()) {
		for (const std::string& loggedString: m_OutputLog) {
			logWriter << loggedString;
		}
		logWriter.EndWrite();
		PrintString("SYSTEM: Entire console contents saved to " + filePath);
		return true;
	}
	return false;
}

void ConsoleMan::ClearLog() {
	m_InputLog.clear();
	m_InputLogPosition = m_InputLog.begin();
	m_OutputLog.clear();
	m_DroppedOutputLines = 0;
}

void ConsoleMan::PrintString(const std::string& stringToPrint) {
	static std::mutex printStringMutex;
	std::scoped_lock<std::mutex> printStringLock(printStringMutex);

	m_OutputLog.emplace_back("\n" + stringToPrint);
	// The log is capped: a script that errors every update, or the Path debug channel, added tens to hundreds of MB an hour, all kept until exit
	// (and written into a crash dump). The first lines (start-up) are always kept, then a line counting what was dropped, then the latest.
	// CCCP_CONSOLE_LOG below still gets every line.
	if (m_OutputLog.size() > c_MaxOutputLines) {
		if (m_DroppedOutputLines == 0) {
			m_OutputLog.insert(m_OutputLog.begin() + c_KeptFirstOutputLines, std::string());
		}
		m_OutputLog.erase(m_OutputLog.begin() + c_KeptFirstOutputLines + 1);
		++m_DroppedOutputLines;
		m_OutputLog[c_KeptFirstOutputLines] = "\n... " + std::to_string(m_DroppedOutputLines) + " older lines dropped (the console keeps the first " + std::to_string(c_KeptFirstOutputLines) + " and the latest) ...";
	}
	// With CCCP_CONSOLE_LOG set to a file name, every line also goes straight to that file, so script errors can be read while the game runs or after it's been killed.
	// (Or, with Misc > "Console log to file" ticked, to LogConsoleLive.txt; read once, at the first line, so it takes effect from the next start.)
	// Looked up on every line, so turning the setting on in the Options menu starts the file straight away; the file opens on its first line.
	const char* envLogPath = std::getenv("CCCP_CONSOLE_LOG");
	const char* liveLogPath = envLogPath ? envLogPath : (g_SettingsMan.ConsoleLogToFile() ? "LogConsoleLive.txt" : nullptr);
	if (liveLogPath) {
		static std::ofstream liveLog(liveLogPath, std::ios::trunc);
		liveLog << stringToPrint << std::endl;
	}
	if (System::IsLoggingToCLI()) {
		System::PrintToCLI(stringToPrint);
	}
}

const std::vector<ConsoleMan::Shortcut>& ConsoleMan::GetShortcuts() {
	// (Each checked against where the key is handled: UInputMan::HandleSpecialInput, ConsoleMan::Update, the event loop in Main.cpp, DebugMan.)
	static const std::vector<Shortcut> shortcuts = {
	    {nullptr, "Console"},
	    {"~", "Open or close the console (Esc closes it too)"},
	    {"RAlt + ~", "The console in read-only mode, leaving the keyboard to the game"},
	    {"Ctrl + Up / Down", "Make the open console smaller or bigger"},
	    {"F1", "This list"},
	    {"F3", "Save the console's text (Console.dump.log)"},
	    {"F4", "Save what was typed into the console (Console.input.log)"},
	    {"F10", "Clear the console"},
	    {nullptr, "Tool windows"},
	    {"F6", "Settings: everything that can be tuned while the game runs, presets, and the debug pages"},
	    {"F7", "The sandbox window (in the Sandbox game mode: its tools, or back into your character)"},
	    {"F8", "Photo mode: frozen time, a free camera, look controls and big screenshots"},
	    {"Tab", "In a game, hide or bring back every tool window (Sandbox game mode: into your character; Shift + Tab puts it where the mouse points)"},
	    {"P", "Sandbox game mode: into your character and back above (Shift + P: where the mouse points)"},
	    {"Ctrl + I", "Inspect the unit under the pointer for the AI debug overlays and tracing, or stop"},
	    {nullptr, "In a game"},
	    {"Ctrl + mouse wheel", "Zoom (the wheel alone in the sandbox's god view)"},
	    {"Esc", "Pause menu (Shift + Esc: straight to the scenario or conquest menu)"},
	    {"RAlt + R", "Restart the activity (a controller's Back button too)"},
	    {"F5", "Quick save"},
	    {"F9", "Load the latest quick save (RAlt + F9: the latest auto save)"},
	    {nullptr, "Screenshots"},
	    {"F12", "A screenshot"},
	    {"RAlt + S", "Screenshots for as long as the keys are held"},
	    {"RAlt + W", "The whole level as one image (Alt + W: a small preview of it)"},
	    {nullptr, "Display and performance"},
	    {"Alt + Enter", "Fullscreen on or off"},
	    {"RAlt + M", "Draw the scene as normal, by material, or by object"},
	    {"RAlt + P", "Performance stats over the game (Alt + P: their graphs; 1 / 2 slower or faster, 5 / 6 the update step, RAlt + 1 / 5 back to normal). F6, Debug: the same as a window, and the frame profiler"},
	    {nullptr, "Modding"},
	    {"F2", "Reload all Lua scripts"},
	    {"Alt + F2", "Reload all sprites"},
	    {"RAlt + F2", "Reload the preset last reloaded with PresetMan:ReloadEntityPreset"},
	};
	return shortcuts;
}

void ConsoleMan::ShowShortcuts() {
	if (!IsEnabled()) {
		SetEnabled();
	}

	std::string text = "\n--- SHORTCUTS ---";
	for (const Shortcut& shortcut: GetShortcuts()) {
		text += shortcut.Keys ? std::string("\n") + shortcut.Keys + " - " + shortcut.What : std::string("\n-- ") + shortcut.What + " --";
	}
	PrintString(text);
}

void ConsoleMan::Update() {
	if (g_UInputMan.FlagRAltState() && g_UInputMan.KeyPressed(SDL_SCANCODE_GRAVE)) {
		SetReadOnly();
	}

	if (!g_UInputMan.FlagShiftState() && (!g_UInputMan.FlagRAltState() && (g_UInputMan.KeyPressed(SDL_SCANCODE_GRAVE) || (IsEnabled() && g_UInputMan.KeyPressed(SDL_SCANCODE_ESCAPE))))) {
		if (IsEnabled()) {
			if (!m_ReadOnly) {
				m_InputTextBox->SetEnabled(false);
				m_GUIControlManager->GetManager()->SetFocus(nullptr);
				// Save any text being worked on in the input, as the box keeps getting junk added to it
				m_LastInputString = m_InputTextBox->GetText();
				SetEnabled(false);
			} else {
				// Restore any text being worked on in the input and set the focus and cursor position of the input line
				m_InputTextBox->SetText(m_LastInputString);
				m_InputTextBox->SetCursorPos(m_InputTextBox->GetText().length());
				m_ReadOnly = false;
			}
		} else {
			m_InputTextBox->SetText(m_LastInputString);
			m_InputTextBox->SetCursorPos(m_InputTextBox->GetText().length());
			SetEnabled(true);
		}
	}

	if (m_ConsoleState != ConsoleState::Enabled && m_ConsoleState != ConsoleState::Disabled) {
		ConsoleOpenClose();
	}

	std::stringstream consoleText;
	for (std::deque<std::string>::iterator logIterator = (m_OutputLog.size() < m_ConsoleTextMaxNumLines) ? m_OutputLog.begin() : m_OutputLog.end() - m_ConsoleTextMaxNumLines; logIterator != m_OutputLog.end(); ++logIterator) {
		consoleText << *logIterator;
	}
	m_ConsoleText->SetText(consoleText.str());

	if (m_ConsoleState != ConsoleState::Enabled) {
		return;
	}

	m_GUIControlManager->Update();

	if (g_UInputMan.FlagCtrlState() && g_UInputMan.KeyPressed(SDLK_DOWN)) {
		SetConsoleScreenSize(m_ConsoleScreenRatio + 0.05F);
	} else if (g_UInputMan.FlagCtrlState() && g_UInputMan.KeyPressed(SDLK_UP)) {
		SetConsoleScreenSize(m_ConsoleScreenRatio - 0.05F);
	}

	if (!m_ReadOnly) {
		if (!m_InputTextBox->GetEnabled() || !m_InputTextBox->HasFocus()) {
			m_InputTextBox->SetEnabled(true);
			m_InputTextBox->SetFocus();
		}

		if (!m_InputLog.empty() && !g_UInputMan.FlagCtrlState()) {
			if (g_UInputMan.KeyPressed(SDLK_UP)) {
				LoadLoggedInput(false);
			} else if (g_UInputMan.KeyPressed(SDLK_DOWN)) {
				LoadLoggedInput(true);
			}
		}
		RemoveGraveAccents();
	} else {
		m_InputTextBox->SetEnabled(false);
		m_GUIControlManager->GetManager()->SetFocus(nullptr);
		return;
	}

	// Execute string when Enter is pressed, or execute immediately if a newline character is found, meaning multiple strings were pasted in.
	if ((g_UInputMan.KeyPressed(SDLK_RETURN) || g_UInputMan.KeyPressed(SDLK_KP_ENTER)) || (m_InputTextBox->GetText().find_last_of('\n') != std::string::npos)) {
		FeedString(m_InputTextBox->GetText().empty() ? true : false);
	}
}

void ConsoleMan::ConsoleOpenClose() {
	float travelCompletionDistance;

	if (m_ConsoleState == ConsoleState::Enabling) {
		m_ParentBox->SetEnabled(true);
		m_ParentBox->SetVisible(true);

		travelCompletionDistance = std::floor(static_cast<float>(m_ParentBox->GetYPos()) * 0.5F);
		m_ParentBox->SetPositionAbs(0, m_ParentBox->GetYPos() - static_cast<int>(travelCompletionDistance));

		if (m_ParentBox->GetYPos() >= 0) {
			m_ConsoleState = ConsoleState::Enabled;
		}
	} else if (m_ConsoleState == ConsoleState::Disabling) {
		travelCompletionDistance = std::ceil((static_cast<float>(m_ParentBox->GetHeight()) + static_cast<float>(m_ParentBox->GetYPos())) * 0.5F);
		m_ParentBox->SetPositionAbs(0, m_ParentBox->GetYPos() - static_cast<int>(travelCompletionDistance));

		if (m_ParentBox->GetYPos() <= -m_ParentBox->GetHeight()) {
			m_ParentBox->SetEnabled(false);
			m_ParentBox->SetVisible(false);
			m_InputTextBox->SetEnabled(false);
			m_GUIControlManager->GetManager()->SetFocus(nullptr);
			m_ConsoleState = ConsoleState::Disabled;
		}
	}

	// If supposed to be enabled or disabled but appears to be the opposite, enable or disable accordingly.
	if (m_ConsoleState == ConsoleState::Enabled && (m_ParentBox->GetYPos() < 0)) {
		m_ConsoleState = ConsoleState::Enabling;
	} else if (m_ConsoleState == ConsoleState::Disabled && (m_ParentBox->GetYPos() > -m_ParentBox->GetHeight())) {
		m_ConsoleState = ConsoleState::Disabling;
	}
}

void ConsoleMan::FeedString(bool feedEmptyString) {
	char strLine[1024];
	std::stringstream inputStream(m_InputTextBox->GetText());

	while (!inputStream.fail()) {
		inputStream.getline(strLine, 1024, '\n');
		std::string line = strLine;

		if (!feedEmptyString) {
			if (!line.empty() && line != "\r") {
				g_LuaMan.GetMasterScriptState().ClearErrors();
				m_OutputLog.emplace_back("\n" + line);
				g_LuaMan.GetMasterScriptState().RunScriptString(line, false);

				if (g_LuaMan.GetMasterScriptState().ErrorExists()) {
					m_OutputLog.emplace_back("\nERROR: " + g_LuaMan.GetMasterScriptState().GetLastError());
				}
				if (m_InputLog.empty() || m_InputLog.front() != line) {
					m_InputLog.push_front(line);
				}

				m_InputLogPosition = m_InputLog.begin();
				m_LastLogMove = 0;
			}
		} else {
			m_OutputLog.emplace_back("\n");
			break;
		}
	}
	m_InputTextBox->SetText("");
}

void ConsoleMan::LoadLoggedInput(bool nextEntry) {
	if (nextEntry) {
		// See if we should decrement doubly because the last move was in the opposite direction
		if (m_LastLogMove > 0 && m_InputLogPosition != m_InputLog.begin()) {
			--m_InputLogPosition;
		}

		if (m_InputLogPosition == m_InputLog.begin()) {
			m_InputTextBox->SetText("");
			m_LastLogMove = 0;
		} else {
			--m_InputLogPosition;
			m_InputTextBox->SetText(*m_InputLogPosition);
			m_InputTextBox->SetCursorPos(m_InputTextBox->GetText().length());
			m_LastLogMove = -1;
		}
	} else {
		// See if we should increment doubly because the last move was in the opposite direction
		if (m_LastLogMove < 0 && m_InputLogPosition != m_InputLog.end() - 1) {
			++m_InputLogPosition;
		}

		m_InputTextBox->SetText(*m_InputLogPosition);
		m_InputTextBox->SetCursorPos(m_InputTextBox->GetText().length());
		++m_InputLogPosition;
		m_LastLogMove = 1;

		// Avoid falling off the end
		if (m_InputLogPosition == m_InputLog.end()) {
			--m_InputLogPosition;
			m_LastLogMove = 0;
		}
	}
}

void ConsoleMan::RemoveGraveAccents() const {
	std::string textBoxString = m_InputTextBox->GetText();
	if (std::find(textBoxString.begin(), textBoxString.end(), '`') != textBoxString.end()) {
		textBoxString.erase(std::remove(textBoxString.begin(), textBoxString.end(), '`'), textBoxString.end());
		m_InputTextBox->SetText(textBoxString);
		m_InputTextBox->SetCursorPos(textBoxString.length());
	}
}

void ConsoleMan::Draw(BITMAP* targetBitmap) const {
	if (m_ConsoleState != ConsoleState::Disabled) {
		AllegroScreen drawScreen(targetBitmap);
		m_GUIControlManager->Draw(&drawScreen);
	}
}
