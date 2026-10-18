/*
** Cortex Command: deterministic maths for LuaJIT.
**
** The C library's sin, cos, exp, log, pow and the like differ between platforms (and, with Microsoft's, between CPUs) in the last bits of their results,
** so scripts would work out different numbers on different machines, which a lockstep online game can't have. These are the engine's own versions
** (Source/System/DetMath.cpp), which give the same results everywhere. The interpreter (vm_x64.dasc), the JIT's call table (lj_ir.c) and constant
** folding (lj_vmmath.c) all use them. fmod, sqrt, floor, ceil and ldexp are exact everywhere already, and fmod only goes through here for the
** interpreter's macro.
*/

#ifndef _LJ_DETMATH_H
#define _LJ_DETMATH_H

#ifdef __cplusplus
extern "C" {
#endif

double rte_detmath_sin(double x);
double rte_detmath_cos(double x);
double rte_detmath_tan(double x);
double rte_detmath_asin(double x);
double rte_detmath_acos(double x);
double rte_detmath_atan(double x);
double rte_detmath_atan2(double y, double x);
double rte_detmath_sinh(double x);
double rte_detmath_cosh(double x);
double rte_detmath_tanh(double x);
double rte_detmath_exp(double x);
double rte_detmath_log(double x);
double rte_detmath_log2(double x);
double rte_detmath_log10(double x);
double rte_detmath_pow(double x, double y);
double rte_detmath_fmod(double x, double y);

#ifdef __cplusplus
}
#endif

#endif
