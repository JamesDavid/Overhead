#include "NetClient.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool NetClient::begin(uint32_t stackWords, BaseType_t core) {
  _reqQ  = xQueueCreate(8, sizeof(Job*));
  _respQ = xQueueCreate(8, sizeof(Job*));
  if (!_reqQ || !_respQ) return false;
  BaseType_t ok = xTaskCreatePinnedToCore(
      taskEntry, "NetTask", stackWords, this, 1, nullptr, core);
  return ok == pdPASS;
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
    }
  }
}

void NetClient::perform(Job* job) {
  if (WiFi.status() != WL_CONNECTED) { job->code = -1; return; }

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setUserAgent("Overhead/0.1");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  bool ok;
  if (job->url.startsWith("https://")) {
    WiFiClientSecure secure;
    secure.setInsecure();             // hobby LAN device — no cert pinning
    ok = http.begin(secure, job->url);
    if (ok) {
      job->code = http.GET();
      if (job->code > 0) job->body = http.getString();
      http.end();
    } else job->code = -2;
  } else {
    ok = http.begin(job->url);
    if (ok) {
      job->code = http.GET();
      if (job->code > 0) job->body = http.getString();
      http.end();
    } else job->code = -2;
  }
}
