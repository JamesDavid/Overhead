#include "WebPortal.h"
#include "Settings.h"
#include "../core/App.h"
#include "../hal/Display.h"
#include <AsyncJson.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <array>

static const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Overhead</title>
<link rel=stylesheet href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<style>body{font:15px system-ui;margin:1rem;max-width:40rem;background:#0b1018;color:#e6e8f0}
a{color:#50aaff}h3{margin:1.1rem 0 .3rem;border-bottom:1px solid #233;color:#9bf}
label{display:flex;justify-content:space-between;align-items:center;gap:1rem;padding:.18rem 0}
input,select{background:#11161f;color:#e6e8f0;border:1px solid #334;border-radius:4px;padding:.25rem;min-width:11rem}
button{background:#2563c0;color:#fff;border:0;border-radius:6px;padding:.5rem 1rem;font-size:15px;margin-top:.6rem}
#map{height:220px;margin:.4rem 0;border:1px solid #334;border-radius:6px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:.1rem 1rem}
.grid label{justify-content:flex-start;gap:.5rem}#msg{color:#5d2}</style></head>
<body><h2>Overhead settings</h2>
<p><a href=/remote>Remote control</a> · <a href=/update>Firmware update (OTA)</a> · <a href=/api/status>status JSON</a></p>
<h3>Location</h3><p style=margin:.2rem;color:#9bf>click the map to set your spot</p>
<div id=map></div>
<form id=f></form><button onclick=save()>Save</button> <span id=msg></span>
<script>
const F=[
 ['Location'],['locMode','mode','sel',['auto','preset']],['locName','name','t'],['locLat','lat','n'],['locLon','lon','n'],
 ['Appearance'],['themeMode','theme','sel',['auto','day','night']],['nightPalette','night palette','sel',['dark','red']],
 ['nightBacklight','night backlight','n'],['themeNightAlt','theme night sun-alt','n'],['dimAfterSec','dim after (s)','n'],['dimLevel','dim level','n'],
 ['Focus'],['focusEnabled','enabled','c'],['ambientDay','ambient day tab','t'],['ambientNight','ambient night tab','t'],
 ['passLeadMin','pass lead (min)','n'],['launchLeadMin','launch lead (min)','n'],['satMinEl','min pass el','n'],
 ['nightAmbientAlt','night ambient sun-alt','n'],['inactivitySec','inactivity->auto (s)','n'],
 ['Aircraft'],['adsbMode','mode','sel',['cloud','local']],['adsbHost','local host','t'],['adsbRadiusNm','radius (nm)','n'],
 ['Refresh'],['refreshLaunchMin','launches (min)','n'],['refreshTleHour','TLE (h)','n'],['refreshSpaceWxMin','space wx (min)','n'],['refreshWeatherMin','weather (min)','n'],
 ['Network'],['hostname','mDNS name (x.local)','t'],['debugShots','remote screenshots (uses 16KB)','c'],
 ['Web/OTA'],['otaUser','user','t'],['otaPass','password','t'],
];
const ORRERY=['Roadster','Psyche','Ceres','Vesta'];  // selectable minor bodies (astro catalog)
// Satellite presets: [display, watchlist token]. Token matches Celestrak names
// (case-insensitive CONTAINS), so designations w/o a catalog token use the real name.
const SATS=[['ISS','ISS'],['Tiangong (CSS)','CSS'],['Hubble','HST'],['SO-50','SO-50'],
 ['AO-91','FOX-1B'],['SatGus','SATGUS'],['NOAA-15','NOAA 15'],['NOAA-18','NOAA 18'],
 ['NOAA-19','NOAA 19'],['METEOR-M2','METEOR-M2'],['Starlink','STARLINK'],['GOES-18','GOES 18']];
let cur={},map,mk;
fetch('/api/settings').then(r=>r.json()).then(d=>{cur=d;build();initMap();});
function build(){let h='';for(const r of F){if(r.length==1){h+=`<h3>${r[0]}</h3>`;continue;}
 const[k,l,t,o]=r,v=cur[k];
 if(t=='c')h+=`<label>${l}<input type=checkbox id=_${k} ${v?'checked':''}></label>`;
 else if(t=='sel')h+=`<label>${l}<select id=_${k}>${o.map(x=>`<option ${x==v?'selected':''}>${x}</option>`).join('')}</select></label>`;
 else h+=`<label>${l}<input id=_${k} type=${t=='n'?'number':'text'} step=any value="${v??''}"></label>`;}
 const wl=(cur.watchlist||[]).map(s=>s.toUpperCase());
 h+='<h3>Satellites to track</h3><div class=grid>';
 for(const[lbl,val]of SATS)h+=`<label><input type=checkbox id=_sat_${val.replace(/\W/g,'')} ${wl.some(w=>w.includes(val.toUpperCase()))?'checked':''}>${lbl}</label>`;
 h+='</div><label>more (comma-sep names)'
   +`<input id=_satx type=text value="${(cur.watchlist||[]).filter(s=>!SATS.some(p=>s.toUpperCase().includes(p[1].toUpperCase()))).join(', ')}"></label>`;
 h+='<h3>Celestial bodies (orrery)</h3><div class=grid>';const ob=cur.orreryBodies||'';
 for(const b of ORRERY)h+=`<label><input type=checkbox id=_orr_${b} ${ob.includes(b)?'checked':''}>${b}</label>`;
 h+='</div>';
 f.innerHTML=h;}
function initMap(){try{
 const lat=Number(document.getElementById('_locLat').value)||34, lon=Number(document.getElementById('_locLon').value)||-118;
 map=L.map('map').setView([lat,lon],4);
 L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:13}).addTo(map);
 mk=L.marker([lat,lon],{draggable:true}).addTo(map);
 const setll=ll=>{document.getElementById('_locLat').value=ll.lat.toFixed(4);
  document.getElementById('_locLon').value=(((ll.lng+540)%360)-180).toFixed(4);
  const ms=document.getElementById('_locMode');if(ms)ms.value='preset';};
 map.on('click',e=>{mk.setLatLng(e.latlng);setll(e.latlng);});
 mk.on('dragend',()=>setll(mk.getLatLng()));
}catch(e){}}
function save(){const o={};for(const r of F){if(r.length==1)continue;const[k,l,t]=r,e=document.getElementById('_'+k);
 if(t=='n'){if(e.value==='')continue;o[k]=Number(e.value);}else o[k]=t=='c'?e.checked:e.value;}
 o.orreryBodies=ORRERY.filter(b=>document.getElementById('_orr_'+b).checked).join(',');
 const sats=SATS.filter(p=>document.getElementById('_sat_'+p[1].replace(/\W/g,'')).checked).map(p=>p[1]);
 const extra=document.getElementById('_satx').value.split(',').map(s=>s.trim()).filter(Boolean);
 o.watchlist=[...new Set([...sats,...extra])];
 fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)})
 .then(r=>r.json()).then(_=>{msg.textContent='saved (some settings apply on reboot)';});}
</script></body></html>
)HTML";

// Remote-control page: live (downsampled) screenshot, click-to-tap, swipe buttons.
static const char kRemoteHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Overhead remote</title>
<style>body{background:#0b1018;color:#e6e8f0;font:14px system-ui;text-align:center;margin:1rem}
img{width:100%;max-width:480px;height:auto;background:#11161f;border:1px solid #334;cursor:crosshair;touch-action:none;display:block;margin:0 auto}
button{font-size:16px;padding:.4rem 1rem;margin:.3rem;background:#2563c0;color:#fff;border:0;border-radius:6px}
a{color:#50aaff}</style></head><body>
<h3>Overhead remote</h3>
<table style="margin:0 auto;border-collapse:collapse">
<tr><td><img id=s alt="screen" width=320 height=240></td>
<td style="vertical-align:middle">
<button style="margin:0;display:block" onclick=sw('up')>&#9650;</button>
<button style="margin:0;display:block" onclick=sw('down')>&#9660;</button></td></tr>
<tr><td><button onclick=sw('prev')>&#9664; prev</button>
<button onclick=ref()>refresh</button>
<button onclick=sw('next')>next &#9654;</button></td>
<td></td></tr></table>
<p id=m>tap the screen to interact</p>
<p><a href=/>settings</a></p>
<script>
const img=document.getElementById('s'),m=document.getElementById('m');let busy=false;
function ref(){img.src='/api/screen.jpg?t='+Date.now();}
img.onload=()=>{busy=false;setTimeout(ref,1300);};
img.onerror=()=>setTimeout(ref,1800);
function tap(e){const r=img.getBoundingClientRect();
 const x=Math.round((e.clientX-r.left)/r.width*320),y=Math.round((e.clientY-r.top)/r.height*240);
 fetch('/api/tap?x='+x+'&y='+y).then(()=>{m.textContent='tap '+x+','+y;setTimeout(ref,300);});}
function sw(d){fetch('/api/swipe?dir='+d).then(()=>setTimeout(ref,400));}
img.addEventListener('click',tap);
ref();
</script></body></html>
)HTML";

bool WebPortal::begin(Settings* s, const String& hostname) {
  _s = s;
  _apiUser = _s->getString("otaUser", "admin");      // gate the API with the OTA creds
  _apiPass = _s->getString("otaPass", "overhead");

  String host = _s->getString("hostname", hostname.c_str());   // editable mDNS name (settings)
  host.trim(); if (!host.length()) host = hostname;
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[web] http://%s.local/\n", host.c_str());
  }

  _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", kIndexHtml);
  }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());

  _server.on("/remote", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", kRemoteHtml);
  }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());

  _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc;
    if (_statusFn) _statusFn(doc);
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());

  _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String out; serializeJson(_s->doc(), out);
    req->send(200, "application/json", out);
  }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());

  // POST/PUT JSON body -> merge changed keys into settings and persist.
  auto* setHandler = new AsyncCallbackJsonWebHandler("/api/settings",
      [this](AsyncWebServerRequest* req, JsonVariant& json) {
        if (!json.is<JsonObject>()) { req->send(400, "application/json", "{\"ok\":false}"); return; }
        for (JsonPair kv : json.as<JsonObject>()) _s->doc()[kv.key()] = kv.value();
        _s->save();
        req->send(200, "application/json", "{\"ok\":true}");
      });
  setHandler->setAuthentication(_apiUser.c_str(), _apiPass.c_str());
  _server.addHandler(setHandler);

  // --- Debug/automation: full-res JPEG screenshot. The UI thread encodes into an
  // on-demand buffer; we stream it and free it when done (no permanent heap hit).
  _server.on("/api/screen.jpg", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_display) { req->send(503, "text/plain", "no display"); return; }
    Display* disp = _display;
    disp->requestShot();
    uint32_t t0 = millis();
    while (!disp->shotReady() && millis() - t0 < 1200) delay(5);
    if (!disp->shotReady() || !disp->jpegLen()) { req->send(503, "text/plain", "capture failed"); return; }

    const uint8_t* jpg = disp->jpeg();
    size_t len = disp->jpegLen();
    AsyncWebServerResponse* res = req->beginChunkedResponse("image/jpeg",
      [jpg, len](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
        if (index >= len) return 0;
        size_t k = (len - index < maxLen) ? (len - index) : maxLen;
        memcpy(buf, jpg + index, k);
        return k;
      });
    req->send(res);
  }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());

  // --- Debug/automation: drive the UI ---
  _server.on("/api/tap", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_app) { req->send(503); return; }
    int x = req->hasParam("x") ? req->getParam("x")->value().toInt() : 0;
    int y = req->hasParam("y") ? req->getParam("y")->value().toInt() : 0;
    _app->injectTap(x, y);
    req->send(200, "application/json", "{\"ok\":true}");
  }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());
  _server.on("/api/swipe", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_app) { req->send(503); return; }
    String d = req->hasParam("dir") ? req->getParam("dir")->value() : String("next");
    if      (d == "up")   _app->injectScroll(-30);     // vertical scroll (e.g. agenda list)
    else if (d == "down") _app->injectScroll(30);
    else                  _app->injectSwipe((d == "prev" || d == "left") ? -1 : 1);
    req->send(200, "application/json", "{\"ok\":true}");
  }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());

  // Non-destructive file upload to LittleFS (e.g. /airports.bin) - raw POST body
  // streamed to the file, so data files update without wiping the partition like
  // uploadfs does. Open on the LAN like the other /api endpoints (backlog: gate it).
  _server.on("/api/fs", HTTP_POST,
    [](AsyncWebServerRequest* req) { req->send(200, "application/json", "{\"ok\":true}"); },
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        String p = req->hasParam("path") ? req->getParam("path")->value() : String("/upload.bin");
        if (!p.startsWith("/")) p = "/" + p;
        _up = LittleFS.open(p, "w");
      }
      if (_up) _up.write(data, len);
      if (_up && index + len == total) _up.close();
    }).setAuthentication(_apiUser.c_str(), _apiPass.c_str());

  // OTA + settings basic-auth (spec §13).
  ElegantOTA.setAuth(_s->getString("otaUser", "admin").c_str(),
                     _s->getString("otaPass", "overhead").c_str());
  ElegantOTA.begin(&_server);

  _server.begin();
  return true;
}

void WebPortal::loop() { ElegantOTA.loop(); }
