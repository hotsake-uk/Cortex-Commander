#pragma once

#include <cmath>
#include <type_traits>

namespace RTE {

	/// Maths functions that give exactly the same results on every platform, compiler and CPU, for the simulation.
	/// The standard library's sin, cos, atan2, exp, log, pow and the like are each library's own (MSVC's, glibc's, Apple's), and differ from one another in the last bits of
	/// their results. Microsoft's even picks a different version depending on which instructions the CPU has. A lockstep game needs every machine to work out exactly the
	/// same numbers, so these are worked out here from only the operations IEEE 754 fixes exactly (+, -, *, /, sqrt), in double precision.
	/// Float overloads work in double and round the result to float, which is also exact.
	/// sqrt, fabs, floor, ceil, round, trunc, fmod, ldexp and frexp are exact everywhere already, so the standard ones are fine for those.
	namespace DetMath {
		double Sin(double x);
		double Cos(double x);
		double Tan(double x);
		double Asin(double x);
		double Acos(double x);
		double Atan(double x);
		double Atan2(double y, double x);
		double Exp(double x);
		double Exp2(double x);
		double Log(double x);
		double Log2(double x);
		double Log10(double x);
		double Pow(double x, double y);
		double Cbrt(double x);
		double Hypot(double x, double y);
		double Sinh(double x);
		double Cosh(double x);
		double Tanh(double x);

		inline float Sin(float x) { return static_cast<float>(Sin(static_cast<double>(x))); }
		inline float Cos(float x) { return static_cast<float>(Cos(static_cast<double>(x))); }
		inline float Tan(float x) { return static_cast<float>(Tan(static_cast<double>(x))); }
		inline float Asin(float x) { return static_cast<float>(Asin(static_cast<double>(x))); }
		inline float Acos(float x) { return static_cast<float>(Acos(static_cast<double>(x))); }
		inline float Atan(float x) { return static_cast<float>(Atan(static_cast<double>(x))); }
		inline float Atan2(float y, float x) { return static_cast<float>(Atan2(static_cast<double>(y), static_cast<double>(x))); }
		inline float Exp(float x) { return static_cast<float>(Exp(static_cast<double>(x))); }
		inline float Exp2(float x) { return static_cast<float>(Exp2(static_cast<double>(x))); }
		inline float Log(float x) { return static_cast<float>(Log(static_cast<double>(x))); }
		inline float Log2(float x) { return static_cast<float>(Log2(static_cast<double>(x))); }
		inline float Log10(float x) { return static_cast<float>(Log10(static_cast<double>(x))); }
		inline float Pow(float x, float y) { return static_cast<float>(Pow(static_cast<double>(x), static_cast<double>(y))); }
		inline float Cbrt(float x) { return static_cast<float>(Cbrt(static_cast<double>(x))); }
		inline float Hypot(float x, float y) { return static_cast<float>(Hypot(static_cast<double>(x), static_cast<double>(y))); }
		inline float Sinh(float x) { return static_cast<float>(Sinh(static_cast<double>(x))); }
		inline float Cosh(float x) { return static_cast<float>(Cosh(static_cast<double>(x))); }
		inline float Tanh(float x) { return static_cast<float>(Tanh(static_cast<double>(x))); }

		// Mixed and whole-number arguments, as the standard functions take them, are worked out in double like the standard ones do.
		template <typename A, typename B>
		    requires(std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !(std::is_same_v<A, float> && std::is_same_v<B, float>))
		inline double Pow(A x, B y) { return Pow(static_cast<double>(x), static_cast<double>(y)); }
		template <typename A, typename B>
		    requires(std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !(std::is_same_v<A, float> && std::is_same_v<B, float>))
		inline double Atan2(A y, B x) { return Atan2(static_cast<double>(y), static_cast<double>(x)); }
		template <typename A, typename B>
		    requires(std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !(std::is_same_v<A, float> && std::is_same_v<B, float>))
		inline double Hypot(A x, B y) { return Hypot(static_cast<double>(x), static_cast<double>(y)); }
		template <typename A>
		    requires std::is_integral_v<A>
		inline double Sin(A x) { return Sin(static_cast<double>(x)); }
		template <typename A>
		    requires std::is_integral_v<A>
		inline double Cos(A x) { return Cos(static_cast<double>(x)); }
		template <typename A>
		    requires std::is_integral_v<A>
		inline double Exp(A x) { return Exp(static_cast<double>(x)); }
		template <typename A>
		    requires std::is_integral_v<A>
		inline double Log(A x) { return Log(static_cast<double>(x)); }
		template <typename A>
		    requires std::is_integral_v<A>
		inline double Log2(A x) { return Log2(static_cast<double>(x)); }
		template <typename A>
		    requires std::is_integral_v<A>
		inline double Log10(A x) { return Log10(static_cast<double>(x)); }
		template <typename A>
		    requires std::is_integral_v<A>
		inline double Cbrt(A x) { return Cbrt(static_cast<double>(x)); }
	} // namespace DetMath
} // namespace RTE
