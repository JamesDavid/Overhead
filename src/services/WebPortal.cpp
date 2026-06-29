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
<style>*{box-sizing:border-box}body{font:15px system-ui;margin:0;background:#0b1018;color:#e6e8f0;display:flex;min-height:100vh}
#nav{width:9.5rem;background:#0e131c;border-right:1px solid #233;padding:.5rem;flex:none}
#nav h2{font-size:1rem;color:#9bf;margin:.2rem .3rem .6rem}
.tab{display:block;width:100%;text-align:left;background:none;color:#cdd;border:0;padding:.5rem .6rem;border-radius:6px;cursor:pointer;margin:.1rem 0;font-size:15px}
.tab.on{background:#2563c0;color:#fff}
#main{flex:1;padding:1rem;max-width:44rem}
.sec{display:none}.sec.on{display:block}
h3{color:#9bf;border-bottom:1px solid #233;margin:.2rem 0 .5rem}
label{display:flex;justify-content:space-between;align-items:center;gap:1rem;padding:.18rem 0}
input,select{background:#11161f;color:#e6e8f0;border:1px solid #334;border-radius:4px;padding:.25rem;width:13rem;flex:none}
.row input{flex:1 1 auto}
button{background:#2563c0;color:#fff;border:0;border-radius:6px;padding:.45rem 1rem;margin:.2rem .3rem .2rem 0;cursor:pointer;font-size:15px}
#map,#smap{height:240px;margin:.4rem 0;border:1px solid #334;border-radius:6px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:.1rem 1rem}.grid label{justify-content:flex-start;gap:.5rem}
.row{display:flex;gap:.4rem;align-items:center;flex-wrap:wrap;margin:.3rem 0}
table{width:100%;border-collapse:collapse;margin:.3rem 0}td{padding:.25rem;border-bottom:1px solid #233}
#msg{color:#5d2;font-size:.85rem;margin:.3rem}.hint{color:#789;font-size:.82rem;margin:.1rem 0 .4rem}
a{color:#50aaff}</style></head>
<body>
<div id=nav><h2>Overhead</h2><div id=tabs></div>
<button onclick=save()>Save</button><div id=msg></div>
<p style="font-size:.8rem"><a href=/remote>remote</a> · <a href=/update>OTA</a> · <a href=/api/status>status</a></p></div>
<div id=main></div>
<script>
const FIELD={
 locName:['name','t'],locLat:['latitude','n'],locLon:['longitude','n'],locMode:['source','sel',['auto','preset']],
 themeMode:['theme','sel',['auto','day','night']],nightPalette:['night palette','sel',['dark','red']],
 nightBacklight:['night backlight','n'],themeNightAlt:['night sun-alt (day→night)','n'],themeRedAlt:['red sun-alt (night→red)','n'],dimAfterSec:['dim after (s)','n'],dimLevel:['dim level','n'],
 focusEnabled:['focus enabled','c'],passLeadMin:['pass lead (min)','n'],launchLeadMin:['launch lead (min)','n'],satMinEl:['min pass el','n'],
 alertSat:['alert: satellite pass','c'],alertLaunch:['alert: launch','c'],alertAircraft:['alert: aircraft overhead','c'],alertWx:['alert: weather','c'],
 audioEnabled:['Morse beeper on alerts','c'],audioKochWpm:['Koch WPM (character)','n'],audioFarnsworthWpm:['Farnsworth WPM (effective)','n'],audioBeepAtNight:['beep at night too','c'],audioToneHz:['tone Hz (PWM speaker; not the CrowPanel buzzer)','n'],
 nightAmbientAlt:['night ambient sun-alt','n'],inactivitySec:['inactivity->auto (s)','n'],
 adsbMode:['mode','sel',['cloud','local']],adsbHost:['local host','t'],adsbRadiusNm:['radius (nm)','n'],
 refreshLaunchMin:['launches (min)','n'],refreshTleHour:['TLE (h)','n'],refreshSpaceWxMin:['space wx (min)','n'],refreshWeatherMin:['weather (min)','n'],
 hostname:['mDNS name','t'],debugShots:['remote screenshots','c'],dispRotation:['rotation 0-7 (even=landscape, +4=mirror; CYD variants)','n'],dispInvert:['invert colours','c'],dispBgr:['swap R/B — BGR (dual-USB CYD; reboot)','c'],webAuth:['require login — basic auth (reboot)','c'],otaUser:['user','t'],otaPass:['password','t']};
const SECTIONS=[['Location','loc'],['Focus','focus'],['Satellites','sats'],['Bodies','bodies'],['Memory Skies','skies'],
 ['Appearance',['themeMode','nightPalette','nightBacklight','themeNightAlt','themeRedAlt','dimAfterSec','dimLevel']],
 ['Aircraft',['adsbMode','adsbHost','adsbRadiusNm']],
 ['System',['hostname','debugShots','refreshLaunchMin','refreshTleHour','refreshSpaceWxMin','refreshWeatherMin','inactivitySec','webAuth','otaUser','otaPass']],
 ['Screen',['dispRotation','dispInvert','dispBgr']],
 ['Audio',['audioEnabled','audioKochWpm','audioFarnsworthWpm','audioToneHz','audioBeepAtNight']]];
const PAGES=['Agenda','Launches','Aircraft','Aviation Wx','Satellites','Space Wx','Solar System','Star Map'];
const ORRERY=['Roadster','Psyche','Ceres','Vesta'];
const SATS=[['ISS','ISS'],['Tiangong (CSS)','CSS'],['Hubble','HST'],['SO-50','SO-50'],['AO-91','FOX-1B'],['SatGus','SATGUS'],
 ['NOAA-15','NOAA 15'],['NOAA-18','NOAA 18'],['NOAA-19','NOAA 19'],['METEOR-M2','METEOR-M2'],['Starlink','STARLINK'],['GOES-18','GOES 18']];
let cur={},map,mk,mapDone=false,smap,smk,smapDone=false,editSky=-1;
const E=id=>document.getElementById(id);
fetch('/api/settings').then(r=>r.json()).then(d=>{cur=d;render();});

function fld(k){const[l,t,o]=FIELD[k],v=cur[k];
 if(t=='c')return `<label>${l}<input type=checkbox id=_${k} ${v?'checked':''}></label>`;
 if(t=='sel')return `<label>${l}<select id=_${k}>${o.map(x=>`<option ${x==v?'selected':''}>${x}</option>`).join('')}</select></label>`;
 return `<label>${l}<input id=_${k} type=${t=='n'?'number':'text'} step=any value="${v??''}"></label>`;}

function render(){
 tabs.innerHTML=SECTIONS.map((s,i)=>`<button class="tab${i?'':' on'}" data-s="${s[0]}" onclick="show('${s[0]}')">${s[0]}</button>`).join('');
 main.innerHTML=SECTIONS.map((s,i)=>{let b;
  if(s[1]=='loc')b=locHtml();else if(s[1]=='focus')b=focusHtml();else if(s[1]=='sats')b=satsHtml();
  else if(s[1]=='bodies')b=bodiesHtml();else if(s[1]=='skies')b=skiesHtml();else b=s[1].map(fld).join('');
  return `<div class="sec${i?'':' on'}" data-s="${s[0]}"><h3>${s[0]}</h3>${b}</div>`;}).join('');
 setTimeout(initMap,60);}

function show(n){[...document.querySelectorAll('.tab')].forEach(t=>t.classList.toggle('on',t.dataset.s==n));
 [...document.querySelectorAll('.sec')].forEach(t=>t.classList.toggle('on',t.dataset.s==n));
 if(n=='Location'){if(map)setTimeout(()=>map.invalidateSize(),60);else initMap();}
 if(n=='Memory Skies'){skyRows();if(smap)setTimeout(()=>smap.invalidateSize(),60);else setTimeout(initSkyMap,60);}}

function locHtml(){return `<p class=hint>Click the map, drag the pin, or search an address. Save spots and pick a default. <b>Source</b>: <i>auto</i> locates you over the internet (IP); <i>preset</i> uses the latitude/longitude below (set automatically when you pick on the map).</p>
 <div class=row><input id=_addr type=text placeholder="address or place" style="flex:1"><button onclick=geocode()>Find</button><button onclick=geoMe()>My location</button></div>
 <div id=map></div>${fld('locName')}${fld('locLat')}${fld('locLon')}${fld('locMode')}
 <div class=row><button onclick=saveLoc()>+ Save current as a location</button></div>
 <table id=loclist></table>`;}
function locRows(){const L=cur.locations||[];
 E('loclist').innerHTML=L.map((p,i)=>`<tr><td>${p.name||'(unnamed)'}</td><td>${(+p.lat).toFixed(2)}, ${(+p.lon).toFixed(2)}</td>
  <td style="text-align:right"><button onclick="useLoc(${i})">use</button><button onclick="delLoc(${i})">x</button></td></tr>`).join('');}
function saveLoc(){const n=E('_locName').value||('Spot '+((cur.locations||[]).length+1));
 cur.locations=[...(cur.locations||[]),{name:n,lat:Number(E('_locLat').value),lon:Number(E('_locLon').value)}];locRows();}
function useLoc(i){const p=cur.locations[i];E('_locName').value=p.name;E('_locLat').value=p.lat;E('_locLon').value=p.lon;E('_locMode').value='preset';
 if(mk){mk.setLatLng([p.lat,p.lon]);map.setView([p.lat,p.lon],8);}}
function delLoc(i){cur.locations.splice(i,1);locRows();}
function geocode(){const q=E('_addr').value.trim();if(!q)return;
 fetch('https://nominatim.openstreetmap.org/search?format=json&limit=1&q='+encodeURIComponent(q))
 .then(r=>r.json()).then(a=>{if(!a[0])return;const la=+a[0].lat,lo=+a[0].lon;
  E('_locLat').value=la.toFixed(4);E('_locLon').value=lo.toFixed(4);E('_locMode').value='preset';
  if(!E('_locName').value)E('_locName').value=(a[0].display_name||q).split(',')[0];
  if(mk){mk.setLatLng([la,lo]);map.setView([la,lo],8);}}).catch(e=>{msg.textContent='geocode failed';});}

// Browser geolocation -> fill the Location ('') or Memory-Skies ('sky') map + fields.
// Needs a secure context: most browsers block this on plain http://<ip>; falls back
// with a message. Works on https or localhost.
function geoMe(which){if(!navigator.geolocation){msg.textContent='no geolocation in this browser';return;}
 msg.textContent='locating…';
 navigator.geolocation.getCurrentPosition(p=>{const la=p.coords.latitude,lo=p.coords.longitude;
  if(which=='sky'){E('_skyLat').value=la.toFixed(4);E('_skyLon').value=lo.toFixed(4);
   if(smk){smk.setLatLng([la,lo]);smap.setView([la,lo],11);}}
  else{E('_locLat').value=la.toFixed(4);E('_locLon').value=lo.toFixed(4);E('_locMode').value='preset';
   if(mk){mk.setLatLng([la,lo]);map.setView([la,lo],11);}}
  msg.textContent='located';},
  e=>{msg.textContent='geolocation blocked (needs https)';},{enableHighAccuracy:true,timeout:8000});}
function focusHtml(){const d=(cur.ambientDay||'').split(',').map(s=>s.trim()),n=(cur.ambientNight||'').split(',').map(s=>s.trim());
 let h=`<p class=hint>Tick which tabs the device auto-tours when idle (day / night). No typing tab names.</p>
  <table><tr><td><b>tab</b></td><td>day</td><td>night</td></tr>`;
 for(const p of PAGES)h+=`<tr><td>${p}</td>
  <td><input type=checkbox class=fday value="${p}" ${d.includes(p)?'checked':''}></td>
  <td><input type=checkbox class=fnight value="${p}" ${n.includes(p)?'checked':''}></td></tr>`;
 h+='</table>'+['focusEnabled','passLeadMin','launchLeadMin','satMinEl','nightAmbientAlt','inactivitySec','alertSat','alertLaunch','alertAircraft','alertWx'].map(fld).join('');
 return h;}
function satsHtml(){const wl=(cur.watchlist||[]).map(s=>s.toUpperCase());
 let h='<p class=hint>Pick satellites to track (matched by name, case-insensitive contains).</p><div class=grid>';
 for(const[lbl,val]of SATS)h+=`<label><input type=checkbox class=satp value="${val}" ${wl.some(w=>w.includes(val.toUpperCase()))?'checked':''}>${lbl}</label>`;
 h+='</div><label>more (comma-sep)<input id=_satx type=text value="'+
  (cur.watchlist||[]).filter(s=>!SATS.some(p=>s.toUpperCase().includes(p[1].toUpperCase()))).join(', ')+'"></label>';
 return h;}
function bodiesHtml(){const ob=cur.orreryBodies||'';let h='<p class=hint>Extra minor bodies on the orrery.</p><div class=grid>';
 for(const b of ORRERY)h+=`<label><input type=checkbox class=orrp value="${b}" ${ob.includes(b)?'checked':''}>${b}</label>`;
 return h+'</div>';}
function skiesHtml(){const la=cur.locLat??'',lo=cur.locLon??'';
 return `<p class=hint>Saved skies — the exact stars overhead at a moment and place (a birthday, an anniversary, a first night under the stars). Swipe up/down on the device's Star Map to cycle through them.</p>
 <label>Title<input id=_skyTitle type=text placeholder="e.g. Mia's birth"></label>
 <label>Place name<input id=_skyPlace type=text placeholder="e.g. Phoenix, AZ"></label>
 <label>Date &amp; time (local)<input id=_skyWhen type=datetime-local></label>
 <p class=hint>Click the map, drag the pin, or search an address to set the place.</p>
 <div class=row><input id=_skyAddr type=text placeholder="address or place" style="flex:1"><button onclick=skyGeocode()>Find</button><button onclick=geoMe('sky')>My location</button></div>
 <div id=smap></div>
 <label>Latitude<input id=_skyLat type=number step=any value="${la}"></label>
 <label>Longitude<input id=_skyLon type=number step=any value="${lo}"></label>
 <div class=row><button id=_skyAdd onclick=addSky()>+ Add memory sky</button><button id=_skyCancel onclick=cancelSky() style="display:none">Cancel edit</button></div>
 <table id=skylist></table>`;}
function skyRows(){if(!E('skylist'))return;const L=cur.memorySkies||[];
 E('skylist').innerHTML=L.map((p,i)=>`<tr><td>${i===editSky?'✎ ':''}${p.title||'(untitled)'}</td><td>${new Date((p.epoch||0)*1000).toLocaleString()}</td>
  <td>${(+p.lat).toFixed(2)}, ${(+p.lon).toFixed(2)}</td><td style="text-align:right"><button onclick="editSkyAt(${i})">edit</button><button onclick="delSky(${i})">x</button></td></tr>`).join('');}
function initSkyMap(){if(smapDone||!E('smap'))return;try{
 const lat=Number(E('_skyLat').value)||34,lon=Number(E('_skyLon').value)||-118;
 smap=L.map('smap').setView([lat,lon],6);
 L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:13}).addTo(smap);
 smk=L.marker([lat,lon],{draggable:true}).addTo(smap);
 const setll=ll=>{E('_skyLat').value=ll.lat.toFixed(4);E('_skyLon').value=(((ll.lng+540)%360)-180).toFixed(4);};
 smap.on('click',e=>{smk.setLatLng(e.latlng);setll(e.latlng);});
 smk.on('dragend',()=>setll(smk.getLatLng()));smapDone=true;
}catch(e){}}
function skyGeocode(){const q=E('_skyAddr').value.trim();if(!q)return;
 fetch('https://nominatim.openstreetmap.org/search?format=json&limit=1&q='+encodeURIComponent(q))
 .then(r=>r.json()).then(a=>{if(!a[0])return;const la=+a[0].lat,lo=+a[0].lon;
  E('_skyLat').value=la.toFixed(4);E('_skyLon').value=lo.toFixed(4);
  if(!E('_skyPlace').value)E('_skyPlace').value=(a[0].display_name||q).split(',').slice(0,2).join(',').trim();
  if(smk){smk.setLatLng([la,lo]);smap.setView([la,lo],8);}}).catch(e=>{msg.textContent='geocode failed';});}
function editSkyAt(i){const p=cur.memorySkies[i];E('_skyTitle').value=p.title||'';E('_skyPlace').value=p.place||'';
 const d=new Date((p.epoch||0)*1000),pad=n=>String(n).padStart(2,'0');
 E('_skyWhen').value=`${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
 E('_skyLat').value=p.lat;E('_skyLon').value=p.lon;
 if(smk){smk.setLatLng([p.lat,p.lon]);smap.setView([p.lat,p.lon],8);}
 editSky=i;E('_skyAdd').textContent='Save changes';E('_skyCancel').style.display='';skyRows();
 msg.textContent='editing "'+(p.title||'sky')+'" - tweak then Save changes';}
function resetSkyForm(){editSky=-1;E('_skyTitle').value='';E('_skyPlace').value='';E('_skyWhen').value='';
 if(E('_skyAdd'))E('_skyAdd').textContent='+ Add memory sky';if(E('_skyCancel'))E('_skyCancel').style.display='none';}
function addSky(){const t=E('_skyTitle').value.trim(),w=E('_skyWhen').value;
 if(!w){msg.textContent='pick a date & time first';return;}
 const o={title:t||'Memory sky',place:E('_skyPlace').value.trim(),epoch:Math.floor(new Date(w).getTime()/1000),lat:Number(E('_skyLat').value),lon:Number(E('_skyLon').value)};
 cur.memorySkies=cur.memorySkies||[];
 if(editSky>=0)cur.memorySkies[editSky]=o;else cur.memorySkies.push(o);
 resetSkyForm();skyRows();}
function cancelSky(){resetSkyForm();skyRows();}
function delSky(i){cur.memorySkies.splice(i,1);if(editSky>=0&&i<=editSky)resetSkyForm();skyRows();}

function initMap(){if(mapDone||!E('map'))return;try{
 const lat=Number(E('_locLat').value)||34,lon=Number(E('_locLon').value)||-118;
 map=L.map('map').setView([lat,lon],6);
 L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:13}).addTo(map);
 mk=L.marker([lat,lon],{draggable:true}).addTo(map);
 const setll=ll=>{E('_locLat').value=ll.lat.toFixed(4);E('_locLon').value=(((ll.lng+540)%360)-180).toFixed(4);E('_locMode').value='preset';};
 map.on('click',e=>{mk.setLatLng(e.latlng);setll(e.latlng);});
 mk.on('dragend',()=>setll(mk.getLatLng()));mapDone=true;locRows();
}catch(e){}}

function save(){const o={};
 for(const k in FIELD){const e=E('_'+k);if(!e)continue;const t=FIELD[k][1];
  if(t=='c')o[k]=e.checked;else if(t=='n'){if(e.value==='')continue;o[k]=Number(e.value);}else o[k]=e.value;}
 o.ambientDay=[...document.querySelectorAll('.fday:checked')].map(c=>c.value).join(',');
 o.ambientNight=[...document.querySelectorAll('.fnight:checked')].map(c=>c.value).join(',');
 const sats=[...document.querySelectorAll('.satp:checked')].map(c=>c.value);
 const extra=(E('_satx')?E('_satx').value:'').split(',').map(s=>s.trim()).filter(Boolean);
 o.watchlist=[...new Set([...sats,...extra])];
 o.orreryBodies=[...document.querySelectorAll('.orrp:checked')].map(c=>c.value).join(',');
 if(cur.locations)o.locations=cur.locations;
 if(cur.memorySkies)o.memorySkies=cur.memorySkies;
 fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)})
 .then(r=>r.json()).then(_=>{msg.textContent='saved (some apply on reboot)';});}
</script></body></html>
)HTML";

// Remote-control page: live (downsampled) screenshot, click-to-tap, swipe buttons.
static const char kRemoteHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Overhead remote</title>
<style>body{background:#0b1018;color:#e6e8f0;font:14px system-ui;text-align:center;margin:1rem}
img{width:100%;max-width:560px;height:auto;background:#11161f;border:1px solid #334;cursor:crosshair;touch-action:none;display:block;margin:0 auto}
button{font-size:16px;padding:.4rem 1rem;margin:.3rem;background:#2563c0;color:#fff;border:0;border-radius:6px}
a{color:#50aaff}</style></head><body>
<h3>Overhead remote</h3>
<table style="margin:0 auto;border-collapse:collapse">
<tr><td><img id=s alt="screen" width=320 height=240></td>
<td style="vertical-align:middle">
<button style="margin:0 0 40px 0;width:50px;height:96px;font-size:22px;display:block" onclick=sw('up')>&#9650;</button>
<button style="margin:0;width:50px;height:96px;font-size:22px;display:block" onclick=sw('down')>&#9660;</button></td></tr>
<tr><td><button onclick=sw('prev')>&#9664; prev</button>
<button onclick=ref()>refresh</button>
<button onclick=sw('next')>next &#9654;</button></td>
<td></td></tr></table>
<p id=m>tap the screen to interact</p>
<p><label><input type=checkbox id=ss onchange=toggleShots()> remote screenshots / control (frees ~16KB when off)</label></p>
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
const ss=document.getElementById('ss');
fetch('/api/settings').then(r=>r.json()).then(d=>{ss.checked=!!d.debugShots;});
function toggleShots(){const on=ss.checked;
 fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({debugShots:on})})
  .then(()=>{m.textContent='remote screenshots '+(on?'ON':'OFF');if(on)ref();});}
ref();
</script></body></html>
)HTML";

bool WebPortal::begin(Settings* s, const String& hostname) {
  _s = s;
  // Auth is OFF by default (home-LAN convenience); flip "require login" in System settings to enforce
  // it. Empty creds make AsyncWebServer/ElegantOTA skip auth entirely (_hasCreds = user && pass).
  bool authOn = _s->getBool("webAuth", false);
  _apiUser = authOn ? _s->getString("otaUser", "admin")    : String("");
  _apiPass = authOn ? _s->getString("otaPass", "overhead") : String("");

  String host = _s->getString("hostname", hostname.c_str());   // editable mDNS name (settings)
  host.trim(); if (!host.length()) host = hostname;
  _host = host;
  // NOTE: routes are REGISTERED here but the listener is NOT started — call start()
  // for that (so a boot-off device never does a begin()->end() that wedges the socket).

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
        // Apply the remote-screenshot toggle LIVE so the web UI can free the 16 KB
        // buffer on demand (off -> freeShot()), not just at the next reboot.
        if (_display && json["debugShots"].is<bool>()) _display->setShotsEnabled(json["debugShots"].as<bool>());
        // Apply rotation/invert live (so a CYD-variant orientation can be dialed in from a phone).
        // Note: rotation desyncs the resistive touch cal — recalibrate via Health after settling on one.
        if (_display && (json["dispRotation"].is<int>() || json["dispInvert"].is<bool>()))
          _display->applyDisplayPrefs((int)_s->getInt("dispRotation", _display->rotation()), _s->getBool("dispInvert"));
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
      [disp, jpg, len](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
        if (index >= len) { disp->freeShot(); return 0; }   // done -> free the 16KB so TLS has heap
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
  _server.on("/api/beep", HTTP_GET, [this](AsyncWebServerRequest* req) {   // force a Morse beep (test)
    if (!_app) { req->send(503); return; }
    String w = req->hasParam("w") ? req->getParam("w")->value() : String("TEST");
    _app->beepTest(w);
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

  // OTA auth follows the same toggle (_apiUser/_apiPass are empty when webAuth is off -> no OTA login).
  ElegantOTA.setAuth(_apiUser.c_str(), _apiPass.c_str());
  ElegantOTA.begin(&_server);            // attach OTA handlers (no listener yet)
  return true;                           // start() opens the listener
}

void WebPortal::loop() { if (_running) ElegantOTA.loop(); }

void WebPortal::stop() {
  if (!_running) return;
  _server.end();                         // close the listener (routes stay registered)
  MDNS.end();
  _running = false;
  _everStopped = true;                   // any re-enable now needs a reboot (TIME_WAIT holds :80)
  Serial.printf("[web] server OFF  largest free block = %u\n", (unsigned)ESP.getMaxAllocHeap());
}

void WebPortal::start() {
  if (_running) return;
  _server.begin();
  if (MDNS.begin(_host.c_str())) { MDNS.addService("http", "tcp", 80); Serial.printf("[web] http://%s.local/\n", _host.c_str()); }
  _running = true;
  Serial.printf("[web] server ON   largest free block = %u\n", (unsigned)ESP.getMaxAllocHeap());
}
