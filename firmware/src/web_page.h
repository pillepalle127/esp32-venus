// Testseite: eine Datei mit HTML, CSS und JavaScript. Werte kommen alle 2 s aus /api/status.
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>esp32-venus</title>
<style>
:root{--bg:#16181b;--card:#202327;--line:#30343a;--fg:#e6e6e6;--dim:#9aa0a6;--acc:#2f81f7;--ok:#3fb950;--bad:#f85149;--warn:#d29922}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px system-ui,sans-serif}
header{padding:12px 16px;border-bottom:1px solid var(--line);display:flex;flex-wrap:wrap;gap:8px 16px;align-items:baseline}
h1{font-size:18px;margin:0}#st{color:var(--dim);font-size:12px}
nav{display:flex;flex-wrap:wrap;gap:4px;padding:8px 16px;border-bottom:1px solid var(--line)}
nav button{background:none;border:1px solid var(--line);color:var(--fg);padding:6px 10px;border-radius:6px;cursor:pointer}
nav button.on{background:var(--acc);border-color:var(--acc)}
main{padding:12px 16px;max-width:1100px}section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin-bottom:12px}
h2{font-size:15px;margin:0 0 8px}.wrap{overflow-x:auto}
table{border-collapse:collapse;width:100%}td,th{padding:4px 8px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}
th{color:var(--dim);font-weight:normal}td.v{text-align:right;font-variant-numeric:tabular-nums}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}.dim{color:var(--dim)}
.btn{background:var(--acc);color:#fff;border:0;border-radius:6px;padding:8px 12px;cursor:pointer;margin:2px}
.btn.g{background:#3a3f45}.btn.r{background:#a4302b}
input,select{background:#111;color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:6px}
pre{background:#0d0f11;border:1px solid var(--line);border-radius:6px;padding:8px;height:420px;overflow:auto;margin:0;font-size:12px;white-space:pre-wrap}
.mono{font-family:ui-monospace,monospace;font-size:12px}.note{color:var(--warn);margin:6px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:12px}
</style></head><body>
<header><h1>esp32-venus</h1><span id="st">verbinde …</span></header>
<nav id="nav"></nav>
<main>
<section id="s-home"><div class="grid">
 <div class="card"><h2>Werte</h2><table id="home"></table></div>
 <div class="card"><h2>Relais</h2><p class="dim">Beim Start sind die Pins nicht getrieben (Eingang).</p><table id="relays"></table></div>
</div></section>

<section id="s-i2c">
 <div class="card"><h2>I²C (SDA GPIO 16, SCL GPIO 15)</h2>
  <button class="btn" onclick="scan()">I²C scannen</button> <span id="scanres" class="mono"></span></div>
 <div class="card"><h2>BME280</h2><div class="wrap"><table id="bme"></table></div></div>
</section>

<section id="s-mt">
 <div class="card"><h2>MT681</h2><div class="wrap"><table id="mt"></table></div>
 <p class="dim">Telegramme zählen nur mit gültiger CRC. „Rahmenfehler“ gibt es nur beim RMT-Empfang (WP).</p></div>
</section>

<section id="s-can">
 <div class="card"><h2>JK-BMS über CAN (nur zuhören, 500 kbit/s)</h2><div class="wrap"><table id="bms"></table></div></div>
 <div class="card"><h2>CAN-Selbsttest</h2>
  <p class="note">Sendet 10 Telegramme auf den CAN-Bus. Nur ohne BMS am Bus, mit dem 120-Ω-Widerstand des Moduls.</p>
  <p><button class="btn r" onclick="if(confirm2(this))cantest()">Selbsttest starten</button> <span id="cantestres" class="mono"></span></p></div>
 <div class="card"><h2>CAN-Telegramme <button class="btn g" onclick="api('can_clear')">leeren</button></h2>
  <div class="wrap"><table id="canstat"></table><table id="can" class="mono"></table></div></div>
</section>

<section id="s-left">
 <div class="card"><h2>Linker Kanal (GPIO 4 TX, GPIO 5 RX)</h2>
  <p>Betriebsart: <b id="lmode"></b></p>
  <p><select id="lsel"><option value="vebus">VE.Bus, nur hören</option><option value="rs485">RS485-Test</option></select>
   <select id="lbaud"><option>2400</option><option>4800</option><option>9600</option><option>19200</option><option>38400</option><option>57600</option><option selected>115200</option><option>256000</option></select>
   <button class="btn" onclick="setMode(this)">übernehmen und neu starten</button></p>
  <p class="dim">Die Baudrate gilt nur für den RS485-Test. VE.Bus läuft fest mit 256000 Baud und sendet nie.</p></div>
 <div class="card" id="vebuscard"><h2>VE.Bus</h2><div class="wrap"><table id="vebus"></table></div></div>
 <div class="card" id="rscard"><h2>RS485-Test</h2>
  <p class="note">Senden nur, wenn kein MultiPlus am Kanal hängt.</p>
  <p><input id="txd" size="40" placeholder="Hex, z. B. 01 03 00 00 00 01 84 0A"> <label><input type="checkbox" id="txtext"> als Text</label>
   <button class="btn r" onclick="send()">senden</button>
   <button class="btn r" onclick="if(confirm2(this))api('burst?b=00').then(r=>{if(r&&r.err)alertx(r.err)})">10 s Dauersenden 0x00</button>
   <button class="btn r" onclick="if(confirm2(this))api('burst?b=55').then(r=>{if(r&&r.err)alertx(r.err)})">10 s Dauersenden 0x55</button></p>
  <p class="dim">0x00 fürs Multimeter (A gegen B wird beim Senden deutlich negativ), 0x55 fürs Oszilloskop (Rechteck, Bitzeit 8,7 µs bei 115200 Baud).</p>
  <div class="wrap"><table id="rsstat"></table><table id="rs" class="mono"></table></div></div>
</section>

<section id="s-ved">
 <div class="card"><h2>CH348 / VE.Direct (nur empfangen, 19200 Baud)</h2><div class="wrap"><table id="chst"></table><table id="ved"></table></div></div>
</section>

<section id="s-log">
 <div class="card"><h2>Konsole</h2><pre id="log"></pre></div>
</section>

<section id="s-sys">
 <div class="card"><h2>System</h2><table id="sys"></table></div>
 <div class="card"><h2>Firmware-Update über WLAN</h2>
  <p><input type="file" id="fw" accept=".bin"> <button class="btn" onclick="upload()">hochladen</button> <span id="upres"></span></p>
  <p class="dim">firmware.bin aus .pio/build/s3/. Danach startet der S3 neu.</p></div>
 <div class="card"><button class="btn r" onclick="if(confirm2(this))api('restart')">Neustart</button></div>
</section>
</main>
<script>
const tabs=[['home','Übersicht'],['i2c','I²C'],['mt','Zähler'],['can','CAN'],['left','VE.Bus / RS485'],['ved','VE.Direct'],['log','Konsole'],['sys','System']];
const $=id=>document.getElementById(id);
let cur='home',S=null,logFrom=0;
function show(t){cur=t;for(const[k]of tabs){$('s-'+k).classList.toggle('on',k==t);$('b-'+k).classList.toggle('on',k==t)}try{localStorage.tab=t}catch(e){}}
for(const[k,n]of tabs){const b=document.createElement('button');b.id='b-'+k;b.textContent=n;b.onclick=()=>show(k);$('nav').appendChild(b)}
try{if(localStorage.tab&&$('s-'+localStorage.tab))cur=localStorage.tab}catch(e){}show(cur);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const f=(v,d,u)=>v==null||isNaN(v)?'–':Number(v).toFixed(d)+(u?' '+u:'');
const age=ms=>ms==null||ms<0?'–':ms<1500?'gerade':ms<120000?Math.round(ms/1000)+' s':Math.round(ms/60000)+' min';
const rows=(id,r)=>{$(id).innerHTML=r.map(x=>'<tr>'+x.map((c,i)=>i==0?'<th>'+c+'</th>':'<td class="v">'+c+'</td>').join('')+'</tr>').join('')};
const head=(id,h,r)=>{$(id).innerHTML='<tr>'+h.map(c=>'<th>'+c+'</th>').join('')+'</tr>'+r.map(x=>'<tr>'+x.map(c=>'<td>'+c+'</td>').join('')+'</tr>').join('')};
const yn=(b,t,n)=>b?'<span class="ok">'+(t||'ja')+'</span>':'<span class="bad">'+(n||'nein')+'</span>';
async function api(p){try{const r=await fetch('/api/'+p);return await r.json()}catch(e){return null}}
function confirm2(b){if(b.dataset.c){return true}b.dataset.c=1;const t=b.textContent;b.textContent='wirklich?';setTimeout(()=>{b.textContent=t;delete b.dataset.c},3000);return false}
function hex(s){return s.replace(/(..)/g,'$1 ').trim()}
const le=(h,o,n,signed)=>{let v=0;for(let i=n-1;i>=0;i--)v=v*256+parseInt(h.substr((o+i)*2,2),16);if(signed&&v>=2**(8*n-1))v-=2**(8*n);return v};
const ascii=h=>hex(h).split(' ').map(x=>{const c=parseInt(x,16);return c>=32&&c<127?String.fromCharCode(c):''}).join('');
const dewp=(t,h)=>{if(!(h>0))return NaN;const a=17.62,b=243.12,g=Math.log(h/100)+a*t/(b+t);return b*g/(a-g)};
const absh=(t,h)=>216.7*(h/100*6.112*Math.exp(17.62*t/(243.12+t)))/(273.15+t);
function bms(c){const m={};if(!c)return m;for(const e of c.ids)m[e.id]=e;const o={};
 if(m[0x351]){const h=m[0x351].d;o.cvl=le(h,0,2)/10;o.ccl=le(h,2,2,1)/10;o.dcl=le(h,4,2,1)/10;o.dvl=le(h,6,2)/10}
 if(m[0x355]){const h=m[0x355].d;o.soc=le(h,0,2);o.soh=le(h,2,2)}
 if(m[0x356]){const h=m[0x356].d;o.v=le(h,0,2,1)/100;o.i=le(h,2,2,1)/10;o.t=le(h,4,2,1)/10}
 if(m[0x373]){const h=m[0x373].d;o.cmin=le(h,0,2);o.cmax=le(h,2,2);o.tmin=le(h,4,2)-273.15;o.tmax=le(h,6,2)-273.15}
 if(m[0x35E])o.name=ascii(m[0x35E].d);if(m[0x379])o.cap=le(m[0x379].d,0,2);if(m[0x35A])o.alarm=m[0x35A].d;
 if(m[0x372])o.mod=le(m[0x372].d,0,2);if(m[0x380])o.sn=ascii(m[0x380].d)+(m[0x381]?ascii(m[0x381].d):'');return o}
const CS={0:'aus',2:'Fehler',3:'Bulk',4:'Absorption',5:'Float',7:'Ausgleich',245:'Start',247:'Ausgleich auto',252:'extern'};
function render(){const s=S;if(!s)return;const y=s.sys;
 $('st').textContent=(y.ip||'kein WLAN')+' · '+age(y.up*1000).replace('gerade','0 s')+' Laufzeit · '+(y.left=='rs485'?'RS485-Test':'VE.Bus hören');
 const b=bms(s.can);
 const r=[];for(const x of s.bme)r.push(['BME280 '+(x.addr==118?'0x76 außen':'0x77'),x.ok?f(x.t,1,'°C')+' · '+f(x.h,1,'%')+' · '+f(x.p,1,'hPa'):'<span class="bad">fehlt</span>']);
 for(const m of s.mt)r.push(['MT681 '+m.name,m.age>=0&&m.age<10000?(m.pwr!=null?f(m.pwr,0,'W')+' · ':'')+(m.imp!=null?f(m.imp/1000,3,'kWh'):''):'<span class="bad">keine Telegramme</span>']);
 r.push(['JK-BMS (CAN)',b.soc!=null?b.soc+' % · '+f(b.v,2,'V')+' · '+f(b.i,1,'A'):'<span class="bad">keine Daten</span>']);
 r.push(['VE.Bus',y.left=='rs485'?'<span class="warn">RS485-Test</span>':yn(s.vebus.sync,'Sync','kein Sync')]);
 r.push(['CMT2300A',yn(s.cmt,'gefunden','nicht gefunden')]);
 r.push(['CH348',s.ch348.ok?'<span class="ok">verbunden</span> · '+s.ch348.ports.filter(p=>p.age>=0&&p.age<5000).length+' Ports mit Daten':'<span class="bad">nicht verbunden</span>']);
 r.push(['WLAN',y.ip?y.ssid+' · '+y.rssi+' dBm':'<span class="bad">getrennt</span>']);rows('home',r);
 $('relays').innerHTML=s.relay.map((x,i)=>'<tr><th>'+x.name+' (GPIO '+x.gpio+')</th><td>'+({frei:'nicht getrieben',low:'low',high:'high'}[x.mode])+'</td><td>'+
  ['frei','low','high'].map(m=>'<button class="btn'+(x.mode==m?'':' g')+'" onclick="api(\'relay?i='+i+'&m='+m+'\').then(t)">'+m+'</button>').join('')+'</td></tr>').join('');
 head('bme',['Adresse','Chip','Temperatur','Feuchte','Druck','Taupunkt','abs. Feuchte','Lesefehler'],s.bme.map(x=>[x.addr==118?'0x76 außen':'0x77',x.id==96?'BME280':x.id==88?'BMP280':x.id?'0x'+x.id.toString(16):'–',
  x.ok?f(x.t,2,'°C'):'–',x.ok&&x.id==96?f(x.h,1,'%'):'–',x.ok?f(x.p,1,'hPa'):'–',x.ok&&x.id==96?f(dewp(x.t,x.h),1,'°C'):'–',x.ok&&x.id==96?f(absh(x.t,x.h),2,'g/m³'):'–',x.err]));
 head('mt',['','Quelle','Leistung','Bezug 1.8.0','Einspeisung 2.8.0','letztes Telegramm','Telegramme','CRC-Fehler','Bytes','Rahmenfehler'],s.mt.map(m=>[m.name,m.src,m.pwr!=null?f(m.pwr,0,'W'):'–',m.imp!=null?f(m.imp/1000,4,'kWh'):'–',
  m.exp!=null?f(m.exp/1000,4,'kWh'):'–',m.age>=0?age(m.age)+' ('+m.len+' B)':'–',m.tel+(m.swap?' (CRC vertauscht: '+m.swap+')':''),m.crc,m.bytes,m.ferr==null?'–':m.ferr+(m.rmt===false?' <span class="bad">RMT aus</span>':'')]));
 rows('bms',[['Name',b.name||'–'],['SoC / SoH',b.soc!=null?b.soc+' % / '+b.soh+' %':'–'],['Spannung / Strom',f(b.v,2,'V')+' / '+f(b.i,1,'A')],['Temperatur',f(b.t,1,'°C')],
  ['Ladespannung / Entladespannung',f(b.cvl,1,'V')+' / '+f(b.dvl,1,'V')],['Ladestrom / Entladestrom (Grenzen)',f(b.ccl,1,'A')+' / '+f(b.dcl,1,'A')],
  ['Zelle min / max',b.cmin!=null?b.cmin+' / '+b.cmax+' mV (Δ '+(b.cmax-b.cmin)+')':'–'],['Temperatur min / max',f(b.tmin,0,'°C')+' / '+f(b.tmax,0,'°C')],
  ['Module online',b.mod??'–'],['Kapazität',b.cap!=null?b.cap+' Ah':'–'],['Warnungen/Alarme 35A',b.alarm?hex(b.alarm):'–'],['Seriennummer',b.sn||'–']]);
 const c=s.can;rows('canstat',[['Treiber',c.ok?c.state:'<span class="bad">nicht gestartet</span>'],['Telegramme',c.frames],['Fehlerzähler RX / Busfehler',c.rx_err+' / '+c.bus_err],['verpasst / Überlauf',c.rx_missed+' / '+c.rx_overrun]]);
 head('can',['ID','Anzahl','Alter','Daten','ASCII'],c.ids.slice().sort((a,b)=>a.id-b.id).map(e=>[(e.ext?e.id.toString(16).padStart(8,'0'):e.id.toString(16).padStart(3,'0')).toUpperCase(),e.n,age(c.now-e.ms),hex(e.d),esc(ascii(e.d))]));
 $('lmode').textContent=y.left=='rs485'?'RS485-Test, '+s.rs.baud+' Baud':'VE.Bus, nur hören';
 $('vebuscard').style.display=y.left=='rs485'?'none':'';$('rscard').style.display=y.left=='rs485'?'':'none';
 if(s.vebus){const v=s.vebus;rows('vebus',[['Sync-Frames',yn(v.sync,'empfangen','keine (1 s)')],['Prüfsummenfehler',v.chk],['DC-Strom (Rundsendung 0x80)',f(v.dci,1,'A')],
  ['Temperatur',f(v.temp,1,'°C')],['Lade-Status 0x80',v.st80],['LED an / blinkt',v.led+' / '+v.blink],['Eingangsstrom Grenze akt / min / max',f(v.ilim,1,'A')+' / '+f(v.imin,1,'A')+' / '+f(v.imax,1,'A')]])}
 if(s.rs){const q=s.rs;rows('rsstat',[['Pinprüfung beim Start',esc(q.pins)],['Senden',q.txok?'<span class="ok">frei</span>':'<span class="bad">gesperrt</span> (erwartet: TX offen, RX high getrieben)'],['Empfangen / gesendet',q.rx+' / '+q.tx+' Bytes'],['Rahmen',q.frames]]);
  head('rs',['Zeit','','Länge','Hex','Text'],q.list.map(x=>[age(y.up*1000-x.ms),x.tx?'<span class="warn">TX</span>':'RX',x.len,hex(x.d)+(x.len>96?' …':''),esc(ascii(x.d))]))}
 const h=s.ch348;rows('chst',[['CH348',h.ok?'<span class="ok">verbunden</span>, Version '+h.ver:'<span class="bad">nicht verbunden</span>'],['ungültige Blöcke',h.bad]]);
 head('ved',['Port','Gerät','Seriennr.','Batterie','Strom','PV / Leistung','Zustand / SoC','Blöcke ok / Fehler','Alter','Bytes'],h.ports.map((p,i)=>{const x=p.f||{};
  return[i,p.dev||(x.PID?'PID '+x.PID:'–'),x['SER#']||'–',x.V!=null?f(x.V/1000,2,'V'):'–',x.I!=null?f(x.I/1000,2,'A'):'–',
   x.PPV!=null?f(x.PPV,0,'W')+' ('+f(x.VPV/1000,1,'V')+')':x.P!=null?f(x.P,0,'W'):'–',x.CS!=null?(CS[x.CS]||x.CS)+(x.ERR&&x.ERR!='0'?' <span class="bad">ERR '+x.ERR+'</span>':''):x.SOC!=null?f(x.SOC/10,1,'%'):'–',
   p.ok+' / '+p.bad,p.age>=0?age(p.age):'–',p.bytes]}));
 rows('sys',[['Firmware',y.build],['Laufzeit',age(y.up*1000)],['Neustartgrund',y.reset],['WLAN',y.ip?y.ssid+', '+y.ip+', '+y.rssi+' dBm':'getrennt'],['MAC',y.mac],
  ['Heap intern frei / min',Math.round(y.heap/1024)+' / '+Math.round(y.heapmin/1024)+' KB'],['PSRAM frei',Math.round(y.psram/1024)+' KB']]);
}
async function t(){const s=await api('status');if(s){S=s;render()}else $('st').textContent='keine Verbindung'}
async function logt(){const r=await api('log?from='+logFrom);if(r){const p=$('log'),bottom=p.scrollTop+p.clientHeight>=p.scrollHeight-20;
 for(const l of r.lines)p.textContent+=l+'\n';logFrom=r.next;if(p.textContent.length>200000)p.textContent=p.textContent.slice(-150000);if(bottom)p.scrollTop=p.scrollHeight}}
async function scan(){$('scanres').textContent='scanne …';const r=await api('i2c');$('scanres').textContent=r?(r.found.length?'gefunden: '+r.found.map(a=>'0x'+a.toString(16).padStart(2,'0')).join(', '):'kein Gerät'):'Fehler';t()}
async function setMode(btn){if(!confirm2(btn))return;await api('mode?m='+$('lsel').value+'&baud='+$('lbaud').value);$('st').textContent='startet neu …'}
async function send(){const v=$('txd').value;const r=await api('send?'+($('txtext').checked?'text=':'hex=')+encodeURIComponent(v));if(r&&r.err)alertx(r.err);t()}
function alertx(m){$('st').textContent=m}
async function cantest(){$('cantestres').textContent='läuft …';const r=await api('can_selftest');
 $('cantestres').innerHTML=!r?'Fehler':r.err?esc(r.err):r.start=='ESP_ERR_INVALID_STATE'?'<span class="bad">nicht gesendet</span>, Pins als Eingang: TX '+r.pin_tx+', RX '+r.pin_rx+' (erwartet: TX offen, RX high getrieben)':(r.ok==10?'<span class="ok">in Ordnung</span>':'<span class="bad">fehlerhaft</span>')+
 ': '+r.sent+' gesendet, '+r.ok+' richtig zurück, '+r.wrong+' falsch, '+r.lost+' verloren · TX-Fehlerzähler '+r.tx_err+', Busfehler '+r.bus_err+', Zustand '+r.state+' · RX vorher '+r.rx_high+' % high · Pins als Eingang: TX '+r.pin_tx+', RX '+r.pin_rx}
async function upload(){const fl=$('fw').files[0];if(!fl)return;$('upres').textContent='lade hoch …';const fd=new FormData();fd.append('fw',fl);
 try{const r=await fetch('/update',{method:'POST',body:fd});$('upres').textContent=await r.text()}catch(e){$('upres').textContent='Fehler: '+e}}
t();logt();setInterval(t,2000);setInterval(logt,1500);
</script></body></html>
)HTML";
