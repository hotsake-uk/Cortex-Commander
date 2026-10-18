/*
** Cortex Command: stand-ins for the engine's deterministic maths functions (see lj_detmath.h), for the luajit.exe that msvc-postbuild.bat links
** to check the library, and then deletes. The game links the real ones from Source/System/DetMath.cpp.
*/

#include <math.h>

double rte_detmath_sin(double x) { return sin(x); }
double rte_detmath_cos(double x) { return cos(x); }
double rte_detmath_tan(double x) { return tan(x); }
double rte_detmath_asin(double x) { return asin(x); }
double rte_detmath_acos(double x) { return acos(x); }
double rte_detmath_atan(double x) { return atan(x); }
double rte_detmath_atan2(double y, double x) { return atan2(y, x); }
double rte_detmath_sinh(double x) { return sinh(x); }
double rte_detmath_cosh(double x) { return cosh(x); }
double rte_detmath_tanh(double x) { return tanh(x); }
double rte_detmath_exp(double x) { return exp(x); }
double rte_detmath_log(double x) { return log(x); }
double rte_detmath_log2(double x) { return log(x) * 1.4426950408889634074; }
double rte_detmath_log10(double x) { return log10(x); }
double rte_detmath_pow(double x, double y) { return pow(x, y); }
double rte_detmath_fmod(double x, double y) { return fmod(x, y); }
