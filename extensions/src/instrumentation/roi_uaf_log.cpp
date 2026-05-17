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

// 256 entries x 128 bytes = 32 KB per ring. Two rings = 64 KB. At thousands of
// access events per second the ring rolls in <100 ms — plenty of history right
// before a crash.
constexpr uint32_t kRingEntries = 256;
constexpr uint32_t kEntryBytes  = 128;

struct LogRing {
	std::atomic<uint32_t> head;
	char entries[kRingEntries][kEntryBytes];
};

static_assert(sizeof(std::atomic<uint32_t>) == 4, "atomic<uint32_t> must be 4 bytes");

// Two separate rings so a flood of access events cannot push out the older
// release events.
LogRing g_release_ring;
LogRing g_access_ring;

// Monotonic event counter so the JS-side dump can show ordering across rings.
std::atomic<uint32_t> g_event_clock{1};

// Idempotent install guard.
std::atomic<bool> g_installed{false};

inline void write_entry(LogRing& ring, const char* text)
{
	const uint32_t i = ring.head.fetch_add(1, std::memory_order_acq_rel) % kRingEntries;
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

			var relAddr   = $0;
			var accAddr   = $1;
			var nEntries  = $2;
			var entryLen  = $3;

			function readEntry(off, maxLen) {
				var end = off;
				var hard = off + maxLen;
				while (end < hard && HEAPU8[end] !== 0) { end++; }
				if (end === off) { return ""; }
				return UTF8ToString(off, end - off);
			}

			function dumpRing(addr, label) {
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
					enriched += dumpRing(relAddr, "RELEASE LOG");
					enriched += dumpRing(accAddr, "ACCESS LOG");
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
		(int) kRingEntries,
		(int) kEntryBytes
	);
}

#endif // __EMSCRIPTEN__
