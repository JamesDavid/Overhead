#pragma once
#include <Arduino.h>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// services/NetClient — queued, non-blocking HTTP off the UI path (spec §3.3, §9).
//
// All blocking work (TCP, TLS, parse) runs in a FreeRTOS task pinned to core 0
// (NetTask). The UI thread enqueues a job with get(); the task performs it and
// hands the finished job back through a response queue. poll() — called from the
// UI loop — invokes the callback ON THE UI THREAD, so providers/pages never need
// locking and never touch the network from a tick or touch handler.
//
// Bodies are returned as String for milestone 1. Streaming parse + ArduinoJson
// document filters for the large LL2/ADS-B payloads (spec §3.1) are added
// per-provider on top of this, by giving the job an in-task parse callback.
class NetClient {
public:
  using Callback = std::function<void(int httpCode, const String& body)>;

  // stackBytes: ESP-IDF task stack is in BYTES. TLS (mbedtls) is stack-heavy,
  // so HTTPS fetches (Celestrak, SWPC, Open-Meteo) need >=16 KB or they fail.
  bool begin(uint32_t stackBytes = 16384, BaseType_t core = 0);
  bool get(const String& url, Callback cb);   // returns false if the queue is full
  void poll();                                 // UI thread: dispatch completed jobs
  size_t inFlight() const { return _inFlight; }
  size_t httpsSkips() const { return _httpsSkips; }   // HTTPS jobs skipped at the heap floor

private:
  struct Job { String url; Callback cb; int code = 0; String body; };

  static void taskEntry(void* arg);
  void taskLoop();
  void perform(Job* job);

  QueueHandle_t  _reqQ  = nullptr;
  QueueHandle_t  _respQ = nullptr;
  volatile size_t _inFlight = 0;
  volatile size_t _httpsSkips = 0;     // count of fetches skipped under the heap floor
};
