#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#ifdef _WIN32
#   define WOORT_IMPORT __declspec(dllimport)
#   define WOORT_EXPORT __declspec(dllexport)
#else
#   define WOORT_IMPORT extern
#   define WOORT_EXPORT extern
#endif

#ifdef WOORT_AS_DYLIB
#   ifdef WOORT_IMPL
#       define WOORT_API WOORT_EXPORT
#   else
#       define WOORT_API WOORT_IMPORT
#   endif
#else
#   define WOORT_API
#endif

    WOORT_API void woort_init(void);
    WOORT_API void woort_shutdown(void);

#undef WOORT_API

#ifdef __cplusplus
}
#endif // __cplusplus
