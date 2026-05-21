#include "extensions/instrumentation/roi_uaf_log.h"

#ifdef __EMSCRIPTEN__

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <emscripten.h>
#include <emscripten/em_asm.h>
#include <emscripten/threading.h>

namespace {

// Stack column in the D1 crash_reports table is capped at 65,536 bytes
// (isle.pizza/server/src/crashes.ts). After the crash stack itself (~1.5 KB)
// and header/indent overhead, ~63 KB is available for ring content.
//
// v3 widens the ACC ring from 256 → 384 to match REL. ACC now logs many
// more sites (Init, Destroy, SetROI, World/PathCtrl::PlaceActor,
// SetXformDestEdge) so the ~16/256 utilisation seen in v2 #1520 is replaced
// by realistic 200–300 events/area-transition.
//
// Worst-case per-line dump size (with kEntryBytes=128 cap, longest current
// format strings): REL ~86 bytes, ACC ~90 bytes.
//
//   384 REL × 86 = ~33 KB
//   384 ACC × 90 = ~34 KB
//   stack + headers ≈ 2 KB
//   total worst    ≈ 69 KB → over 65 KB cap, but typical entries are 60–75
//   bytes (not the worst-case 90). Real-world dumps measured at ~50 KB.
constexpr uint32_t kRelRingEntries = 384;
constexpr uint32_t kAccRingEntries = 384;
constexpr uint32_t kEntryBytes     = 128;

template <uint32_t N>
struct LogRing {
	std::atomic<uint32_t> head;
	char entries[N][kEntryBytes];
};

static_assert(sizeof(std::atomic<uint32_t>) == 4, "atomic<uint32_t> must be 4 bytes");

// Two separate rings so a flood of access events cannot push out the older
// release events. Sized independently — release events are higher-rate.
LogRing<kRelRingEntries> g_release_ring;
LogRing<kAccRingEntries> g_access_ring;

// Monotonic event counter so the JS-side dump can show ordering across rings.
std::atomic<uint32_t> g_event_clock{1};

// Idempotent install guard.
std::atomic<bool> g_installed{false};

template <uint32_t N>
inline void write_entry(LogRing<N>& ring, const char* text)
{
	const uint32_t i = ring.head.fetch_add(1, std::memory_order_acq_rel) % N;
	// Zero out then copy so reader never sees a stale tail in this slot.
	std::memset(ring.entries[i], 0, kEntryBytes);
	std::strncpy(ring.entries[i], text, kEntryBytes - 1);
}

} // namespace

extern "C" void roi_uaf_log_release(const void* p_roi, const char* p_name, const char* p_site)
{
	char buf[kEntryBytes];
	const uint32_t t = g_event_clock.fetch_add(1, std::memory_order_relaxed);
	std::snprintf(
		buf, sizeof buf,
		"REL t=%u roi=0x%08x name=%-16s site=%s",
		t,
		static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p_roi)),
		p_name ? p_name : "?",
		p_site ? p_site : "?"
	);
	write_entry(g_release_ring, buf);
}

extern "C" void roi_uaf_log_access(const void* p_roi, const char* p_site)
{
	char buf[kEntryBytes];
	const uint32_t t = g_event_clock.fetch_add(1, std::memory_order_relaxed);
	std::snprintf(
		buf, sizeof buf,
		"ACC t=%u roi=0x%08x site=%s",
		t,
		static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p_roi)),
		p_site ? p_site : "?"
	);
	write_entry(g_access_ring, buf);
}

// ────────────────────────────────────────────────────────────────────────────
// Crash enricher: wraps Module.onAbort on the MAIN browser thread. With
// PROXY_TO_PTHREAD=1 our wasm runs on a worker, but the actual onAbort handler
// is on main. We pass the ring addresses as integers so the JS closure reads
// shared linear memory directly via HEAPU8/HEAPU32 — no cross-thread wasm call
// at crash time, since the worker may be in an undefined state by then.
// ────────────────────────────────────────────────────────────────────────────
extern "C" void roi_uaf_log_install(void)
{
	bool expected = false;
	if (!g_installed.compare_exchange_strong(expected, true)) {
		return;
	}

	MAIN_THREAD_EM_ASM(
		{
			if (Module.__roiUafLogInstalled) { return; }
			Module.__roiUafLogInstalled = true;

			var relAddr     = $0;
			var accAddr     = $1;
			var relEntries  = $2;
			var accEntries  = $3;
			var entryLen    = $4;

			function readEntry(off, maxLen) {
				var end = off;
				var hard = off + maxLen;
				while (end < hard && HEAPU8[end] !== 0) { end++; }
				if (end === off) { return ""; }
				return UTF8ToString(off, end - off);
			}

			function dumpRing(addr, nEntries, label) {
				try {
					// Layout: [u32 head][nEntries x entryLen bytes]
					var head = HEAPU32[addr >>> 2];
					var entriesAddr = addr + 4;
					var lines = [];
					// Oldest -> newest: (head % nEntries) is the next slot to
					// write, so (head) is the oldest still-live entry once
					// the ring has wrapped.
					for (var k = 0; k < nEntries; k++) {
						var i = (head + k) % nEntries;
						var off = entriesAddr + i * entryLen;
						var text = readEntry(off, entryLen);
						if (text) { lines.push(text); }
					}
					if (lines.length === 0) {
						return "\n[" + label + "] (empty)\n";
					}
					return "\n[" + label + "] " + lines.length + " entries\n  " + lines.join("\n  ") + "\n";
				} catch (e) {
					return "\n[" + label + "] (read error: " + e + ")\n";
				}
			}

			var orig = Module.onAbort;
			Module.onAbort = function(what) {
				var enriched = (what === undefined || what === null) ? "" : String(what);
				try {
					enriched += dumpRing(relAddr, relEntries, "RELEASE LOG");
					enriched += dumpRing(accAddr, accEntries, "ACCESS LOG");
				} catch (e) {
					enriched += "\n[roi_uaf_log enricher failed: " + e + "]\n";
				}
				if (typeof orig === "function") {
					return orig.call(this, enriched);
				}
				// No prior handler (e.g. single-threaded fallback): make it
				// visible at minimum.
				if (typeof console !== "undefined" && console.error) {
					console.error(enriched);
				}
			};
		},
		(int) reinterpret_cast<uintptr_t>(&g_release_ring),
		(int) reinterpret_cast<uintptr_t>(&g_access_ring),
		(int) kRelRingEntries,
		(int) kAccRingEntries,
		(int) kEntryBytes
	);
}

#endif // __EMSCRIPTEN__
