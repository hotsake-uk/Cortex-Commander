#pragma once

/// Debug function to print GL errors to the console from:
/// https : // stackoverflow.com/questions/11256470/define-a-macro-to-facilitate-opengl-command-debugging
void CheckOpenGLError(const char* stmt, const char* fname, int line);

/// Debug macro to be used for all GL calls. In a shipping build (RELEASE_BUILD and NDEBUG both set) it only makes the call: each check is a
/// glGetError, which can stall the driver, and the renderer makes hundreds of these calls a frame. Development builds keep the checks.
#if defined(RELEASE_BUILD) && defined(NDEBUG)
#define GL_CHECK(stmt) \
	do { \
		stmt; \
	} while (0)
#else
#define GL_CHECK(stmt) \
	do { \
		stmt; \
		CheckOpenGLError(#stmt, __FILE__, __LINE__); \
	} while (0)
#endif
