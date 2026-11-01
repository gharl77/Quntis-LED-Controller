#pragma once

namespace esphome {
    namespace quntis_web {

        static const char PAGE[] = R"QUNTIS(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lampe Quntis</title>
<style>
:root{color-scheme:dark;--bg:#14161a;--card:#1f2228;--line:#30343c;--text:#eef0f3;--dim:#9aa0aa;--accent:#ffb454;--bad:#ff7a70}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.4 system-ui,sans-serif}
main{max-width:480px;margin:auto;padding:16px}
h1{font-size:22px;margin:0;overflow-wrap:anywhere}
h2{font-size:15px;margin:0 0 12px;color:var(--dim);font-weight:600}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin:4px 0 16px}
select{padding:8px;border-radius:9px;border:1px solid var(--line);background:var(--card);color:var(--text);font-size:15px}
section{background:var(--card);border-radius:14px;padding:16px;margin-bottom:14px}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;min-height:44px}
label{display:block}
input[type=range]{width:100%;height:32px;accent-color:var(--accent)}
input[type=text],input[type=password]{width:100%;padding:11px;border-radius:9px;border:1px solid var(--line);background:var(--bg);color:var(--text);font-size:16px;margin-bottom:10px}
button{padding:11px 14px;border:1px solid var(--line);border-radius:9px;background:transparent;color:var(--text);font-size:15px;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#201505;font-weight:600}
button:disabled{opacity:.5;cursor:default}
.switch{position:relative;width:52px;height:30px;flex:none}
.switch input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer}
.switch i{position:absolute;inset:0;border-radius:15px;background:var(--line);pointer-events:none;transition:.15s}
.switch i:after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:#fff;transition:.15s}
.switch input:checked+i{background:var(--accent)}
.switch input:checked+i:after{left:25px}
.switch input:disabled+i{opacity:.4}
.value{color:var(--dim)}
.temp{background:linear-gradient(90deg,#ffb86b,#fff,#bcd6ff);border-radius:8px;height:8px;margin-top:-4px}
.presets{display:flex;gap:6px;margin-top:10px}
.presets button{flex:1;padding:8px 2px;font-size:14px}
#nets{list-style:none;margin:0 0 12px;padding:0;max-height:240px;overflow:auto}
#nets button{width:100%;display:flex;justify-content:space-between;gap:10px;border:0;border-bottom:1px solid var(--line);border-radius:0;text-align:left}
#nets span:first-child{overflow:hidden;text-overflow:ellipsis}
#nets .value{flex:none}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0}
dt{color:var(--dim)}dd{margin:0;text-align:right;overflow-wrap:anywhere}
.msg{margin:10px 0 0;color:var(--dim)}.msg.bad{color:var(--bad)}
.note{font-size:14px;color:var(--dim);margin:12px 0 0}
a{color:var(--accent)}
</style></head><body><main>
<header><h1 id="title">Lampe Quntis</h1>
<select id="lang" aria-label="Langue / Language"><option value="fr">Français</option><option value="en">English</option></select></header>

<section><h2 data-t="light"></h2>
<div class="row"><span data-t="lamp"></span><label class="switch"><input type="checkbox" id="on" data-a="lamp"><i></i></label></div>
<label><span data-t="brightness"></span> <span class="value" id="bv"></span><input type="range" id="brightness" min="10" max="100"></label>
<label><span data-t="temperature"></span> <span class="value" id="kv"></span><input type="range" id="kelvin" min="3000" max="6500" step="50"></label>
<div class="temp"></div>
<div class="presets" id="presets"><button data-k="3000">3000 K</button><button data-k="4000">4000 K</button><button data-k="5000">5000 K</button><button data-k="6500">6500 K</button></div>
<div class="row" style="margin-top:10px"><span data-t="backlight"></span><label class="switch"><input type="checkbox" id="backlight" data-a="backlight"><i></i></label></div>
<div class="row"><span data-t="autoDim"></span><label class="switch"><input type="checkbox" id="auto_dim" data-a="autoDim"><i></i></label></div>
<p class="note" data-t="note"></p>
</section>

<section><h2 data-t="information"></h2><dl id="info"></dl>
<p><a href="#" id="wifiopen" data-t="changeWifi"></a></p>
<button id="restart" data-t="restart"></button></section>

<section id="wifipanel" hidden>
<div class="row"><h2 style="margin:0" data-t="changeWifi"></h2><a href="#" id="wificlose" data-t="close"></a></div>
<ul id="nets"></ul>
<button id="scan"></button>
<form id="wifi" style="margin-top:14px">
<input type="text" id="ssid" data-p="networkName" maxlength="32" autocomplete="off" autocapitalize="none" spellcheck="false">
<input type="password" id="password" data-p="password" maxlength="64" autocomplete="new-password">
<button class="primary" id="save" data-t="save"></button>
</form>
<p class="msg" id="wifimsg" role="status"></p>
</section>
</main><script>
const TEXT={
fr:{light:'Lumière',lamp:'Lampe',brightness:'Luminosité',temperature:'Température',backlight:'Rétroéclairage',autoDim:'Variation automatique',
note:"La lampe ne renvoie pas son état : cette page montre le dernier ordre envoyé par l'ESP32, pas ce que fait la télécommande.",
information:'Informations',changeWifi:'Modifier le WiFi',restart:"Redémarrer l'ESP32",close:'Fermer',scan:'Balayer les réseaux',scanning:'Balayage en cours…',
networkName:'Nom du réseau',password:'Mot de passe',save:'Enregistrer et connecter',
network:'Réseau',ip:'Adresse IP',signal:'Signal WiFi',radio:'Module radio',radioOk:'OK, canal ',radioBad:'Ne répond pas, vérifie les fils',uptime:'En marche depuis',memory:'Mémoire libre',mac:'Adresse MAC',
excellent:'Excellent',good:'Bon',fair:'Moyen',weak:'Faible',day:' j ',kb:' ko',
shortPassword:'Le mot de passe doit avoir au moins 8 caractères.',noNetwork:'Choisis ou écris un réseau.',
confirmWifi:n=>'Connecter la lampe à « '+n+' » ? Cette page va se déconnecter.',
saved:h=>"Enregistré. La lampe se connecte. Si le réseau change, son adresse changera aussi : essaie http://"+h+" ou cherche-la dans ton routeur. En cas d'échec, connecte-toi à son WiFi de secours après environ une minute.",
refused:'La lampe a refusé ces informations.',confirmRestart:"Redémarrer l'ESP32 ?"},
en:{light:'Light',lamp:'Lamp',brightness:'Brightness',temperature:'Color temperature',backlight:'Backlight',autoDim:'Automatic dimming',
note:"The lamp does not report its state: this page shows the last command sent by the ESP32, not what the remote does.",
information:'Information',changeWifi:'Change WiFi',restart:'Restart the ESP32',close:'Close',scan:'Scan networks',scanning:'Scanning…',
networkName:'Network name',password:'Password',save:'Save and connect',
network:'Network',ip:'IP address',signal:'WiFi signal',radio:'Radio module',radioOk:'OK, channel ',radioBad:'Not responding, check the wires',uptime:'Uptime',memory:'Free memory',mac:'MAC address',
excellent:'Excellent',good:'Good',fair:'Fair',weak:'Weak',day:' d ',kb:' kB',
shortPassword:'The password must have at least 8 characters.',noNetwork:'Choose or type a network.',
confirmWifi:n=>'Connect the lamp to "'+n+'"? This page will disconnect.',
saved:h=>"Saved. The lamp is connecting. If the network changes, its address will change too: try http://"+h+" or look for it in your router. If it fails, connect to its backup WiFi after about a minute.",
refused:'The lamp refused this information.',confirmRestart:'Restart the ESP32?'}};
const $=id=>document.getElementById(id);
let busyUntil=0,wasScanning=false,scanStartUntil=0,lastState=null,lastNets=[],T=TEXT.fr;
const post=(path,params={})=>fetch(path,{method:'POST',headers:{'X-Quntis':'1','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(params)});
const hold=()=>busyUntil=Date.now()+1500;
function send(path,params){hold();return post(path,params).catch(()=>{})}
function bars(r){return r>=-55?T.excellent:r>=-67?T.good:r>=-78?T.fair:T.weak}
function uptime(s){const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return(d?d+T.day:'')+(d||h?h+' h ':'')+m+' min'}
function labels(){$('bv').textContent=$('brightness').value+' %';$('kv').textContent=$('kelvin').value+' K'}
function scanLabel(){$('scan').disabled=wasScanning;$('scan').textContent=wasScanning?T.scanning:T.scan}
function setLang(code){
 T=TEXT[code];document.documentElement.lang=code;$('lang').value=code;
 for(const e of document.querySelectorAll('[data-t]'))e.textContent=T[e.dataset.t];
 for(const e of document.querySelectorAll('[data-a]'))e.setAttribute('aria-label',T[e.dataset.a]);
 for(const e of document.querySelectorAll('[data-p]')){e.placeholder=T[e.dataset.p];e.setAttribute('aria-label',T[e.dataset.p])}
 $('wifimsg').textContent='';scanLabel();if(lastState)render(lastState);showNets();
}
function render(s){
 lastState=s;$('title').textContent=s.name;document.title=s.name;
 if(Date.now()>busyUntil){$('on').checked=!!s.on;$('backlight').checked=!!s.backlight;$('auto_dim').checked=!!s.auto_dim;
  if(s.on)$('brightness').value=s.brightness;$('kelvin').value=s.kelvin;labels()}
 $('auto_dim').disabled=!s.on&&!s.auto_dim;
 const rows=[[T.network,s.ssid],[T.ip,s.ip],[T.signal,s.rssi+' dBm ('+bars(s.rssi)+')'],[T.radio,s.radio?T.radioOk+s.channel:T.radioBad],[T.uptime,uptime(s.uptime)],[T.memory,Math.round(s.heap/1024)+T.kb],[T.mac,s.mac],['ESPHome',s.version]];
 const info=$('info');info.replaceChildren();
 for(const[k,v]of rows){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;d.textContent=v;info.append(t,d)}
 const scanning=!!s.scanning||Date.now()<scanStartUntil;
 if(wasScanning&&!scanning)nets();
 wasScanning=scanning;scanLabel();
}
async function poll(){try{render(await(await fetch('/api/state')).json())}catch(e){}}
function showNets(){
 const ul=$('nets');ul.replaceChildren();
 for(const n of lastNets){const li=document.createElement('li'),b=document.createElement('button'),a=document.createElement('span'),c=document.createElement('span');
  b.type='button';a.textContent=n.ssid;c.className='value';c.textContent=(n.lock?'🔒 ':'')+bars(n.rssi);
  b.append(a,c);b.onclick=()=>{$('ssid').value=n.ssid;$('password').value='';$('password').focus()};li.append(b);ul.append(li)}
}
async function nets(){
 try{lastNets=await(await fetch('/api/networks')).json()}catch(e){return}
 lastNets.sort((a,b)=>b.rssi-a.rssi);showNets();
}
$('on').onchange=e=>send('/api/light',{on:e.target.checked?1:0});
$('backlight').onchange=e=>send('/api/backlight',{on:e.target.checked?1:0});
$('auto_dim').onchange=e=>send('/api/auto_dim',{on:e.target.checked?1:0});
for(const id of['brightness','kelvin']){$(id).oninput=()=>{labels();hold()};$(id).onchange=e=>send('/api/light',{[id]:e.target.value})}
$('presets').onclick=e=>{const k=e.target.dataset.k;if(k){$('kelvin').value=k;labels();send('/api/light',{kelvin:k})}};
$('scan').onclick=()=>{wasScanning=true;scanStartUntil=Date.now()+1500;scanLabel();post('/api/scan').catch(()=>{})};
$('wifiopen').onclick=e=>{e.preventDefault();const p=$('wifipanel');p.hidden=false;p.scrollIntoView({behavior:'smooth'});nets();if(!wasScanning)$('scan').click()};
$('wificlose').onclick=e=>{e.preventDefault();$('wifipanel').hidden=true;$('password').value=''};
$('wifi').onsubmit=async e=>{e.preventDefault();
 const m=$('wifimsg'),ssid=$('ssid').value,pw=$('password').value;m.className='msg';
 if(!ssid||(pw&&pw.length<8)){m.className='msg bad';m.textContent=ssid?T.shortPassword:T.noNetwork;return}
 if(!confirm(T.confirmWifi(ssid)))return;
 try{const r=await post('/api/wifi',{ssid,password:pw});if(!r.ok)throw 0;$('password').value='';m.textContent=T.saved(location.hostname)}
 catch(e){m.className='msg bad';m.textContent=T.refused}};
$('restart').onclick=()=>{if(confirm(T.confirmRestart))post('/api/restart').catch(()=>{})};
$('lang').onchange=e=>{try{localStorage.setItem('lang',e.target.value)}catch(err){}setLang(e.target.value)};
let saved=null;try{saved=localStorage.getItem('lang')}catch(e){}
setLang(TEXT[saved]?saved:(navigator.language||'').toLowerCase().startsWith('fr')?'fr':'en');
labels();poll();setInterval(poll,2000);
</script></body></html>)QUNTIS";

    }  // namespace quntis_web
}  // namespace esphome
