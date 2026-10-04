#include "ConsoleMan.h"
#include "RTEError.h"
#include "Serializable.h"

namespace RTE {

	int Serializable::CreateSerializable(Reader& reader, bool checkType, bool doCreate, bool skipStartingObject) {
		if (checkType && reader.ReadPropValue() != GetClassName()) {
			reader.ReportError("Wrong type in Reader when passed to Serializable::Create()");
			return -1;
		}

		if (!skipStartingObject) {
			reader.StartObject();
		}
		while (reader.NextProperty()) {
			SetFormattedReaderPosition("in file " + reader.GetCurrentFilePath() + " on line " + reader.GetCurrentFileLine());
			std::string propName = reader.ReadPropName();
			// We need to check if !propName.empty() because ReadPropName may return "" when it reads an IncludeFile without any properties in case they are all commented out or it's the last line in file.
			// Also ReadModuleProperty may return "" when it skips IncludeFile till the end of file.
			if (!propName.empty() && ReadProperty(propName, reader) < 0) {
				// TODO: Could not match property. Log here!
			}
		}

		return doCreate ? Create() : 0;
	}

	int Serializable::ReadProperty(const std::string_view& propName, Reader& reader) {
		reader.ReadPropValue();
		if (RTEError::s_LoadingMod) {
			// A mod made for another version of the game may set things this one doesn't have. That one setting is passed over, with a note; the rest of the mod loads.
			g_ConsoleMan.AddLoadWarningLogEntry("Unknown property '" + std::string(propName) + "' passed over in " + reader.GetCurrentFilePath() + " at line " + reader.GetCurrentFileLine() + ".");
			return -1;
		}
		reader.ReportError("Could not match property '" + std::string(propName) + "'!");
		return -1;
	}

	Reader& operator>>(Reader& reader, Serializable& operand) {
		operand.Create(reader);
		return reader;
	}

	Reader& operator>>(Reader& reader, Serializable* operand) {
		if (operand) {
			operand->Create(reader);
		}
		return reader;
	}

	Writer& operator<<(Writer& writer, const Serializable& operand) {
		operand.Save(writer);
		writer.ObjectEnd();
		return writer;
	}

	Writer& operator<<(Writer& writer, const Serializable* operand) {
		if (operand) {
			operand->Save(writer);
			writer.ObjectEnd();
		} else {
			writer.NoObject();
		}
		return writer;
	}
} // namespace RTE
