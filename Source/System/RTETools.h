#pragma once

// Header file for global utility methods.

#include "RTEError.h"
#include "DetMath.h"
#include "Constants.h"

#include <random>
#include <memory>
#include <string_view>
#include <type_traits>

namespace RTE {

	class Vector;
	class Matrix;

#pragma region Random Numbers
	class RandomGenerator {
		std::mt19937 m_RNG; //!< The random number generator used for all random functions.

	public:
		/// Seed the random number generator.
		void Seed(uint64_t seed) { m_RNG.seed(seed); };

		/// Gets the next raw number the generator will produce, without advancing it. This can be used to check whether two generators are in the same state.
		/// @return The next raw number the generator will produce.
		uint32_t PeekNext() const { return static_cast<uint32_t>(std::mt19937(m_RNG)()); }

		/// Function template which returns a uniformly distributed random number in the range [-1, 1].
		/// @return Uniformly distributed random number in the range [-1, 1].
		template <typename floatType = float>
		typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNormalNum() {
			return floatType(2.0) * UnitInterval<floatType>() - floatType(1.0);
		}

		/// Function template specialization for int types which returns a uniformly distributed random number in the range [-1, 1].
		/// @return Uniformly distributed random number in the range [-1, 1].
		template <typename intType>
		typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNormalNum() {
			return static_cast<intType>(static_cast<int>(UpTo(2)) - 1);
		}

		/// Function template which returns a uniformly distributed random number in the range [0, 1].
		/// @return Uniformly distributed random number in the range [0, 1].
		template <typename floatType = float>
		typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum() {
			return UnitInterval<floatType>();
		}

		/// Function template specialization for int types which returns a uniformly distributed random number in the range [0, 1].
		/// @return Uniformly distributed random number in the range [0, 1].
		template <typename intType>
		typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum() {
			return static_cast<intType>(UpTo(1));
		}

		/// Function template which returns a uniformly distributed random number in the range [min, max].
		/// @param min Lower boundary of the range to pick a number from.
		/// @param max Upper boundary of the range to pick a number from.
		/// @return Uniformly distributed random number in the range [min, max].
		template <typename floatType = float>
		typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum(floatType min, floatType max) {
			if (max < min) {
				std::swap(min, max);
			}
			return min + UnitInterval<floatType>() * (max - min);
		}

		/// Function template specialization for int types which returns a uniformly distributed random number in the range [min, max].
		/// @param min Lower boundary of the range to pick a number from.
		/// @param max Upper boundary of the range to pick a number from.
		/// @return Uniformly distributed random number in the range [min, max].
		template <typename intType>
		typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum(intType min, intType max) {
			if (max < min) {
				std::swap(min, max);
			}
			using UnsignedType = std::make_unsigned_t<intType>;
			return static_cast<intType>(static_cast<UnsignedType>(min) + static_cast<UnsignedType>(UpTo(static_cast<uint64_t>(static_cast<UnsignedType>(static_cast<UnsignedType>(max) - static_cast<UnsignedType>(min))))));
		}

	private:
		// The standard library's distributions aren't specified exactly, so each library (MSVC's, libstdc++, libc++) turns the same raw numbers into different ones.
		// These are written out here instead, so that the same seed gives the same numbers on every platform. mt19937 itself is specified exactly.

		/// Gets a uniformly distributed number in the range [0, 1], from as many raw bits as the type can hold exactly.
		template <typename floatType>
		floatType UnitInterval() {
			if constexpr (sizeof(floatType) <= sizeof(float)) {
				return static_cast<floatType>(static_cast<float>(m_RNG() >> 8) * (1.0F / 16777215.0F));
			} else {
				uint64_t bits = Raw64() >> 11;
				return static_cast<floatType>(static_cast<double>(bits) * (1.0 / 9007199254740991.0));
			}
		}

		/// Gets 64 raw bits, from two raw numbers taken in a fixed order (the order the operands of an expression are worked out in isn't fixed).
		uint64_t Raw64() {
			uint64_t high = m_RNG();
			uint64_t low = m_RNG();
			return high << 32 | low;
		}

		/// Gets a uniformly distributed whole number in the range [0, max], by rejecting the raw numbers that would make some results likelier than others.
		uint64_t UpTo(uint64_t max) {
			if (max <= 0xFFFFFFFFULL) {
				uint32_t range = static_cast<uint32_t>(max);
				if (range == 0xFFFFFFFFU) {
					return m_RNG();
				}
				uint32_t bucketCount = range + 1;
				uint32_t limit = 0xFFFFFFFFU - (0xFFFFFFFFU % bucketCount + 1) % bucketCount;
				uint32_t raw;
				do {
					raw = m_RNG();
				} while (raw > limit);
				return raw % bucketCount;
			}
			uint64_t limit = max == ~0ULL ? ~0ULL : ~0ULL - (~0ULL % (max + 1) + 1) % (max + 1);
			uint64_t raw;
			do {
				raw = Raw64();
			} while (raw > limit);
			return max == ~0ULL ? raw : raw % (max + 1);
		}
	};

	extern RandomGenerator g_RandomGenerator; //!< The global random number generator used in our simulation thread.
	extern RandomGenerator g_DrawRandomGenerator; //!< The random number generator for drawing and audio, which must never take numbers from the simulation's, as how often they run differs from one machine and run to the next.

	/// Seed global the global random number generators. The thread that calls it is taken as the simulation thread (see ThisThreadsRandomGenerator).
	void SeedRNG();

	/// Sets a number that SeedRNG adds to the seed of the simulation's random number generator, so that the simulation gets a different stream of random numbers.
	/// @param seedOffset The number to add to the seed. With 0, the simulation gets its usual random numbers.
	void SetRNGSeedOffset(uint32_t seedOffset);

	/// The random number generator for the calling thread: g_RandomGenerator on the simulation thread, and one of its own on any other (the
	/// sight-ray workers, the threaded AI scripts, the Lua collector). A std::mt19937 shared between threads with no lock hands out duplicate
	/// numbers and can run its index past its state; and draws from the workers, in whatever order they ran, made the sim's own stream depend
	/// on thread timing, which undid the seeded determinism. The functions below all go through this.
	RandomGenerator& ThisThreadsRandomGenerator();

	/// Sets a random number generator for the global random functions to use when called from this thread, ahead of the ones ThisThreadsRandomGenerator would otherwise pick.
	/// The Lua states set their own while their threaded updates and AI run, so which worker thread happens to run which state doesn't change the numbers anything gets.
	/// @param randomGenerator The random number generator to use, or nullptr to go back to the usual one. Ownership is NOT transferred!
	void SetThreadRandomGenerator(RandomGenerator* randomGenerator);

	/// While one of these exists, the global random functions called from this thread use g_DrawRandomGenerator instead of the simulation's.
	/// Made around drawing, which runs a different number of times per sim update depending on frame rate, so whatever it draws randomly doesn't use up the simulation's numbers.
	class ScopedDrawRandomGenerator {
	public:
		ScopedDrawRandomGenerator() { SetThreadRandomGenerator(&g_DrawRandomGenerator); }
		~ScopedDrawRandomGenerator() { SetThreadRandomGenerator(nullptr); }
		ScopedDrawRandomGenerator(const ScopedDrawRandomGenerator&) = delete;
		ScopedDrawRandomGenerator& operator=(const ScopedDrawRandomGenerator&) = delete;
	};

	// TODO: Maybe remove these passthrough functions and force the user to manually specify if they want the simulation thread random,
	// Or, in future, a render-thread random, as right now determinism isn't viable because framerate affects sim updates per draw
	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNormalNum() {
		return ThisThreadsRandomGenerator().RandomNormalNum();
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNormalNum() {
		return ThisThreadsRandomGenerator().RandomNormalNum();
	}

	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum() {
		return ThisThreadsRandomGenerator().RandomNum();
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum() {
		return ThisThreadsRandomGenerator().RandomNum();
	}

	template <typename floatType = float>
	typename std::enable_if<std::is_floating_point<floatType>::value, floatType>::type RandomNum(floatType min, floatType max) {
		return ThisThreadsRandomGenerator().RandomNum(min, max);
	}

	template <typename intType>
	typename std::enable_if<std::is_integral<intType>::value, intType>::type RandomNum(intType min, intType max) {
		return ThisThreadsRandomGenerator().RandomNum(min, max);
	}
#pragma endregion

#pragma region Interpolation
	/// Simple Linear Interpolation, with an added bonus: scaleStart and scaleEnd let you define your scale, where 0 and 1 would be standard scale.
	/// This scale is used to normalize your progressScalar value and Lerp accordingly.
	/// @param scaleStart The start of the scale to Lerp along.
	/// @param scaleEnd The end of the scale to Lerp along.
	/// @param startValue The start value of your Lerp.
	/// @param endValue The end value of your Lerp.
	/// @param progressScalar How far your Lerp has progressed. Automatically normalized through use of scaleStart and scaleEnd.
	/// @return Interpolated value.
	float Lerp(float scaleStart, float scaleEnd, float startValue, float endValue, float progressScalar);

	/// Simple Linear Interpolation, with an added bonus: scaleStart and scaleEnd let you define your scale, where 0 and 1 would be standard scale.
	/// This scale is used to normalize your progressScalar value and Lerp accordingly.
	/// @param scaleStart The start of the scale to Lerp along.
	/// @param scaleEnd The end of the scale to Lerp along.
	/// @param startValue The start position of your Lerp.
	/// @param endValue The end position of your Lerp.
	/// @param progressScalar How far your Lerp has progressed. Automatically normalized through use of scaleStart and scaleEnd.
	/// @return Interpolated value.
	Vector Lerp(float scaleStart, float scaleEnd, Vector startPos, Vector endPos, float progressScalar);

	/// Simple Linear Interpolation, with an added bonus: scaleStart and scaleEnd let you define your scale, where 0 and 1 would be standard scale.
	/// This scale is used to normalize your progressScalar value and Lerp accordingly.
	/// @param scaleStart The start of the scale to Lerp along.
	/// @param scaleEnd The end of the scale to Lerp along.
	/// @param startRot The start rotation of your Lerp.
	/// @param endRot The end rotation of your Lerp.
	/// @param progressScalar How far your Lerp has progressed. Automatically normalized through use of scaleStart and scaleEnd.
	/// @return Interpolated value.
	Matrix Lerp(float scaleStart, float scaleEnd, const Matrix& startRot, const Matrix& endRot, float progressScalar);

	/// Nonlinear ease-in interpolation. Starts slow.
	/// @param start Start value.
	/// @param end End value.
	/// @param progressScalar Normalized positive progress scalar (0 - 1.0).
	/// @return Interpolated value.
	float EaseIn(float start, float end, float progressScalar);

	/// Nonlinear ease-out interpolation. Slows down toward the end.
	/// @param start Start value.
	/// @param end End value.
	/// @param progressScalar Normalized positive progress scalar (0 - 1.0).
	/// @return Interpolated value.
	float EaseOut(float start, float end, float progressScalar);

	/// Nonlinear ease-in-out interpolation. Slows down in the start and end.
	/// @param start Start value.
	/// @param end End value.
	/// @param progressScalar Normalized positive progress scalar (0 - 1.0).
	/// @return Interpolated value.
	float EaseInOut(float start, float end, float progressScalar);
#pragma endregion

#pragma region Clamping
	/// Clamps a value between two limit values.
	/// @param value Value to clamp.
	/// @param upperLimit Upper limit of value.
	/// @param lowerLimit Lower limit of value.
	/// @return True if either limit is currently reached, False if not.
	bool Clamp(float& value, float upperLimit, float lowerLimit);

	/// Clamps a value between two limit values.
	/// @param value Value to clamp.
	/// @param upperLimit Upper limit of value.
	/// @param lowerLimit Lower limit of value.
	/// @return Upper/Lower limit value if limit is currently reached, value between limits if not.
	float Limit(float value, float upperLimit, float lowerLimit);
#pragma endregion

#pragma region Rounding
	/// Rounds a float to a set fixed point precision (digits after decimal point) with option to always ceil or always floor the remainder.
	/// @param inputFloat The input float to round.
	/// @param precision The precision to round to, i.e. the number of digits after the decimal points.
	/// @param roundingMode Method of rounding to use. 0 for system default, 1 for floored remainder, 2 for ceiled remainder.
	/// @return A string of the float, rounded and displayed to chosen precision.
	std::string RoundFloatToPrecision(float input, int precision, int roundingMode = 0);

	/// Rounds an integer to the specified nearest multiple.
	/// For example, if the arguments are 63 and 5, the returned value will be 65.
	/// @param num The number to round to the nearest multiple.
	/// @param multiple The multiple to round to.
	/// @return An integer rounded to the specified nearest multiple.
	inline int RoundToNearestMultiple(int num, int multiple) { return static_cast<int>(std::round(static_cast<float>(num) / static_cast<float>(multiple)) * static_cast<float>(multiple)); }
#pragma endregion

#pragma region Angle Helpers
	/// Returns a copy of the angle normalized so it's between 0 and 2PI.
	/// @param angle The angle to normalize, in radians.
	/// @return The angle, normalized so it's between 0 and 2PI
	float NormalizeAngleBetween0And2PI(float angle);

	/// Returns a copy of the angle normalized so it's between -PI and PI.
	/// @param angle The angle to normalize, in radians.
	/// @return The angle, normalized so it's between -PI and PI
	float NormalizeAngleBetweenNegativePIAndPI(float angle);

	/// Returns whether or not the angle to check is between the start and end angles. Note that, because of how angles work (when normalized), the start angle may be greater than the end angle.
	/// @param angleToCheck The angle to check, in radians.
	/// @param startAngle The starting angle for the range.
	/// @param endAngle The ending angle for the range.
	/// @return Whether or not the angle to check is between the start and end angle.
	bool AngleWithinRange(float angleToCheck, float startAngle, float endAngle);

	/// Clamps the passed in angle between the specified lower and upper limits, in a CCW direction.
	/// @param angleToClamp The angle to clamp.
	/// @param startAngle The lower limit for clamping.
	/// @param endAngle The upper limit for clamping.
	/// @return The angle, clamped between the start and end angle.
	float ClampAngle(float angleToClamp, float startAngle, float endAngle);
#pragma endregion

#pragma region Detection
	/// Tells whether a point is within a specified box.
	/// @param point Vector position of the point we're checking.
	/// @param boxPos Vector position of the box.
	/// @param width Width of the box.
	/// @param height Height of the box.
	/// @return True if point is inside box bounds.
	bool WithinBox(const Vector& point, const Vector& boxPos, float width, float height);

	/// Tells whether a point is within a specified box.
	/// @param point Vector position of the point we're checking.
	/// @param left Position of box left plane (X start).
	/// @param top Position of box top plane (Y start).
	/// @param right Position of box right plane (X end).
	/// @param bottom Position of box bottom plane (Y end).
	/// @return True if point is inside box bounds.
	bool WithinBox(const Vector& point, float left, float top, float right, float bottom);
#pragma endregion

#pragma region Conversion
	/// Returns a corrected angle value that can be used with Allegro fixed point math routines where 256 equals 360 degrees.
	/// @param angleDegrees The angle value to correct. In degrees.
	/// @return A float with the represented angle as full rotations being 256.
	inline float GetAllegroAngle(float angleDegrees) { return (angleDegrees / 360) * 256; }

	/// Returns the given angle converted from degrees to radians.
	/// @param angleDegrees The angle in degrees to be converted.
	/// @return The converted angle in radians.
	inline float DegreesToRadians(float angleDegrees) { return angleDegrees / 180.0F * c_PI; }

	/// Returns the given angle converted from radians to degrees.
	/// @param angleRadians The angle in radians to be converted.
	/// @return The converted angle in degrees.
	inline float RadiansToDegrees(float angleRadians) { return angleRadians / c_PI * 180.0F; }
#pragma endregion

#pragma region Strings
	/// Checks whether two strings are equal when the casing is disregarded.
	/// @param strA First string.
	/// @param strB Second string.
	/// @return Whether the two strings are equal case insensitively.
	inline bool StringsEqualCaseInsensitive(const std::string_view& strA, const std::string_view& strB) {
		return std::equal(strA.begin(), strA.end(), strB.begin(), strB.end(), [](char strAChar, char strBChar) { return std::tolower(strAChar) == std::tolower(strBChar); });
	}

	/// If a file "foo/Bar.txt" exists, and this method is passed "FOO/BAR.TXT", then this method will return "foo/Bar.txt".
	/// This method's purpose is to enable Linux to get the real path using a case-insensitive search.
	/// The real path is used by the Lua file I/O handling methods to ensure full Windows compatibility.
	/// @param fullPath Path to case-insensitively translate to a real path.
	/// @return The real path. If the path doesn't exist, it returns the fullPath argument with all the existing parent directories correctly capitalized.
	std::string GetCaseInsensitiveFullPath(const std::string& fullPath);

	/// Hashes a string in a cross-compiler/platform safe way (std::hash gives different results on different compilers).
	/// @param text Text string to hash.
	/// @return The hash result value.
	uint64_t Hash(const std::string& text);
#pragma endregion

#pragma region Misc
	/// Convenience method that takes in a double pointer array and returns a std::vector with its contents, because pointers-to-pointers are the devil. The passed in array is deleted in the process so no need to delete it manually.
	/// @param arrayOfType The double pointer to convert to a std::vector.
	/// @param arraySize The size of the double pointer array.
	template <typename Type> std::vector<Type*> ConvertDoublePointerToVectorOfPointers(Type** arrayOfType, size_t arraySize) {
		std::unique_ptr<Type*[]> doublePointerArray = std::unique_ptr<Type*[]>(arrayOfType);
		std::vector<Type*> outputVector;
		for (size_t i = 0; i < arraySize; ++i) {
			outputVector.emplace_back(doublePointerArray[i]);
		}
		return outputVector;
	}

	/// Returns the sign of the given input value.
	/// @return The sign as an integer -1, 0 or +1.
	template <typename Type> int Sign(const Type& value) {
		return (Type(0) < value) - (Type(0) > value);
	}

	/// Exponential decay function. Allows decaying a value from A to B at the same rate regardless of delta time. Always stable.
	///
	/// As an example, assuming variables like this:
	/// float current = 1.0f;
	/// float target = 0.0f;
	/// float decay = 10.0f;
	///
	/// Then running this:
	///
	/// current = ExpDecay(current, target, decay, 1.0f / 30.0f);
	///
	/// Preduces the same result (barring precision errors) as:
	/// 
	/// current = ExpDecay(current, target, decay, 1.0f / 60.0f);
	/// current = ExpDecay(current, target, decay, 1.0f / 60.0f);
	///
	/// In both cases, "current" will be equal to about 0.716531310573789.
	/// 
	/// Lecture on the topic: https://youtube.com/watch?v=LSNQuFEDOyQ
	///
	/// @param current The current value that we're decaying.
	/// @param target Target we're decaying to.
	/// @param decay Rate of decay. For example, with 0.7 the value will be decayed about halfway there in 1 second.
	/// @param deltaTime Amount of time of decay to simulate.
	/// @returns The decayed value.
	inline float ExpDecay(float current, float target, float decay, float deltaTime) {
		return target + (current - target) * DetMath::Exp(-decay * deltaTime);
	}
		
#pragma endregion
} // namespace RTE
