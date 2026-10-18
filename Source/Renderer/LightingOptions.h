#pragma once

#include "LightingSettings.h"

#include <string>
#include <vector>

namespace RTE {

	/// One lighting or effects setting as Settings.ini and the Lighting settings menu know it: its key, its name in the menu, the page it's on and the field of LightingSettings it is.
	/// Drawing only: none of these change what happens in a game.
	struct LightingOption {
		const char* Key; //!< The key in Settings.ini, the same as Cortex Commander's settings presets use.
		const char* Label; //!< The name shown in the menu.
		int Page; //!< The menu page it's on (LightingOptions::GetPageNames), -1 for one that's only saved.
		bool LightingSettings::*BoolField = nullptr; //!< The field, if it's on or off.
		float LightingSettings::*FloatField = nullptr; //!< The field, if it's a number.
		int LightingSettings::*IntField = nullptr; //!< The field, if it's a whole number.
		glm::vec3 LightingSettings::*ColorField = nullptr; //!< The field, if it's a colour.
		float Min = 0.0F; //!< The lowest value it can have. Min and Max the same for no limits.
		float Max = 0.0F; //!< The highest value it can have.

		LightingOption(const char* key, const char* label, int page, bool LightingSettings::*field) :
		    Key(key), Label(label), Page(page), BoolField(field), Min(0.0F), Max(1.0F) {}
		LightingOption(const char* key, const char* label, int page, float LightingSettings::*field, float min = 0.0F, float max = 0.0F) :
		    Key(key), Label(label), Page(page), FloatField(field), Min(min), Max(max) {}
		LightingOption(const char* key, const char* label, int page, int LightingSettings::*field, float min = 0.0F, float max = 0.0F) :
		    Key(key), Label(label), Page(page), IntField(field), Min(min), Max(max) {}
		LightingOption(const char* key, const char* label, int page, glm::vec3 LightingSettings::*field) :
		    Key(key), Label(label), Page(page), ColorField(field) {}

		/// Gets whether the option has limits.
		bool HasRange() const { return Max > Min; }
	};

	/// The lighting and effects settings the player can change, in one table that Settings.ini and the Lighting settings menu both go by.
	class LightingOptions {

	public:
		/// Gets every option: those on the menu's pages in order, then those that are only saved.
		static const std::vector<LightingOption>& GetAll();

		/// Gets the names of the menu's pages.
		static const std::vector<std::string>& GetPageNames();

		/// Gets the option saved under a key.
		/// @param key The Settings.ini key.
		/// @return The option, or nullptr if there's none with that key.
		static const LightingOption* Find(const std::string& key);

		/// Gets an option's value as a number: 0 or 1 for one that's on or off. Not for colours.
		static float GetValue(const LightingOption& option, const LightingSettings& settings);

		/// Sets an option's value from a number, kept within its limits. Not for colours.
		static void SetValue(const LightingOption& option, LightingSettings& settings, float value);

		/// Gets an option's value as Settings.ini writes it.
		static std::string GetValueText(const LightingOption& option, const LightingSettings& settings);

		/// Sets an option's value from Settings.ini, kept within its limits.
		static void SetValueText(const LightingOption& option, LightingSettings& settings, const std::string& text);

		/// Copies an option's value from one set of settings to another.
		static void CopyValue(const LightingOption& option, LightingSettings& to, const LightingSettings& from);
	};
} // namespace RTE
