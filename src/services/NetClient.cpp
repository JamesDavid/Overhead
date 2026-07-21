#include "NetClient.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

bool NetClient::begin(uint32_t stackWords, BaseType_t core) {
  // Depth must cover the boot fan-out (~16 providers refresh at once: TLE x3, SpaceWx x5,
  // launch, aircraft, weather, aviation, pmap, sounding, hazards, mars). An 8-deep queue
  // overflowed and get() dropped the losers with no retry, so a feed that lost the boot race
  // (often launch) stayed stale for hours. Jobs run sequentially on the NetTask regardless of
  // depth, so this only buffers pending requests -- it doesn't raise the concurrent-TLS peak.
  _reqQ  = xQueueCreate(24, sizeof(Job*));
  _respQ = xQueueCreate(24, sizeof(Job*));
  if (!_reqQ || !_respQ) return false;
  BaseType_t ok = xTaskCreatePinnedToCore(
      taskEntry, "NetTask", stackWords, this, 1, nullptr, core);
  return ok == pdPASS;
}

bool NetClient::get(const String& url, Callback cb, StreamParse inTask) {
  Job* job = new Job();
  job->url    = url;
  job->cb     = std::move(cb);
  job->inTask = std::move(inTask);
  if (xQueueSend(_reqQ, &job, 0) != pdTRUE) { delete job; return false; }
  _inFlight++;
  return true;
}

bool NetClient::get(const String& url, Callback cb) {
  Job* job = new Job();
  job->url = url;
  job->cb  = std::move(cb);
  if (xQueueSend(_reqQ, &job, 0) != pdTRUE) {   // never block the UI thread
    delete job;
    return false;
  }
  _inFlight++;
  return true;
}

void NetClient::poll() {
  Job* job = nullptr;
  while (xQueueReceive(_respQ, &job, 0) == pdTRUE) {
    if (job->cb) job->cb(job->code, job->body);   // runs on the UI thread
    delete job;
    if (_inFlight) _inFlight--;
  }
}

void NetClient::taskEntry(void* arg) { static_cast<NetClient*>(arg)->taskLoop(); }

void NetClient::taskLoop() {
  Job* job = nullptr;
  for (;;) {
    if (xQueueReceive(_reqQ, &job, portMAX_DELAY) == pdTRUE) {
      perform(job);
      xQueueSend(_respQ, &job, portMAX_DELAY);
      // Let the UI thread drain (free) this response body before the next TLS
      // fetch checks the heap floor — otherwise a lingering large body trips the
      // guard and the next provider gets skipped (the boot fetch-storm problem).
      vTaskDelay(pdMS_TO_TICKS(120));
    }
  }
}

void NetClient::perform(Job* job) {
  if (WiFi.status() != WL_CONNECTED) { job->code = -1; return; }

  // TLS needs a large contiguous block; attempting it when the heap is too
  // fragmented OOMs and can corrupt the heap (lfs_close assert). Skip instead
  // and let the provider serve stale / retry later (cyd-radio §15 mbedtls floor).
  if (job->url.startsWith("https://")) {
#if defined(BOARD_HAS_PSRAM)
    // TLS handshake buffers are routed to PSRAM (mbedtls_platform_set_calloc_free in
    // setup), so gate on the largest free PSRAM block, not internal RAM.
    size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#else
    // No-PSRAM CYD: mbedtls allocates from internal RAM. MALLOC_CAP_INTERNAL == the
    // 8BIT heap here, but is explicit about the pool TLS actually draws from.
    size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
#endif
    if (blk < 42000) {
      job->code = -3;
      _httpsSkips++;
      Serial.printf("[net] skip (low heap, %u total) %.40s\n", (unsigned)_httpsSkips, job->url.c_str());
      return;
    }
  }

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setUserAgent("Overhead/0.1");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // Build the body String, OR (if an in-task parser is supplied) stream the response
  // straight through it without buffering — avoids a huge contiguous String alloc.
  auto consume = [&]() {
    job->code = http.GET();
    if (job->code > 0) {
      if (job->inTask) job->inTask(job->code, http.getStream());
      else             job->body = http.getString();
    }
    http.end();
  };

  bool ok;
  if (job->url.startsWith("https://")) {
    WiFiClientSecure secure;
    secure.setInsecure();             // hobby LAN device — no cert pinning
    ok = http.begin(secure, job->url);
    if (ok) consume(); else job->code = -2;
  } else {
    ok = http.begin(job->url);
    if (ok) consume(); else job->code = -2;
  }
}
