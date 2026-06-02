#ifndef EXTENSIONS_INSTRUMENTATION_ROI_UAF_LOG_H
#define EXTENSIONS_INSTRUMENTATION_ROI_UAF_LOG_H

#ifdef __EMSCRIPTEN__

#ifdef __cplusplus
extern "C" {
#endif

void roi_uaf_log_release(const void* p_roi, const char* p_name, const char* p_site);
void roi_uaf_log_access(const void* p_roi, const char* p_site);
void roi_uaf_log_install(void);

// Bug A residual capture (#1828): a live-ROI registry so the animation walk can
// detect a slot in LegoAnimActorStruct::m_roiMap that points at a freed ROI
// (despite B3's SlotRefTracker nulling). track_alive in LegoROI ctor,
// track_dead in ~LegoROI (passing the slot-ref count the ROI had at death).
// anim_context records the current actor/anim before the tree walk; slot_check
// logs one ANIM-STALE entry into the existing ACC ring when a stale slot is hit.
void roi_uaf_track_alive(const void* p_roi);
void roi_uaf_track_dead(const void* p_roi, unsigned p_slotCount);
void roi_uaf_anim_context(const void* p_actor, int p_curAnim, const void* p_roiMap, unsigned p_numROIs);
void roi_uaf_anim_slot_check(const void* p_roi, unsigned p_index, const void* p_roiMap);

#ifdef __cplusplus
}
#endif

#else

static inline void roi_uaf_log_release(const void*, const char*, const char*) {}
static inline void roi_uaf_log_access(const void*, const char*) {}
static inline void roi_uaf_log_install(void) {}
static inline void roi_uaf_track_alive(const void*) {}
static inline void roi_uaf_track_dead(const void*, unsigned) {}
static inline void roi_uaf_anim_context(const void*, int, const void*, unsigned) {}
static inline void roi_uaf_anim_slot_check(const void*, unsigned, const void*) {}

#endif

#endif
