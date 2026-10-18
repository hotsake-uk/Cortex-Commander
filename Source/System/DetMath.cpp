#include "DetMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Every function here is built only from +, -, *, /, sqrt and the exact helpers (fabs, floor, fmod, ldexp, frexp), which IEEE 754 makes give the same result everywhere,
// as long as the compiler doesn't fuse a multiply and an add into one instruction or reorder the sums. The build pins floating point for that (/fp:precise and
// -ffp-contract=off), which this file depends on.
// The polynomials are the classic ones from Sun's fdlibm, so the results are within an ulp or so of the correctly rounded ones over the ranges a game uses.

namespace RTE::DetMath {

	namespace {
		constexpr double c_NaN = std::numeric_limits<double>::quiet_NaN();
		constexpr double c_Infinity = std::numeric_limits<double>::infinity();

		constexpr double c_Pi = 3.14159265358979311600e+00;
		constexpr double c_PiLo = 1.22464679914735317720e-16;
		constexpr double c_HalfPi = 1.57079632679489655800e+00;
		constexpr double c_HalfPiLo = 6.12323399573676603587e-17;
		constexpr double c_TwoOverPi = 6.36619772367581382433e-01;

		// pi/2 split into pieces whose products with a whole number up to about 2^20 are exact, for taking whole quarter turns off an angle (Cody and Waite).
		constexpr double c_HalfPi1 = 1.57079632673412561417e+00;
		constexpr double c_HalfPi2 = 6.07710050630396597660e-11;
		constexpr double c_HalfPi3 = 2.02226624871116645580e-21;
		constexpr double c_HalfPi3Tail = 8.47842766036889956997e-32;

		constexpr double c_Ln2Hi = 6.93147180369123816490e-01;
		constexpr double c_Ln2Lo = 1.90821492927058770002e-10;
		constexpr double c_InvLn2 = 1.44269504088896338700e+00;
		constexpr double c_InvLn10 = 4.34294481903251816668e-01;

		/// sin on [-pi/4, pi/4].
		double SinKernel(double x) {
			constexpr double S1 = -1.66666666666666324348e-01;
			constexpr double S2 = 8.33333333332248946124e-03;
			constexpr double S3 = -1.98412698298579493134e-04;
			constexpr double S4 = 2.75573137070700676789e-06;
			constexpr double S5 = -2.50507602534068634195e-08;
			constexpr double S6 = 1.58969099521155010221e-10;
			double z = x * x;
			return x + x * z * (S1 + z * (S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)))));
		}

		/// cos on [-pi/4, pi/4].
		double CosKernel(double x) {
			constexpr double C1 = 4.16666666666666019037e-02;
			constexpr double C2 = -1.38888888888741095749e-03;
			constexpr double C3 = 2.48015872894767294178e-05;
			constexpr double C4 = -2.75573143513906633035e-07;
			constexpr double C5 = 2.08757232129817482790e-09;
			constexpr double C6 = -1.13596475577881948265e-11;
			double z = x * x;
			double r = z * z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
			double halfZ = 0.5 * z;
			double w = 1.0 - halfZ;
			// The 1 - z/2 is worked out with what its rounding lost added back, which keeps cos near 1 accurate.
			return w + (((1.0 - w) - halfZ) + r);
		}

		/// Takes whole quarter turns off an angle. Returns how many (mod 4) and leaves what's left, in [-pi/4, pi/4], in the reduced angle.
		int ReduceQuarterTurns(double x, double& reduced) {
			// Angles this large have lost all their sub-turn precision anyway; taking whole turns off first keeps the quarter-turn count small enough to be exact.
			if (std::fabs(x) > 1.0e6) {
				x = std::fmod(x, 6.28318530717958623200e+00);
			}
			double turns = std::floor(x * c_TwoOverPi + 0.5);
			reduced = (((x - turns * c_HalfPi1) - turns * c_HalfPi2) - turns * c_HalfPi3) - turns * c_HalfPi3Tail;
			int64_t quarter = static_cast<int64_t>(turns);
			return static_cast<int>(quarter & 3);
		}

		/// atan on [0, infinity), by reducing to a small argument around one of a few points whose atan is known exactly.
		double AtanPositive(double x) {
			constexpr double atanHi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00};
			constexpr double atanLo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17};
			constexpr double aT[] = {3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01, -1.11111104054623557880e-01, 9.09088713343650656196e-02, -7.69187620504482999495e-02,
			                         6.66107313738753120669e-02, -5.83357013379057348645e-02, 4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02};
			if (x != x) {
				return x;
			}
			if (x > 1.0e17) {
				return atanHi[3] + atanLo[3];
			}
			int id;
			if (x < 0.4375) {
				id = -1;
			} else if (x < 0.6875) {
				id = 0;
				x = (2.0 * x - 1.0) / (2.0 + x);
			} else if (x < 1.1875) {
				id = 1;
				x = (x - 1.0) / (x + 1.0);
			} else if (x < 2.4375) {
				id = 2;
				x = (x - 1.5) / (1.0 + 1.5 * x);
			} else {
				id = 3;
				x = -1.0 / x;
			}
			double z = x * x;
			double w = z * z;
			double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
			double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
			if (id < 0) {
				return x - x * (s1 + s2);
			}
			return atanHi[id] - ((x * (s1 + s2) - atanLo[id]) - x);
		}

		/// log(1 + f) - f for the f that Log leaves, in [sqrt(2)/2 - 1, sqrt(2) - 1].
		double LogKernel(double f) {
			constexpr double Lg1 = 6.666666666666735130e-01;
			constexpr double Lg2 = 3.999999999940941908e-01;
			constexpr double Lg3 = 2.857142874366239149e-01;
			constexpr double Lg4 = 2.222219843214978396e-01;
			constexpr double Lg5 = 1.818357216161805012e-01;
			constexpr double Lg6 = 1.531383769920937332e-01;
			constexpr double Lg7 = 1.479819860511658591e-01;
			double s = f / (2.0 + f);
			double z = s * s;
			double w = z * z;
			double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
			double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
			double r = t2 + t1;
			double halfFSquared = 0.5 * f * f;
			return s * (halfFSquared + r) - halfFSquared;
		}

		/// Splits a positive, finite x into 2^exponent * (1 + f), with 1 + f in [sqrt(2)/2, sqrt(2)).
		void SplitForLog(double x, int& exponent, double& f) {
			double mantissa = std::frexp(x, &exponent); // In [0.5, 1).
			if (mantissa < 0.70710678118654752440) {
				mantissa *= 2.0;
				--exponent;
			}
			f = mantissa - 1.0;
		}
	} // namespace

	double Sin(double x) {
		if (!std::isfinite(x)) {
			return c_NaN;
		}
		double r;
		switch (ReduceQuarterTurns(x, r)) {
			case 0: return SinKernel(r);
			case 1: return CosKernel(r);
			case 2: return -SinKernel(r);
			default: return -CosKernel(r);
		}
	}

	double Cos(double x) {
		if (!std::isfinite(x)) {
			return c_NaN;
		}
		double r;
		switch (ReduceQuarterTurns(x, r)) {
			case 0: return CosKernel(r);
			case 1: return -SinKernel(r);
			case 2: return -CosKernel(r);
			default: return SinKernel(r);
		}
	}

	double Tan(double x) {
		if (!std::isfinite(x)) {
			return c_NaN;
		}
		double r;
		int quarter = ReduceQuarterTurns(x, r);
		double s = SinKernel(r);
		double c = CosKernel(r);
		return (quarter & 1) ? -c / s : s / c;
	}

	double Atan(double x) {
		return x < 0.0 ? -AtanPositive(-x) : AtanPositive(x);
	}

	double Atan2(double y, double x) {
		if (x != x || y != y) {
			return c_NaN;
		}
		if (y == 0.0) {
			// The signs of zeros pick the side, as the standard atan2 does.
			if (std::signbit(x)) {
				return std::signbit(y) ? -c_Pi : c_Pi;
			}
			return y;
		}
		if (x == 0.0) {
			return y > 0.0 ? c_HalfPi : -c_HalfPi;
		}
		if (std::isinf(x)) {
			if (std::isinf(y)) {
				double angle = x > 0.0 ? c_Pi / 4.0 : 3.0 * c_Pi / 4.0;
				return y > 0.0 ? angle : -angle;
			}
			if (x > 0.0) {
				return std::signbit(y) ? -0.0 : 0.0;
			}
			return y > 0.0 ? c_Pi : -c_Pi;
		}
		if (std::isinf(y)) {
			return y > 0.0 ? c_HalfPi : -c_HalfPi;
		}

		double ratio = std::fabs(y / x);
		double angle;
		if (std::isinf(ratio) || ratio > 1.0e17) {
			angle = c_HalfPi + 0.5 * c_HalfPiLo;
		} else if (x < 0.0 && ratio < 1.0e-17) {
			angle = 0.0;
		} else {
			angle = AtanPositive(ratio);
		}
		if (x < 0.0) {
			angle = c_Pi - (angle - c_PiLo);
		}
		return y < 0.0 ? -angle : angle;
	}

	double Asin(double x) {
		if (x != x || x > 1.0 || x < -1.0) {
			return c_NaN;
		}
		return Atan2(x, std::sqrt((1.0 - x) * (1.0 + x)));
	}

	double Acos(double x) {
		if (x != x || x > 1.0 || x < -1.0) {
			return c_NaN;
		}
		return Atan2(std::sqrt((1.0 - x) * (1.0 + x)), x);
	}

	double Exp(double x) {
		constexpr double P1 = 1.66666666666666019037e-01;
		constexpr double P2 = -2.77777777770155933842e-03;
		constexpr double P3 = 6.61375632143793436117e-05;
		constexpr double P4 = -1.65339022054652515390e-06;
		constexpr double P5 = 4.13813679705723846039e-08;
		if (x != x) {
			return x;
		}
		if (x > 709.782712893383973096) {
			return c_Infinity;
		}
		if (x < -745.13321910194110842) {
			return 0.0;
		}
		// x = k * ln(2) + r, with |r| <= ln(2) / 2, so exp(x) = 2^k * exp(r).
		double k = std::floor(x * c_InvLn2 + 0.5);
		double hi = x - k * c_Ln2Hi;
		double lo = k * c_Ln2Lo;
		double r = hi - lo;
		double z = r * r;
		double c = r - z * (P1 + z * (P2 + z * (P3 + z * (P4 + z * P5))));
		double expR = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
		return std::ldexp(expR, static_cast<int>(k));
	}

	double Exp2(double x) {
		if (x != x) {
			return x;
		}
		// Whole powers of two are exact.
		if (x == std::floor(x) && std::fabs(x) < 2000.0) {
			return std::ldexp(1.0, static_cast<int>(x));
		}
		double whole = std::floor(x);
		return std::ldexp(Exp((x - whole) * c_Ln2Hi + (x - whole) * c_Ln2Lo), static_cast<int>(std::fmax(-2000.0, std::fmin(2000.0, whole))));
	}

	double Log(double x) {
		if (x != x || x < 0.0) {
			return c_NaN;
		}
		if (x == 0.0) {
			return -c_Infinity;
		}
		if (std::isinf(x)) {
			return x;
		}
		int exponent;
		double f;
		SplitForLog(x, exponent, f);
		double k = static_cast<double>(exponent);
		return k * c_Ln2Hi + ((f + LogKernel(f)) + k * c_Ln2Lo);
	}

	double Log2(double x) {
		if (x != x || x < 0.0) {
			return c_NaN;
		}
		if (x == 0.0) {
			return -c_Infinity;
		}
		if (std::isinf(x)) {
			return x;
		}
		int exponent;
		double f;
		SplitForLog(x, exponent, f);
		// The whole part of the answer is exact, so only the fraction's log is scaled.
		return static_cast<double>(exponent) + (f + LogKernel(f)) * c_InvLn2;
	}

	double Log10(double x) {
		return Log(x) * c_InvLn10;
	}

	double Pow(double x, double y) {
		if (y == 0.0) {
			return 1.0;
		}
		if (x == 1.0) {
			return 1.0;
		}
		if (x != x || y != y) {
			return c_NaN;
		}
		if (y == 1.0) {
			return x;
		}
		if (y == 2.0) {
			return x * x;
		}
		if (y == 0.5 && x >= 0.0) {
			return std::sqrt(x);
		}
		if (y == -1.0) {
			return 1.0 / x;
		}

		bool yIsWhole = std::isfinite(y) && y == std::floor(y);
		// Small whole powers are worked out by repeated squaring, which is exact more often and closer otherwise.
		if (yIsWhole && std::fabs(y) <= 64.0) {
			int64_t power = static_cast<int64_t>(std::fabs(y));
			double base = x;
			double result = 1.0;
			while (power > 0) {
				if (power & 1) {
					result *= base;
				}
				base *= base;
				power >>= 1;
			}
			return y < 0.0 ? 1.0 / result : result;
		}

		if (x == 0.0) {
			bool oddWhole = yIsWhole && std::fmod(std::fabs(y), 2.0) == 1.0;
			if (y < 0.0) {
				return oddWhole && std::signbit(x) ? -c_Infinity : c_Infinity;
			}
			return oddWhole ? x : 0.0;
		}
		if (std::isinf(y)) {
			double magnitude = std::fabs(x);
			if (magnitude == 1.0) {
				return 1.0;
			}
			return (magnitude > 1.0) == (y > 0.0) ? c_Infinity : 0.0;
		}
		if (std::isinf(x)) {
			if (x > 0.0) {
				return y > 0.0 ? c_Infinity : 0.0;
			}
			bool oddWhole = yIsWhole && std::fmod(std::fabs(y), 2.0) == 1.0;
			if (y > 0.0) {
				return oddWhole ? -c_Infinity : c_Infinity;
			}
			return oddWhole ? -0.0 : 0.0;
		}

		double sign = 1.0;
		if (x < 0.0) {
			if (!yIsWhole) {
				return c_NaN;
			}
			if (std::fmod(std::fabs(y), 2.0) == 1.0) {
				sign = -1.0;
			}
			x = -x;
		}
		return sign * Exp(y * Log(x));
	}

	double Cbrt(double x) {
		if (x == 0.0 || !std::isfinite(x)) {
			return x;
		}
		double magnitude = std::fabs(x);
		double root = Exp(Log(magnitude) / 3.0);
		// One Newton step tidies up what exp and log lost.
		root = root - (root * root * root - magnitude) / (3.0 * root * root);
		return x < 0.0 ? -root : root;
	}

	double Hypot(double x, double y) {
		x = std::fabs(x);
		y = std::fabs(y);
		if (std::isinf(x) || std::isinf(y)) {
			return c_Infinity;
		}
		if (x != x || y != y) {
			return c_NaN;
		}
		double larger = x > y ? x : y;
		double smaller = x > y ? y : x;
		if (larger == 0.0) {
			return 0.0;
		}
		// Scaled by the larger one, so the squares can't overflow or underflow.
		double ratio = smaller / larger;
		return larger * std::sqrt(1.0 + ratio * ratio);
	}

	namespace {
		/// sinh for |x| < 0.5, by its series, as exp(x) - exp(-x) loses most of its digits that close to 0.
		double SinhSeries(double x) {
			double z = x * x;
			// x + x^3/3! + ... + x^13/13!, which is below an ulp off for |x| < 0.5.
			return x + x * z * (1.0 / 6.0 + z * (1.0 / 120.0 + z * (1.0 / 5040.0 + z * (1.0 / 362880.0 + z * (1.0 / 39916800.0 + z * (1.0 / 6227020800.0))))));
		}
	} // namespace

	double Sinh(double x) {
		if (std::fabs(x) < 0.5) {
			return SinhSeries(x);
		}
		double e = Exp(x);
		return 0.5 * (e - 1.0 / e);
	}

	double Cosh(double x) {
		double e = Exp(x);
		return 0.5 * (e + 1.0 / e);
	}

	double Tanh(double x) {
		if (x != x) {
			return x;
		}
		if (x > 22.0) {
			return 1.0;
		}
		if (x < -22.0) {
			return -1.0;
		}
		if (std::fabs(x) < 0.5) {
			double sinh = SinhSeries(x);
			return sinh / std::sqrt(1.0 + sinh * sinh);
		}
		double e = Exp(2.0 * x);
		return (e - 1.0) / (e + 1.0);
	}
} // namespace RTE::DetMath

// For LuaJIT, so scripts' maths (math.sin, x ^ y and the rest, in the interpreter and in compiled traces alike) is the same as the engine's on every platform.
// See external/sources/LuaJIT-2.1/src/lj_detmath.h.
extern "C" {
double rte_detmath_sin(double x) { return RTE::DetMath::Sin(x); }
double rte_detmath_cos(double x) { return RTE::DetMath::Cos(x); }
double rte_detmath_tan(double x) { return RTE::DetMath::Tan(x); }
double rte_detmath_asin(double x) { return RTE::DetMath::Asin(x); }
double rte_detmath_acos(double x) { return RTE::DetMath::Acos(x); }
double rte_detmath_atan(double x) { return RTE::DetMath::Atan(x); }
double rte_detmath_atan2(double y, double x) { return RTE::DetMath::Atan2(y, x); }
double rte_detmath_sinh(double x) { return RTE::DetMath::Sinh(x); }
double rte_detmath_cosh(double x) { return RTE::DetMath::Cosh(x); }
double rte_detmath_tanh(double x) { return RTE::DetMath::Tanh(x); }
double rte_detmath_exp(double x) { return RTE::DetMath::Exp(x); }
double rte_detmath_log(double x) { return RTE::DetMath::Log(x); }
double rte_detmath_log2(double x) { return RTE::DetMath::Log2(x); }
double rte_detmath_log10(double x) { return RTE::DetMath::Log10(x); }
double rte_detmath_pow(double x, double y) { return RTE::DetMath::Pow(x, y); }
double rte_detmath_fmod(double x, double y) { return std::fmod(x, y); }
}
