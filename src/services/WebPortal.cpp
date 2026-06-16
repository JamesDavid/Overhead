#include "WebPortal.h"
#include "Settings.h"
#include "../core/App.h"
#include "../hal/Display.h"
#include <AsyncJson.h>
#include <ElegantOTA.h>
#include <ESPmDNS.h>
#include <array>

static const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Overhead</title>
<style>body{font:15px system-ui;margin:1rem;max-width:40rem;background:#0b1018;color:#e6e8f0}
a{color:#50aaff}h3{margin:1.1rem 0 .3rem;border-bottom:1px solid #233;color:#9bf}
label{display:flex;justify-content:space-between;align-items:center;gap:1rem;padding:.18rem 0}
input,select{background:#11161f;color:#e6e8f0;border:1px solid #334;border-radius:4px;padding:.25rem;min-width:11rem}
button{background:#2563c0;color:#fff;border:0;border-radius:6px;padding:.5rem 1rem;font-size:15px;margin-top:.6rem}
#msg{color:#5d2}</style></head>
<body><h2>Overhead settings</h2>
<p><a href=/remote>Remote control</a> · <a href=/update>Firmware update (OTA)</a> · <a href=/api/status>status JSON</a></p>
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
 ['Web/OTA'],['otaUser','user','t'],['otaPass','password','t'],
];
const ORRERY=['Roadster','Psyche','Ceres','Vesta'];  // selectable minor bodies (astro catalog)
let cur={};
fetch('/api/settings').then(r=>r.json()).then(d=>{cur=d;build()});
function build(){let h='';for(const r of F){if(r.length==1){h+=`<h3>${r[0]}</h3>`;continue;}
 const[k,l,t,o]=r,v=cur[k];
 if(t=='c')h+=`<label>${l}<input type=checkbox id=_${k} ${v?'checked':''}></label>`;
 else if(t=='sel')h+=`<label>${l}<select id=_${k}>${o.map(x=>`<option ${x==v?'selected':''}>${x}</option>`).join('')}</select></label>`;
 else h+=`<label>${l}<input id=_${k} type=${t=='n'?'number':'text'} step=any value="${v??''}"></label>`;}
 h+='<h3>Orrery bodies</h3>';const ob=cur.orreryBodies||'';
 for(const b of ORRERY)h+=`<label>${b}<input type=checkbox id=_orr_${b} ${ob.includes(b)?'checked':''}></label>`;
 h+='<h3>Satellite watchlist</h3><label>names (comma-sep)'
   +`<input id=_watchlist type=text value="${(cur.watchlist||[]).join(', ')}"></label>`;
 f.innerHTML=h;}
function save(){const o={};for(const r of F){if(r.length==1)continue;const[k,l,t]=r,e=document.getElementById('_'+k);
 if(t=='n'){if(e.value==='')continue;o[k]=Number(e.value);}else o[k]=t=='c'?e.checked:e.value;}
 o.orreryBodies=ORRERY.filter(b=>document.getElementById('_orr_'+b).checked).join(',');
 o.watchlist=document.getElementById('_watchlist').value.split(',').map(s=>s.trim()).filter(Boolean);
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
img{width:100%;max-width:480px;aspect-ratio:4/3;border:1px solid #334;cursor:crosshair;touch-action:none}
button{font-size:16px;padding:.4rem 1rem;margin:.3rem;background:#2563c0;color:#fff;border:0;border-radius:6px}
a{color:#50aaff}</style></head><body>
<h3>Overhead remote</h3>
<div><img id=s alt="screen"></div>
<div><button onclick=sw('prev')>&#9664; prev</button>
<button onclick=ref()>refresh</button>
<button onclick=sw('next')>next &#9654;</button></div>
<p id=m>tap the screen to interact</p>
<p><a href=/>settings</a></p>
<script>
const img=document.getElementById('s'),m=document.getElementById('m');let busy=false;
function ref(){img.src='/api/screen.bmp?t='+Date.now();}
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

  if (MDNS.begin(hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[web] http://%s.local/\n", hostname.c_str());
  }

  _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", kIndexHtml);
  });

  _server.on("/remote", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", kRemoteHtml);
  });

  _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc;
    if (_statusFn) _statusFn(doc);
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String out; serializeJson(_s->doc(), out);
    req->send(200, "application/json", out);
  });

  // POST/PUT JSON body -> merge changed keys into settings and persist.
  auto* setHandler = new AsyncCallbackJsonWebHandler("/api/settings",
      [this](AsyncWebServerRequest* req, JsonVariant& json) {
        if (!json.is<JsonObject>()) { req->send(400, "application/json", "{\"ok\":false}"); return; }
        for (JsonPair kv : json.as<JsonObject>()) _s->doc()[kv.key()] = kv.value();
        _s->save();
        req->send(200, "application/json", "{\"ok\":true}");
      });
  _server.addHandler(setHandler);

  // --- Debug/automation: screenshot (downsampled BMP, streamed from a RAM buffer
  // so the async task never blocks on SPI). ?fmt= probes the read-back colour order.
  _server.on("/api/screen.bmp", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_display) { req->send(503, "text/plain", "no display"); return; }
    _display->requestShot();
    uint32_t t0 = millis();
    while (!_display->shotReady() && millis() - t0 < 800) delay(5);
    if (!_display->shotReady()) { req->send(503, "text/plain", "capture timeout"); return; }

    const int W = Display::kShotW, H = Display::kShotH;
    const uint16_t* px = _display->shot();
    const uint32_t rowBytes = (uint32_t)W * 3, body = rowBytes * H, total = 54 + body;
    std::array<uint8_t, 54> hdr{};
    auto u32 = [&](int o, uint32_t v) { hdr[o] = v; hdr[o+1] = v>>8; hdr[o+2] = v>>16; hdr[o+3] = v>>24; };
    hdr[0]='B'; hdr[1]='M'; u32(2,total); u32(10,54); u32(14,40);
    u32(18,(uint32_t)W); u32(22,(uint32_t)(-H));    // negative height = top-down
    hdr[26]=1; hdr[28]=24; u32(34,body); u32(38,2835); u32(42,2835);

    AsyncWebServerResponse* res = req->beginChunkedResponse("image/bmp",
      [px, W, rowBytes, total, hdr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
        if (index >= total) return 0;
        size_t n = 0;
        while (n < maxLen && index + n < total) {
          uint32_t p = index + n;
          if (p < 54) { buf[n++] = hdr[p]; continue; }
          uint32_t bp = p - 54, row = bp / rowBytes, col = (bp % rowBytes) / 3, ch = (bp % rowBytes) % 3;
          // This panel's read-back, after a byte-swap, packs the channels as
          // hi5=B, mid6=R, lo5=G (verified against bg + green + white). BMP wants B,G,R.
          uint16_t c = px[row * W + col];
          c = (uint16_t)((c >> 8) | (c << 8));
          uint8_t B = ((c >> 11) & 0x1f) << 3, R = ((c >> 5) & 0x3f) << 2, G = (c & 0x1f) << 3;
          buf[n++] = ch == 0 ? B : ch == 1 ? G : R;
        }
        return n;
      });
    req->send(res);
  });

  // --- Debug/automation: drive the UI ---
  _server.on("/api/tap", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_app) { req->send(503); return; }
    int x = req->hasParam("x") ? req->getParam("x")->value().toInt() : 0;
    int y = req->hasParam("y") ? req->getParam("y")->value().toInt() : 0;
    _app->injectTap(x, y);
    req->send(200, "application/json", "{\"ok\":true}");
  });
  _server.on("/api/swipe", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!_app) { req->send(503); return; }
    String d = req->hasParam("dir") ? req->getParam("dir")->value() : String("next");
    _app->injectSwipe((d == "prev" || d == "left") ? -1 : 1);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // OTA + settings basic-auth (spec §13).
  ElegantOTA.setAuth(_s->getString("otaUser", "admin").c_str(),
                     _s->getString("otaPass", "overhead").c_str());
  ElegantOTA.begin(&_server);

  _server.begin();
  return true;
}

void WebPortal::loop() { ElegantOTA.loop(); }
