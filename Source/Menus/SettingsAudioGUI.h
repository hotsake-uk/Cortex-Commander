#pragma once

#include "AudioMan.h"

#include <array>

namespace RTE {

	class GUIControlManager;
	class GUICollectionBox;
	class GUILabel;
	class GUISlider;
	class GUICheckbox;
	class GUIButton;
	class GUIEvent;

	/// Handling for audio settings through the game settings user interface.
	class SettingsAudioGUI {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a SettingsAudioGUI object in system memory and make it ready for use.
		/// @param parentControlManager Pointer to the parent GUIControlManager which owns all the GUIControls of this SettingsAudioGUI. Ownership is NOT transferred!
		explicit SettingsAudioGUI(GUIControlManager* parentControlManager);
#pragma endregion

#pragma region Concrete Methods
		/// Enables or disables the SettingsAudioGUI.
		/// @param enable Show and enable or hide and disable the SettingsAudioGUI.
		void SetEnabled(bool enable = true) const;

		/// Handles the player interaction with the AudioVideoGUI GUI elements.
		/// @param guiEvent The GUIEvent containing information about the player interaction with an element.
		void HandleInputEvents(GUIEvent& guiEvent);
#pragma endregion

	private:
		GUIControlManager* m_GUIControlManager; //!< The GUIControlManager which holds all the GUIControls of this menu. Not owned by this.

		/// GUI elements that compose the audio settings menu screen.
		GUICollectionBox* m_AudioSettingsBox;
		GUILabel* m_MasterVolumeLabel;
		GUISlider* m_MasterVolumeSlider;
		GUICheckbox* m_MasterMuteCheckbox;
		GUILabel* m_MusicVolumeLabel;
		GUISlider* m_MusicVolumeSlider;
		GUICheckbox* m_MusicMuteCheckbox;
		GUILabel* m_SoundVolumeLabel;
		GUISlider* m_SoundVolumeSlider;
		GUICheckbox* m_SoundMuteCheckbox;
		GUICheckbox* m_MuteOnFocusLossCheckbox;
		std::array<GUISlider*, AudioMan::MIX_CATEGORY_COUNT> m_SoundMixSliders;
		std::array<GUILabel*, AudioMan::MIX_CATEGORY_COUNT> m_SoundMixValueLabels;
		GUIButton* m_ResetSoundMixButton;

#pragma region Audio Settings Handling
		/// Updates the position of the master volume slider and volume value label, based on what the AudioMan is currently set to.
		void UpdateMasterVolumeControls();

		/// Updates the position of the music volume slider and volume value label, based on what the AudioMan is currently set to.
		void UpdateMusicVolumeControls();

		/// Updates the position of the sound volume slider and volume value label, based on what the AudioMan is currently set to.
		void UpdateSoundVolumeControls();

		/// Updates the position of a sound mix category slider and its value label, based on what the AudioMan is currently set to.
		/// @param category The SoundMixCategory to update the controls of.
		void UpdateSoundMixControls(AudioMan::SoundMixCategory category);
#pragma endregion

		// Disallow the use of some implicit methods.
		SettingsAudioGUI(const SettingsAudioGUI& reference) = delete;
		SettingsAudioGUI& operator=(const SettingsAudioGUI& rhs) = delete;
	};
} // namespace RTE
