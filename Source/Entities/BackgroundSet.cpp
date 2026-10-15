#include "BackgroundSet.h"
#include "PresetMan.h"
#include "SLBackground.h"

using namespace RTE;

ConcreteClassInfo(BackgroundSet, Entity, 0);

int BackgroundSet::Create(const BackgroundSet& reference) {
	Entity::Create(reference);
	for (const SLBackground* layer: reference.m_Layers) {
		m_Layers.push_back(dynamic_cast<SLBackground*>(layer->Clone()));
	}
	return 0;
}

void BackgroundSet::Destroy(bool notInherited) {
	for (SLBackground* layer: m_Layers) {
		delete layer;
	}
	if (!notInherited) {
		Entity::Destroy();
	}
	Clear();
}

int BackgroundSet::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Entity::ReadProperty(propName, reader));

	MatchProperty("AddBackgroundLayer", {
		if (SLBackground* layer = dynamic_cast<SLBackground*>(g_PresetMan.ReadReflectedPreset(reader))) {
			m_Layers.push_back(layer);
		} else {
			reader.ReportError("A BackgroundSet's AddBackgroundLayer has to be an SLBackground!");
		}
	});

	EndPropertyList;
}

int BackgroundSet::Save(Writer& writer) const {
	Entity::Save(writer);
	for (const SLBackground* layer: m_Layers) {
		writer.NewProperty("AddBackgroundLayer");
		layer->SavePresetCopy(writer);
	}
	return 0;
}
