#pragma once

#include "Entity.h"

#include <list>

namespace RTE {

	class SLBackground;

	/// A named stack of background layers (sky, clouds, far and near hills) that can be put behind any map in place of its own, from the Settings panel (F6).
	/// Defined in ini like a Scene's backgrounds: one AddBackgroundLayer per layer, nearest first.
	class BackgroundSet : public Entity {

	public:
		EntityAllocation(BackgroundSet);
		SerializableOverrideMethods;
		ClassInfoGetters;

#pragma region Creation
		/// Constructor method used to instantiate a BackgroundSet object in system memory. Create() should be called before using the object.
		BackgroundSet() { Clear(); }

		/// Creates a BackgroundSet to be identical to another, by deep copy.
		/// @param reference A reference to the BackgroundSet to deep copy.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(const BackgroundSet& reference);
#pragma endregion

#pragma region Destruction
		/// Destructor method used to clean up a BackgroundSet object before deletion from system memory.
		~BackgroundSet() override { Destroy(true); }

		/// Destroys and resets (through Clear()) the BackgroundSet object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or to destroy all inherited members also.
		void Destroy(bool notInherited = false) override;
#pragma endregion

#pragma region Getters
		/// The layers of this set, nearest first. Ownership is NOT transferred; clone them to put them in a Scene.
		const std::list<SLBackground*>& GetLayers() const { return m_Layers; }
#pragma endregion

	protected:
		static Entity::ClassInfo m_sClass; //!< ClassInfo for this class.

		std::list<SLBackground*> m_Layers; //!< The layers, owned.

	private:
		/// Clears all the member variables of this BackgroundSet, effectively resetting the members of this abstraction level only.
		void Clear() { m_Layers.clear(); }

		// Disallow the use of some implicit methods.
		BackgroundSet(const BackgroundSet& reference) = delete;
		BackgroundSet& operator=(const BackgroundSet& rhs) = delete;
	};
} // namespace RTE
