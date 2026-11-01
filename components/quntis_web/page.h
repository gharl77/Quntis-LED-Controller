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
h1{font-size:22px;margin:4px 0 16px}
h2{font-size:15px;margin:0 0 12px;color:var(--dim);font-weight:600}
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
<h1 id="title">Lampe Quntis</h1>

<section><h2>Lumière</h2>
<div class="row"><span>Lampe</span><label class="switch"><input type="checkbox" id="on" aria-label="Lampe"><i></i></label></div>
<label>Luminosité <span class="value" id="bv"></span><input type="range" id="brightness" min="10" max="100"></label>
<label>Température <span class="value" id="kv"></span><input type="range" id="kelvin" min="3000" max="6500" step="50"></label>
<div class="temp"></div>
<div class="presets" id="presets"><button data-k="3000">3000 K</button><button data-k="4000">4000 K</button><button data-k="5000">5000 K</button><button data-k="6500">6500 K</button></div>
<div class="row" style="margin-top:10px"><span>Rétroéclairage</span><label class="switch"><input type="checkbox" id="backlight" aria-label="Rétroéclairage"><i></i></label></div>
<div class="row"><span>Variation automatique</span><label class="switch"><input type="checkbox" id="auto_dim" aria-label="Variation automatique"><i></i></label></div>
<p class="note">La lampe ne renvoie pas son état : cette page montre le dernier ordre envoyé par l'ESP32, pas ce que fait la télécommande.</p>
</section>

<section><h2>Informations</h2><dl id="info"></dl>
<p><a href="#" id="wifiopen">Modifier le WiFi</a></p>
<button id="restart">Redémarrer l'ESP32</button></section>

<section id="wifipanel" hidden>
<div class="row"><h2 style="margin:0">Modifier le WiFi</h2><a href="#" id="wificlose">Fermer</a></div>
<ul id="nets"></ul>
<button id="scan">Balayer les réseaux</button>
<form id="wifi" style="margin-top:14px">
<input type="text" id="ssid" placeholder="Nom du réseau" maxlength="32" autocomplete="off" autocapitalize="none" spellcheck="false" aria-label="Nom du réseau">
<input type="password" id="password" placeholder="Mot de passe" maxlength="64" autocomplete="new-password" aria-label="Mot de passe">
<button class="primary" id="save">Enregistrer et connecter</button>
</form>
<p class="msg" id="wifimsg" role="status"></p>
</section>
</main><script>
const $=id=>document.getElementById(id);
let busyUntil=0,wasScanning=false,scanStartUntil=0;
const post=(path,params={})=>fetch(path,{method:'POST',headers:{'X-Quntis':'1','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(params)});
const hold=()=>busyUntil=Date.now()+1500;
function send(path,params){hold();return post(path,params).catch(()=>{})}
function bars(r){return r>=-55?'Excellent':r>=-67?'Bon':r>=-78?'Moyen':'Faible'}
function uptime(s){const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return(d?d+' j ':'')+(d||h?h+' h ':'')+m+' min'}
function labels(){$('bv').textContent=$('brightness').value+' %';$('kv').textContent=$('kelvin').value+' K'}
function render(s){
 $('title').textContent=s.name;document.title=s.name;
 if(Date.now()>busyUntil){$('on').checked=!!s.on;$('backlight').checked=!!s.backlight;$('auto_dim').checked=!!s.auto_dim;
  if(s.on)$('brightness').value=s.brightness;$('kelvin').value=s.kelvin;labels()}
 $('auto_dim').disabled=!s.on&&!s.auto_dim;
 const rows=[['Réseau',s.ssid],['Adresse IP',s.ip],['Signal WiFi',s.rssi+' dBm ('+bars(s.rssi)+')'],['Module radio',s.radio?'OK, canal '+s.channel:'Ne répond pas, vérifie les fils'],['En marche depuis',uptime(s.uptime)],['Mémoire libre',Math.round(s.heap/1024)+' ko'],['Adresse MAC',s.mac],['ESPHome',s.version]];
 const info=$('info');info.replaceChildren();
 for(const[k,v]of rows){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;d.textContent=v;info.append(t,d)}
 const scanning=!!s.scanning||Date.now()<scanStartUntil;
 if(wasScanning&&!scanning)nets();
 wasScanning=scanning;
 const b=$('scan');b.disabled=wasScanning;b.textContent=wasScanning?'Balayage en cours…':'Balayer les réseaux';
}
async function poll(){try{render(await(await fetch('/api/state')).json())}catch(e){}}
async function nets(){
 let list;try{list=await(await fetch('/api/networks')).json()}catch(e){return}
 list.sort((a,b)=>b.rssi-a.rssi);
 const ul=$('nets');ul.replaceChildren();
 for(const n of list){const li=document.createElement('li'),b=document.createElement('button'),a=document.createElement('span'),c=document.createElement('span');
  b.type='button';a.textContent=n.ssid;c.className='value';c.textContent=(n.lock?'🔒 ':'')+bars(n.rssi);
  b.append(a,c);b.onclick=()=>{$('ssid').value=n.ssid;$('password').value='';$('password').focus()};li.append(b);ul.append(li)}
}
$('on').onchange=e=>send('/api/light',{on:e.target.checked?1:0});
$('backlight').onchange=e=>send('/api/backlight',{on:e.target.checked?1:0});
$('auto_dim').onchange=e=>send('/api/auto_dim',{on:e.target.checked?1:0});
for(const id of['brightness','kelvin']){$(id).oninput=()=>{labels();hold()};$(id).onchange=e=>send('/api/light',{[id]:e.target.value})}
$('presets').onclick=e=>{const k=e.target.dataset.k;if(k){$('kelvin').value=k;labels();send('/api/light',{kelvin:k})}};
$('scan').onclick=()=>{$('scan').disabled=true;wasScanning=true;scanStartUntil=Date.now()+1500;post('/api/scan').catch(()=>{})};
$('wifiopen').onclick=e=>{e.preventDefault();const p=$('wifipanel');p.hidden=false;p.scrollIntoView({behavior:'smooth'});nets();if(!wasScanning)$('scan').click()};
$('wificlose').onclick=e=>{e.preventDefault();$('wifipanel').hidden=true;$('password').value=''};
$('wifi').onsubmit=async e=>{e.preventDefault();
 const m=$('wifimsg'),ssid=$('ssid').value,pw=$('password').value;m.className='msg';
 if(!ssid||(pw&&pw.length<8)){m.className='msg bad';m.textContent=ssid?'Le mot de passe doit avoir au moins 8 caractères.':'Choisis ou écris un réseau.';return}
 if(!confirm('Connecter la lampe à « '+ssid+' » ? Cette page va se déconnecter.'))return;
 try{const r=await post('/api/wifi',{ssid,password:pw});if(!r.ok)throw 0;$('password').value='';
  m.textContent="Enregistré. La lampe se connecte. Si le réseau change, son adresse changera aussi : essaie http://"+location.hostname+" ou cherche-la dans ton routeur. En cas d'échec, connecte-toi à son WiFi de secours après environ une minute."}
 catch(e){m.className='msg bad';m.textContent='La lampe a refusé ces informations.'}};
$('restart').onclick=()=>{if(confirm("Redémarrer l'ESP32 ?"))post('/api/restart').catch(()=>{})};
labels();poll();setInterval(poll,2000);
</script></body></html>)QUNTIS";

    }  // namespace quntis_web
}  // namespace esphome
