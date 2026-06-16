#include "WebPortal.h"
#include "Settings.h"
#include <AsyncJson.h>
#include <ElegantOTA.h>
#include <ESPmDNS.h>

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
<p><a href=/update>Firmware update (OTA)</a> · <a href=/api/status>status JSON</a></p>
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
 o[k]=t=='c'?e.checked:t=='n'?Number(e.value):e.value;}
 o.orreryBodies=ORRERY.filter(b=>document.getElementById('_orr_'+b).checked).join(',');
 o.watchlist=document.getElementById('_watchlist').value.split(',').map(s=>s.trim()).filter(Boolean);
 fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)})
 .then(r=>r.json()).then(_=>{msg.textContent='saved (some settings apply on reboot)';});}
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

  // OTA + settings basic-auth (spec §13).
  ElegantOTA.setAuth(_s->getString("otaUser", "admin").c_str(),
                     _s->getString("otaPass", "overhead").c_str());
  ElegantOTA.begin(&_server);

  _server.begin();
  return true;
}

void WebPortal::loop() { ElegantOTA.loop(); }
