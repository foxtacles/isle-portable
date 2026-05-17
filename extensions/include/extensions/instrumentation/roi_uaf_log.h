#ifndef EXTENSIONS_INSTRUMENTATION_ROI_UAF_LOG_H
#define EXTENSIONS_INSTRUMENTATION_ROI_UAF_LOG_H

#ifdef __EMSCRIPTEN__

#ifdef __cplusplus
extern "C" {
#endif

void roi_uaf_log_release(const void* p_roi, const char* p_name, const char* p_site);
void roi_uaf_log_access(const void* p_roi, const char* p_site);
void roi_uaf_log_install(void);

#ifdef __cplusplus
}
#endif

#else

static inline void roi_uaf_log_release(const void*, const char*, const char*) {}
static inline void roi_uaf_log_access(const void*, const char*) {}
static inline void roi_uaf_log_install(void) {}

#endif

#endif
