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

<section id="s-ess">
 <div class="card"><h2>ESS am MultiPlus (Sollwert über VE.Bus)</h2><table id="ess"></table>
  <p>MultiPlus: <span id="esssw"></span></p>
  <p id="essbtn"></p>
  <p>Modus <select id="emode"><option value="null">Nulleinspeisung</option><option value="fest">fester Sollwert</option><option value="aus">aus (0 W, sendet nicht)</option></select>
   Ziel <input id="etarget" size="5" type="number"> W ·
   fest <input id="efixed" size="6" type="number"> W ·
   Mindest-SoC <input id="esoc" size="4" type="number"> % ·
   max. einspeisen <input id="edis" size="5" type="number"> W ·
   max. laden <input id="ech" size="5" type="number"> W
   <button class="btn g" onclick="essSave()">übernehmen</button></p>
  <p class="note">Sollwert positiv = einspeisen (Batterie entlädt), negativ = laden. Ohne „Senden“ rechnet ESP_venus nur mit. Senden nur, wenn Venus nicht am VE.Bus hängt, sonst schicken zwei Geräte Sollwerte. Senden bleibt über einen Neustart erhalten. Kommt 60 s kein Sollwert, geht der MultiPlus in Passthrough. Ohne Zählerwert oder bei gesperrter Entladung deckt AC-in die AC-out-Last, die Batterie ruht. Schalter „Aus“: AC-out ohne Strom.</p></div>
</section>

<section id="s-ved">
 <div class="card"><h2>CH348 / VE.Direct (nur empfangen, 19200 Baud)</h2><div class="wrap"><table id="chst"></table><table id="ved"></table></div></div>
</section>

<section id="s-packs">
 <div class="card"><h2>Kreuztest RS485 (Testplan A6c)</h2>
  <p class="note">Sendet auf allen beteiligten Kanälen. Nur ohne JK-BMS und ohne MultiPlus an den Kanälen.</p>
  <p>Zwei Kanäle verbinden: A an A, B an B, Busmasse an Busmasse. Der linke Kanal muss im Reiter VE.Bus / RS485 auf „RS485-Test“ stehen, alle mit derselben Baudrate.</p>
  <p><button class="btn r" onclick="if(confirm2(this))cross()">Kreuztest</button> <span id="crossres"></span></p>
  <div class="wrap"><table id="crosstab" class="mono"></table></div>
  <p class="dim">Jeder Kanal sendet 55 AA 3x C3 (x: 0 links, 1 Pack 1, 2 Pack 2), alle anderen hören 80 ms mit. Beim Sender selbst zeigt die Zeile, ob das RS485-Modul das eigene Senden zurückgibt.</p></div>
 <div class="card"><h2>Pack 1 (UART B)</h2>
  <p><select id="pBbaud"><option>115200</option><option>57600</option><option>38400</option><option>19200</option><option>9600</option><option>4800</option><option>2400</option></select>
   <button class="btn g" onclick="api('pack?ch=B&baud='+$('pBbaud').value).then(r=>{if(r&&r.err)alertx(r.err);t()})">Baudrate übernehmen</button></p>
  <p><input id="pBtxd" size="40" placeholder="Hex, z. B. 55 AA 01 02"> <label><input type="checkbox" id="pBtxt"> als Text</label>
   <button class="btn r" onclick="packSend('B')">senden</button></p>
  <p><button class="btn r" onclick="if(confirm2(this))api('pack_burst?ch=B&b=00').then(t)">10 s Dauersenden 0x00</button>
   <button class="btn r" onclick="if(confirm2(this))api('pack_burst?ch=B&b=55').then(t)">10 s Dauersenden 0x55</button></p>
  <div class="wrap"><table id="pBstat"></table><table id="pBlist" class="mono"></table></div></div>
 <div class="card"><h2>Pack 2 (UART A)</h2>
  <p><select id="pAbaud"><option>115200</option><option>57600</option><option>38400</option><option>19200</option><option>9600</option><option>4800</option><option>2400</option></select>
   <button class="btn g" onclick="api('pack?ch=A&baud='+$('pAbaud').value).then(r=>{if(r&&r.err)alertx(r.err);t()})">Baudrate übernehmen</button></p>
  <p><input id="pAtxd" size="40" placeholder="Hex, z. B. 55 AA 01 02"> <label><input type="checkbox" id="pAtxt"> als Text</label>
   <button class="btn r" onclick="packSend('A')">senden</button></p>
  <p><button class="btn r" onclick="if(confirm2(this))api('pack_burst?ch=A&b=00').then(t)">10 s Dauersenden 0x00</button>
   <button class="btn r" onclick="if(confirm2(this))api('pack_burst?ch=A&b=55').then(t)">10 s Dauersenden 0x55</button></p>
  <div class="wrap"><table id="pAstat"></table><table id="pAlist" class="mono"></table></div></div>

 <div class="card"><h2>SC16IS752 (CS 2, SI 42, SO 41, SCLK 38, Quarz 1,8432 MHz)</h2>
  <p><button class="btn" onclick="sc16test()">SPI prüfen</button> <span id="sc16res"></span></p>
  <p><button class="btn" onclick="packLoop()">interne Schleife</button> <span id="loopres"></span></p>
  <div class="wrap"><table id="sc16tab" class="mono"></table></div>
  <p class="dim">Schreibt 55, AA, 00, FF in das Scratchpad-Register beider Kanäle und liest jeden Wert zurück. Gegenprobe Kanalwahl: A bekommt 5A, B bekommt A5, erst danach werden beide gelesen. Line-Status nach dem Start: 60 (Sendepuffer leer). Pack 1 hängt an UART B, Pack 2 an UART A.</p></div>
</section>

<section id="s-radio">
 <div class="card"><h2>CMT2300A (CSB 8, SDIO 3, SCLK 46, FCSB 9)</h2><table id="rst"></table>
  <p><select id="rfreq"></select> <button class="btn" onclick="api('radio?ch='+$('rfreq').value).then(t)">mithören</button>
   <button class="btn" onclick="api('radio?scan=1').then(t)">Kanalscan</button>
   <button class="btn g" onclick="api('radio?clear=1').then(t)">leeren</button></p>
  <p class="dim">Mithören und Kanalscan senden nie und schalten die Abfrage aus. Wechselrichter senden nur, wenn eine DTU sie abfragt. Der Kanalscan bleibt 6 s auf jedem Kanal, ein Durchgang dauert knapp 3 min. „Spitze“ gilt seit dem Leeren.</p></div>
 <div class="card"><h2>Gehörte Geräte</h2><p class="dim">Kennung: die letzten 8 Stellen der Seriennummer. Anfragen sendet die DTU, Antworten der Wechselrichter. Im DTU-Betrieb stehen hier auch die eigenen Anfragen.</p><div class="wrap"><table id="rpeers"></table></div></div>
 <div class="card"><h2>Kanäle</h2><div class="wrap"><table id="rch"></table></div></div>
 <div class="card"><h2>Pakete</h2><div class="wrap"><table id="rpk" class="mono"></table></div></div>
</section>

<section id="s-dtu">
 <div class="card"><h2>Abfrage als DTU (sendet)</h2><table id="dst"></table>
  <p>DTU-Frequenz <select id="dfreq"></select> <button class="btn g" onclick="api('radio?dtu='+$('dfreq').value).then(t)">übernehmen</button>
   Sendeleistung <select id="pa"><option>-10</option><option>-5</option><option>0</option><option>5</option><option>10</option><option>14</option><option>17</option><option>20</option></select> dBm
   <button class="btn g" onclick="api('radio?pa='+$('pa').value).then(t)">übernehmen</button></p>
  <p class="note">Sendet. Erreicht ESP_venus einen Wechselrichter nicht, schickt ESP_venus ihm auf 868.00 MHz den Befehl, auf die DTU-Frequenz zu wechseln. Nur einschalten, wenn keine andere DTU diesen Wechselrichter abfragt. Gelesen werden nur Messwerte, kein Limit, kein Ein/Aus. Die Einstellungen bleiben über einen Neustart erhalten.</p>
  <p id="dresume"></p></div>
 <div class="card"><h2>Regelung WP-HMS (sendet Limits)</h2><table id="dpl"></table>
  <p id="dplbtn"></p>
  <p class="dim" id="dplcfg"></p>
  <p class="note">Ein Limit ändert, was der WP-HMS aus der Batterie einspeist. Ohne frischen Zählerwert vom MT681 WP sendet die Regelung nichts.</p></div>
 <div class="card"><h2>Geräte</h2><div class="wrap"><table id="rinv"></table></div></div>
 <div class="card"><h2>Gerät anlegen oder ändern</h2>
  <p><input id="fid" size="14" placeholder="Seriennummer"> <input id="fname" size="20" maxlength="23" placeholder="Name">
   Abstand <input id="fiv" size="4" type="number" min="1" max="3600" placeholder="5"> s
   <button class="btn" onclick="devSave()">speichern</button> <button class="btn r" onclick="if(confirm2(this))devDel()">löschen</button> <span id="fres"></span></p>
  <p class="dim">Seriennummer mit 12 Stellen vom Typenschild, nur damit kann ESP_venus abfragen. Mit der Kennung (8 Stellen aus dem Funk) lässt sich ein Gerät nur benennen, etwa eine fremde DTU oder ein fremder Wechselrichter. Neue Geräte sind aus.</p></div>
</section>

<section id="s-vrm">
 <div class="card"><h2>VRM (V2)</h2><table id="vrm"></table>
  <p><button class="btn" onclick="api('vrm?an=1').then(t)">senden an</button>
   <button class="btn g" onclick="api('vrm?an=0').then(t)">senden aus</button>
   <button class="btn g" onclick="api('vrm?test=1').then(t)">Verbindungstest</button>
   <a class="btn g" href="/vrm.txt" target="_blank">letzte SENDDATA-Zeile</a></p>
  <p class="dim">ESP_venus meldet an die Portal-ID aus vrm_secrets.h wie der vrmlogger: alle 300 s Werte, alle 15 min kWh. Live-Ansicht über MQTT; Schreibwünsche aus VRM werden nie ausgeführt. Werte kommen im Übergang vom Broker des Pi, nur lesend. „senden aus“ gilt bis zum Neustart.</p></div>
</section>

<section id="s-log">
 <div class="card"><h2>Konsole <a class="btn g" href="/log.txt" target="_blank">als Text</a></h2><pre id="log"></pre>
  <p class="dim">Das Log übersteht Neustart, Absturz und Watchdog, nach Stromausfall ist es leer. Zeitstempel mit Datum, vor dem Stellen der Uhr Laufzeit seit dem Start (+hh:mm:ss).</p></div>
</section>

<section id="s-sys">
 <div class="card"><h2>System</h2><table id="sys"></table></div>
 <div class="card"><h2>Firmware-Update über WLAN</h2>
  <p><input type="file" id="fw" accept=".bin"> <button class="btn" onclick="upload()">hochladen</button> <span id="upres"></span></p>
  <p class="dim">firmware.bin aus .pio/build/s3/. Danach startet ESP_venus neu. Die neue Firmware gilt erst, wenn sie 60 s mit WLAN läuft. Startet ESP_venus vorher neu, stürzt ab oder findet 5 min kein WLAN, startet der Bootloader wieder die vorherige.</p></div>
 <div class="card"><h2>Notfall</h2><p id="sysbtn"></p></div>
 <div class="card"><button class="btn r" onclick="if(confirm2(this))api('restart')">Neustart</button></div>
</section>
</main>
<script>
const tabs=[['home','Übersicht'],['i2c','I²C'],['mt','Zähler'],['can','CAN'],['left','VE.Bus / RS485'],['ess','ESS MultiPlus'],['ved','VE.Direct'],['packs','RS485 Packs'],['radio','Funk'],['dtu','DTU'],['vrm','VRM'],['log','Konsole'],['sys','System']];
const $=id=>document.getElementById(id);
let cur='home',S=null,logFrom=0;
function essSave(){api('ess?mode='+$('emode').value+'&target='+$('etarget').value+'&fixed='+$('efixed').value+'&soc_min='+$('esoc').value+'&max_dis='+$('edis').value+'&max_ch='+$('ech').value).then(t)}
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
function sec(n,fn){try{fn()}catch(e){console.error(n,e);const x=$('st');if(x&&!x.textContent.includes(n))x.textContent+=' · Anzeigefehler: '+n}}
function render(){const s=S;if(!s)return;const y=s.sys;
 let b={};try{b=bms(s.can)}catch(e){}
 sec('Status',()=>{
 $('st').textContent=(y.ip||'kein WLAN')+' · '+age(y.up*1000).replace('gerade','0 s')+' Laufzeit · '+(y.left=='rs485'?'RS485-Test':'VE.Bus hören');
 });
 sec('Übersicht',()=>{
 const r=[];for(const x of s.bme)r.push(['BME280 '+(x.addr==118?'0x76 außen':'0x77'),x.ok?f(x.t,1,'°C')+' · '+f(x.h,1,'%')+' · '+f(x.p,1,'hPa'):'<span class="bad">fehlt</span>']);
 for(const m of s.mt)r.push(['MT681 '+m.name,m.age>=0&&m.age<10000?(m.pwr!=null?f(m.pwr,0,'W')+' · ':'')+(m.imp!=null?f(m.imp/1000,3,'kWh'):''):'<span class="bad">keine Telegramme</span>']);
 r.push(['JK-BMS (CAN)',b.soc!=null?b.soc+' % · '+f(b.v,2,'V')+' · '+f(b.i,1,'A'):'<span class="bad">keine Daten</span>']);
 r.push(['VE.Bus',y.left=='rs485'?'<span class="warn">RS485-Test</span>':yn(s.vebus.sync,'Sync','kein Sync')]);
 r.push(['CMT2300A',s.cmt.ok?'<span class="ok">gefunden</span> · '+f(s.cmt.vdd,2,'V')+' · '+(s.cmt.mode=='dtu'?s.cmt.devs.filter(v=>v.poll&&v.reach).length+' von '+s.cmt.devs.filter(v=>v.poll).length+' Wechselrichtern erreichbar':s.cmt.pkts+' Pakete gehört'):'<span class="bad">nicht gefunden</span>']);
 r.push(['CH348',s.ch348.ok?'<span class="ok">verbunden</span> · '+s.ch348.ports.filter(p=>p.age>=0&&p.age<5000).length+' Ports mit Daten':'<span class="bad">nicht verbunden</span>']);
 r.push(['WLAN',y.ip?y.ssid+' · '+y.rssi+' dBm':'<span class="bad">getrennt</span>']);rows('home',r);
 });
 sec('VRM',()=>{const v=s.vrm;if(!v)return;const q=v.quelle||{};const ag=x=>x<0?'noch nie':'vor '+x+' s';
  $('vrm').innerHTML=(v.secrets?[['Zustand',(v.an?'':'<span class="bad">aus</span> · ')+esc(v.zustand||'')],
   ['Quelle',esc(q.zustand||'')+' · '+esc(q.quelle||'')+' · '+q.mit_wert+' von '+q.themen+' Themen mit Wert · letzte Nachricht '+ag(q.alter_s)],
   ['Warteschlange',v.schlange+' Zeilen, '+v.schlange_kb+' kB'+(v.verworfen?' · <span class="bad">'+v.verworfen+' verworfen</span>':'')],
   ['Gesendet',v.zeilen+' Zeilen in '+v.posts+' POSTs · '+v.fehler+' Fehler · zuletzt angenommen '+ag(v.ok_s)],
   ['Letzte Antwort',esc(v.antwort||'')],
   ['SENDDATA / kWh',ag(v.senddata_s)+' / '+ag(v.kwh_s)],
   ['Live-Brücke',esc(v.bruecke||'')+' · '+esc(v.broker||'')],
   ['VRM-Seite','letzte Anfrage '+ag(v.seite_s)+' · '+v.r+' Leseanfragen, '+v.keepalive+' Keepalive, '+v.w+' Schreibwünsche (nicht ausgeführt) · '+v.publiziert+' Nachrichten gesendet'],
   ['Verbindungstest',esc(v.post||'')]]:[['Zugangsdaten','<span class="bad">src/vrm_secrets.h fehlt</span>']])
   .map(r=>'<tr><th>'+r[0]+'</th><td>'+r[1]+'</td></tr>').join('')});
 sec('Relais',()=>{
 $('relays').innerHTML=s.relay.map((x,i)=>'<tr><th>'+x.name+' (GPIO '+x.gpio+')</th><td>'+({frei:'nicht getrieben',low:'low',high:'high'}[x.mode])+'</td><td>'+
  ['frei','low','high'].map(m=>'<button class="btn'+(x.mode==m?'':' g')+'" onclick="api(\'relay?i='+i+'&m='+m+'\').then(t)">'+m+'</button>').join('')+'</td></tr>').join('');
 });
 sec('BME280',()=>{
 head('bme',['Adresse','Chip','Temperatur','Feuchte','Druck','Taupunkt','abs. Feuchte','Lesefehler'],s.bme.map(x=>[x.addr==118?'0x76 außen':'0x77',x.id==96?'BME280':x.id==88?'BMP280':x.id?'0x'+x.id.toString(16):'–',
  x.ok?f(x.t,2,'°C'):'–',x.ok&&x.id==96?f(x.h,1,'%'):'–',x.ok?f(x.p,1,'hPa'):'–',x.ok&&x.id==96?f(dewp(x.t,x.h),1,'°C'):'–',x.ok&&x.id==96?f(absh(x.t,x.h),2,'g/m³'):'–',x.err]));
 });
 sec('Zähler',()=>{
 head('mt',['','Quelle','Leistung','Bezug 1.8.0','Einspeisung 2.8.0','letztes Telegramm','Telegramme','CRC-Fehler','Bytes','Rahmenfehler'],s.mt.map(m=>[m.name,m.src,m.pwr!=null?f(m.pwr,0,'W'):'–',m.imp!=null?f(m.imp/1000,4,'kWh'):'–',
  m.exp!=null?f(m.exp/1000,4,'kWh'):'–',m.age>=0?age(m.age)+' ('+m.len+' B)':'–',m.tel+(m.swap?' (CRC vertauscht: '+m.swap+')':''),m.crc,m.bytes,m.ferr==null?'–':m.ferr+(m.rmt===false?' <span class="bad">RMT aus</span>':'')]));
 });
 sec('CAN',()=>{
 rows('bms',[['Name',b.name||'–'],['SoC / SoH',b.soc!=null?b.soc+' % / '+b.soh+' %':'–'],['Spannung / Strom',f(b.v,2,'V')+' / '+f(b.i,1,'A')],['Temperatur',f(b.t,1,'°C')],
  ['Ladespannung / Entladespannung',f(b.cvl,1,'V')+' / '+f(b.dvl,1,'V')],['Ladestrom / Entladestrom (Grenzen)',f(b.ccl,1,'A')+' / '+f(b.dcl,1,'A')],
  ['Zelle min / max',b.cmin!=null?b.cmin+' / '+b.cmax+' mV (Δ '+(b.cmax-b.cmin)+')':'–'],['Temperatur min / max',f(b.tmin,0,'°C')+' / '+f(b.tmax,0,'°C')],
  ['Module online',b.mod??'–'],['Kapazität',b.cap!=null?b.cap+' Ah':'–'],['Warnungen/Alarme 35A',b.alarm?hex(b.alarm):'–'],['Seriennummer',b.sn||'–']]);
 const c=s.can;rows('canstat',[['Treiber',c.ok?c.state:'<span class="bad">nicht gestartet</span>'],['Telegramme',c.frames],['Fehlerzähler RX / Busfehler',c.rx_err+' / '+c.bus_err],['verpasst / Überlauf',c.rx_missed+' / '+c.rx_overrun]]);
 head('can',['ID','Anzahl','Alter','Daten','ASCII'],c.ids.slice().sort((a,b)=>a.id-b.id).map(e=>[(e.ext?e.id.toString(16).padStart(8,'0'):e.id.toString(16).padStart(3,'0')).toUpperCase(),e.n,age(c.now-e.ms),hex(e.d),esc(ascii(e.d))]));
 });
 sec('VE.Bus/RS485',()=>{
 $('lmode').textContent=y.left=='rs485'?'RS485-Test, '+s.rs.baud+' Baud':'VE.Bus, nur hören';
 $('vebuscard').style.display=y.left=='rs485'?'none':'';$('rscard').style.display=y.left=='rs485'?'':'none';
 if(s.vebus){const v=s.vebus;rows('vebus',[['Sync-Frames',yn(v.sync,'empfangen','keine (1 s)')],['Prüfsummenfehler',v.chk],['DC-Strom (Rundsendung 0x80)',f(v.dci,1,'A')],
  ['Temperatur',f(v.temp,1,'°C')],['Lade-Status 0x80',v.st80],['LED an / blinkt',v.led+' / '+v.blink],['Eingangsstrom Grenze akt / min / max',f(v.ilim,1,'A')+' / '+f(v.imin,1,'A')+' / '+f(v.imax,1,'A')]])}
 if(s.rs){const q=s.rs;rows('rsstat',[['Pinprüfung beim Start',esc(q.pins)],['Senden',q.txok?'<span class="ok">frei</span>':'<span class="bad">gesperrt</span> (erwartet: TX offen, RX high getrieben)'],['Empfangen / gesendet',q.rx+' / '+q.tx+' Bytes'],['Rahmen',q.frames]]);
  head('rs',['Zeit','','Länge','Hex','Text'],q.list.map(x=>[age(y.up*1000-x.ms),x.tx?'<span class="warn">TX</span>':'RX',x.len,hex(x.d)+(x.len>96?' …':''),esc(ascii(x.d))]))}
 });
 sec('RS485 Packs',()=>{
 if(s.sc16&&s.sc16.ok)for(const c of s.sc16.ch){const id='p'+c.name;if(document.activeElement!=$(id+'baud'))$(id+'baud').value=c.baud;
  rows(id+'stat',[['Baudrate',c.baud],['Empfangen / gesendet',c.rx+' / '+c.tx+' Bytes'],['Rahmen',c.frames],['Fehler Überlauf / Parität / Rahmen / Break',c.ovr+' / '+c.par+' / '+c.frm+' / '+c.brk]]);
  head(id+'list',['Zeit','','Länge','Hex','Text'],c.list.map(x=>[age(y.up*1000-x.ms),x.tx?'<span class="warn">TX</span>':'RX',x.len,hex(x.d)+(x.len>96?' …':''),esc(ascii(x.d))]))}
 });
 sec('ESS',()=>{
 const E=s.ess;if(E){const ag=ms=>ms>=0?' ('+age(ms)+')':'';
  rows('ess',[['Zustand',E.send?(E.state=='regelt'?'<span class="ok">regelt, sendet</span>':'<span class="warn">sendet · '+esc(E.state)+'</span>'):esc(E.state)],
   ['Hauszähler (Bezug +)',E.grid!=null?f(E.grid,0,'W')+ag(E.grid_age):'<span class="bad">kein Wert</span>'],
   ['Sollwert (+ einspeisen, − laden)','<b>'+f(E.sp,0,'W')+'</b>'+(E.send?'':' (nicht gesendet)')],
   ['Grenzen einspeisen / laden',f(E.lim_dis,0,'W')+' / '+f(E.lim_ch,0,'W')],
   ['BMS: SoC · Spannung · Strom',E.soc!=null?f(E.soc,0,'%')+' · '+f(E.v,2,'V')+' · '+f(E.i,1,'A')+ag(E.bms_age):'<span class="bad">keine Daten</span>'],
   ['BMS: Ladeschluss / Entladeschluss',f(E.cvl,1,'V')+' / '+f(E.dvl,1,'V')],['BMS: Lade- / Entladestrom max.',f(E.ccl,1,'A')+' / '+f(E.dcl,1,'A')],
   ['gesendet / quittiert',E.sent+' / '+E.acked+(E.sent_age>=0?ag(E.sent_age):'')],
   ['MultiPlus: Batterie',E.mp_age>=0?f(E.mp_v,2,'V')+ag(E.mp_age):'–'],
   ['MultiPlus: Wechselrichter / AC-in / AC-out',E.mp_age>=0?f(E.mp_inv,0,'W')+' / '+f(E.mp_mains,0,'W')+' / '+f(E.mp_out,0,'W'):'–'],
   ['MultiPlus Schalter',esc(E.sw)+(E.sw_cmd!='–'?' <span class="dim">(zuletzt gesendet: '+esc(E.sw_cmd)+')</span>':'')]]);
  $('esssw').innerHTML=[['ein','Ein'],['laden','Nur Laden'],['wr','Nur Wechselrichter'],['aus','Aus']].map(x=>'<button class="btn'+(E.sw==({ein:'ein',laden:'nur Laden',wr:'nur Wechselrichter',aus:'aus'})[x[0]]?'':' g')+'" onclick="if(confirm2(this))api(\'ess?sw='+x[0]+'\').then(t)">'+x[1]+'</button>').join(' ');
  $('essbtn').innerHTML='<button class="btn'+(E.send?' r':'')+'" onclick="'+(E.send?'':'if(confirm2(this))')+'api(\'ess?send='+(E.send?0:1)+'\').then(t)">Senden '+(E.send?'aus':'ein')+'</button>';
  const c=E.cfg,setv=(id,v)=>{if(document.activeElement!=$(id))$(id).value=v};
  setv('emode',E.mode);setv('etarget',c.target);setv('efixed',c.fixed);setv('esoc',c.soc_min);setv('edis',c.max_dis);setv('ech',c.max_ch)}
 });
 sec('Regelung WP',()=>{
 const D=s.dpl;if(D){const ag=ms=>ms>=0?' ('+age(ms)+')':'';
  rows('dpl',[['Zustand',(D.on?(D.state=='regelt'?'<span class="ok">regelt</span>':'<span class="warn">'+esc(D.state)+'</span>'):'aus')],
   ['WP-Zähler (Bezug +)',D.meter!=null?f(D.meter,0,'W')+ag(D.meter_age):'<span class="bad">kein Wert</span>'],
   ['HMS Leistung AC',f(D.pac,1,'W')+ag(D.inv_age)],['Batterie Spannung am HMS / korrigiert',f(D.udc,2,'V')+' / '+f(D.ucorr,2,'V')],
   ['SoC (CAN)',D.soc!=null?f(D.soc,0,'%')+ag(D.soc_age):'–'],['Entladen erlaubt',yn(D.discharge)],['Nacht',D.night?'ja':'nein'],['HMS ausgeschaltet',D.standby?'ja':'nein'],
   ['letztes Limit',D.limit!=null?f(D.limit,0,'W')+ag(D.limit_age)+(D.cmd==1?' · läuft':D.cmd==2?' · <span class="bad">Fehler</span>':''):'–'],
   ['Befehle Limit / Ein-Aus / Neustart',D.limits+' / '+D.power_cmds+' / '+D.restarts]]);
  $('dplbtn').innerHTML=D.avail?'<button class="btn'+(D.on?' r':'')+'" onclick="'+(D.on?'':'if(confirm2(this))')+'api(\'dpl?on='+(D.on?0:1)+'\').then(t)">Regelung '+(D.on?'aus':'ein')+'</button>':'<span class="bad">Kein geregelter Wechselrichter (DPL_SERIAL) in der Geräteliste</span>';
  const c=D.cfg;$('dplcfg').textContent='Werte aus der Config der OpenDTU WP: Ziel '+c.target+' W ±'+c.hyst+' W am WP-Zähler, Limit '+c.lower+' bis '+c.upper+' W, Standby '+(c.standby?'erlaubt':'nein')+
   ', Start/Stopp SoC '+c.soc_start+'/'+c.soc_stop+' %, ohne SoC Spannung '+c.v_start+'/'+c.v_stop+' V (korrigiert um '+c.corr+' V/W, Stoppspannung gilt immer), nachts immer Batterie: '+(c.night?'ja':'nein')+', täglicher Neustart des HMS: '+(c.restart>=0?c.restart+' Uhr':'aus')+'.'}
 });
 sec('Funk',()=>{
 const R=s.cmt,mhz=c=>(860+c*0.25).toFixed(2)+' MHz',bar=v=>'<span style="display:inline-block;height:8px;width:'+Math.max(1,Math.min(110,v+130))+'px;background:var(--acc)"></span>';
 if(!R.ok){rows('rst',[['Chip','<span class="bad">nicht gefunden</span>']]);head('rpeers',[],[]);head('rch',[],[]);head('rpk',[],[]);head('rinv',[],[]);$('dresume').innerHTML='';rows('dst',[['Chip','<span class="bad">nicht gefunden</span>']])}else{
 rows('rst',[['Chip','<span class="ok">gefunden</span>'],['Versorgung am Chip (LBD)',f(R.vdd,2,'V')],['Betrieb',R.mode=='scan'?'Kanalscan, gerade '+mhz(R.ch)+', '+R.passes+' Durchgänge fertig':R.mode=='dtu'?'<span class="warn">DTU, sendet</span> auf '+mhz(R.dtu_ch)+', '+R.pa+' dBm, Seriennummer '+R.dtu:'hört auf '+mhz(R.ch)],
  ['Uhrzeit (für die Abfrage nötig)',yn(R.time,'gestellt','fehlt')],['Signal jetzt',R.rssi+' dBm'],
  ['Pakete gültig / CRC falsch',R.pkts+' / '+R.crc],['Empfang neu gestartet',R.restarts]]);
 rows('dst',[['Betrieb',R.mode=='dtu'?'<span class="warn">sendet</span> auf '+mhz(R.dtu_ch)+', '+R.pa+' dBm':'sendet nicht, '+(R.mode=='scan'?'Kanalscan':'hört auf '+mhz(R.ch))+(R.suspended?' (Abfrage unterbrochen)':'')],['DTU-Seriennummer',R.dtu],['Uhrzeit (für die Abfrage nötig)',yn(R.time,'gestellt','fehlt')]]);
 $('dresume').innerHTML=R.suspended&&R.devs.some(v=>v.poll)?'<button class="btn r" onclick="api(\'radio?resume=1\').then(t)">Abfrage fortsetzen</button> Mithören oder Kanalscan hat die Abfrage unterbrochen.':'';
 if(document.activeElement!=$('dfreq'))$('dfreq').value=R.dtu_ch;if(document.activeElement!=$('pa'))$('pa').value=R.pa;
 head('rinv',['Name','Kennung','Seriennr.','Typ','Abfrage','Abstand','','erreichbar','Leistung AC','Spannung AC','Netz','Temperatur','Ertrag heute / gesamt','DC je Eingang','Signal','Anfragen / ok / Teil / keine / defekt','Daten'],R.devs.map(v=>[esc(v.name)||'–',v.id,v.serial||'–',v.serial?v.type||'<span class="bad">unbekannt</span>':'nur Name',
  v.serial&&v.type?'<button class="btn'+(v.poll?' r':' g')+'" onclick="'+(v.poll?'':'if(confirm2(this))')+'devPoll(\''+v.id+'\','+(v.poll?0:1)+')">'+(v.poll?'aus':'ein')+'</button>':'–',v.serial?v.iv+' s':'–',
  '<button class="btn g" onclick="devEdit(\''+v.id+'\')">bearbeiten</button>',
  v.reach?'<span class="ok">ja</span>':'nein',f(v.pac,1,'W'),f(v.uac,1,'V'),f(v.f,2,'Hz'),f(v.t,1,'°C'),v.yd!=null?f(v.yd,0,'Wh')+' / '+f(v.yt,3,'kWh'):'–',
  v.dc?v.dc.map(d=>f(d[0],1,'W')+' ('+f(d[1],1,'V')+')').join(' · '):'–',v.ok||v.part?v.rssi+' dBm':'–',v.tx+' / '+v.ok+' / '+v.part+' / '+v.none+' / '+v.bad,v.age>=0?age(v.age):'–']));
 const nm=id=>{const d=R.devs.find(v=>v.id==id);return d&&d.name?esc(d.name)+' <span class="dim">'+id+'</span>':id+' <button class="btn g" onclick="devNew(\''+id+'\')">benennen</button>'};
 head('rpeers',['Wechselrichter','DTU','Frequenz','Anfragen','Signal DTU','Antworten','Signal WR','zuletzt'],R.peers.map(e=>[nm(e.inv)+(e.rsp?'':' <span class="warn">keine Antwort</span>'),e.dtu==R.dtu.slice(4)?'ESP_venus':nm(e.dtu),mhz(e.ch),e.req,e.rreq?e.rreq+' dBm':'–',e.rsp,e.rrsp?e.rrsp+' dBm':'–',age(R.now-e.ms)]));
 head('rch',['Frequenz','Mittel','Spitze','','Pakete'],R.chs.map(c=>[mhz(c.ch)+(c.ch==R.ch?' ◀':''),c.avg+' dBm',c.max+' dBm',bar(c.max),c.pkts||'']));
 const FL=[[0x20,'PKT_ERR'],[0x10,'PREAM'],[0x08,'SYNC'],[0x04,'NODE'],[0x02,'CRC_OK'],[0x01,'PKT_OK']],FF=[[0x40,'voll'],[0x20,'nicht leer'],[0x08,'Überlauf']];
 const flg=p=>p.fl?FL.filter(x=>p.fl&x[0]).map(x=>x[1]).join(' ')+' · FIFO '+(FF.filter(x=>p.ff&x[0]).map(x=>x[1]).join(' ')||'leer'):'';
 head('rpk',['Zeit','','Frequenz','Signal','CRC','Länge','Chip-Flags','Hex'],R.list.map(p=>[age(R.now-p.ms),p.tx?'<span class="warn">TX</span>':'RX',mhz(p.ch),p.tx?'–':p.rssi+' dBm',p.crc?'ok':p.fl&&!(p.fl&2)?'<span class="bad">CRC-Fehler im Chip</span>':'<span class="bad">falsch</span>',p.d.length/2,flg(p),hex(p.d)]))}
 });
 sec('VE.Direct',()=>{
 const h=s.ch348;rows('chst',[['CH348',h.ok?'<span class="ok">verbunden</span>, Version '+h.ver:'<span class="bad">nicht verbunden</span>'],['ungültige Blöcke',h.bad]]);
 head('ved',['Port','Gerät','Seriennr.','Batterie','Strom','PV / Leistung','Zustand / SoC','Blöcke ok / Fehler','Alter','Bytes'],h.ports.map((p,i)=>{const x=p.f||{};
  return[i,p.dev||(x.PID?'PID '+x.PID:'–'),x['SER#']||'–',x.V!=null?f(x.V/1000,2,'V'):'–',x.I!=null?f(x.I/1000,2,'A'):'–',
   x.PPV!=null?f(x.PPV,0,'W')+' ('+f(x.VPV/1000,1,'V')+')':x.P!=null?f(x.P,0,'W'):'–',x.CS!=null?(CS[x.CS]||x.CS)+(x.ERR&&x.ERR!='0'?' <span class="bad">ERR '+x.ERR+'</span>':''):x.SOC!=null?f(x.SOC/10,1,'%'):'–',
   p.ok+' / '+p.bad,p.age>=0?age(p.age):'–',p.bytes]}));
 });
 sec('System',()=>{
 rows('sys',[['Firmware',y.build+' · '+y.part+', '+esc(y.fwstate)+' · ELF '+y.elf],['vorherige Firmware',y.other?esc(y.other):'–'],['Absturz im Flash',y.coredump?'<span class="bad">ja</span>':'nein'],['Laufzeit',age(y.up*1000)],['Neustartgrund',y.reset],['WLAN',y.ip?y.ssid+', '+y.ip+', '+y.rssi+' dBm':'getrennt'],['MAC',y.mac],
  ['Heap intern frei / min',Math.round(y.heap/1024)+' / '+Math.round(y.heapmin/1024)+' KB'],['PSRAM frei',Math.round(y.psram/1024)+' KB']]);
 });
}
async function t(){const s=await api('status');if(s){S=s;render()}else $('st').textContent='keine Verbindung'}
async function logt(){const r=await api('log?from='+logFrom);if(r){const p=$('log'),bottom=p.scrollTop+p.clientHeight>=p.scrollHeight-20;
 for(const l of r.lines)p.textContent+=l+'\n';logFrom=r.next;if(p.textContent.length>200000)p.textContent=p.textContent.slice(-150000);if(bottom)p.scrollTop=p.scrollHeight}}
async function scan(){$('scanres').textContent='scanne …';const r=await api('i2c');$('scanres').textContent=r?(r.found.length?'gefunden: '+r.found.map(a=>'0x'+a.toString(16).padStart(2,'0')).join(', '):'kein Gerät'):'Fehler';t()}
async function setMode(btn){if(!confirm2(btn))return;await api('mode?m='+$('lsel').value+'&baud='+$('lbaud').value);$('st').textContent='startet neu …'}
async function send(){const v=$('txd').value;const r=await api('send?'+($('txtext').checked?'text=':'hex=')+encodeURIComponent(v));if(r&&r.err)alertx(r.err);t()}
function alertx(m){$('st').textContent=m}
async function packLoop(){$('loopres').textContent='läuft …';const r=await api('pack_loop');$('loopres').innerHTML=r?r.map(c=>c.pack+' (UART '+c.name+', '+c.baud+' Baud): '+(c.ok?'<span class="ok">in Ordnung</span>':'<span class="bad">gesendet '+hex(c.sent)+', zurück '+(c.got?hex(c.got):'nichts')+'</span>')).join(' · '):'Fehler'}
async function packSend(ch){const v=$('p'+ch+'txd').value;const r=await api('pack?ch='+ch+'&'+($('p'+ch+'txt').checked?'text=':'hex=')+encodeURIComponent(v));if(r&&r.err)alertx(r.err);t()}
async function cross(){$('crossres').textContent='läuft …';const r=await api('rs485_cross');if(!r){$('crossres').textContent='Fehler';return}
 const bs=r.baud.filter(b=>b>0),same=bs.every(b=>b==bs[0]);
 $('crossres').innerHTML=(r.left?'':'<span class="warn">linker Kanal nicht im RS485-Test, nimmt nicht teil</span> ')+(same?'':'<span class="bad">Baudraten verschieden: '+r.baud.join(' / ')+'</span>');
 const rows=[];for(const x of r.res)for(const y of x.to)rows.push([esc(x.from),esc(y.name)+(y.self?' (Echo)':''),hex(x.sent),y.got?hex(y.got):'–',y.self?(y.got?'Echo':'kein Echo'):(y.ok?'<span class="ok">richtig</span>':y.got?'<span class="bad">falsch</span>':'nichts')]);
 head('crosstab',['sendet','empfängt','gesendet','empfangen','Ergebnis'],rows)}
async function sc16test(){$('sc16res').textContent='prüfe …';const r=await api('sc16_test');if(!r){$('sc16res').textContent='Fehler';return}
 const h2=v=>v.toString(16).toUpperCase().padStart(2,'0');
 $('sc16res').innerHTML=(r.good==r.total&&r.ch.every(c=>c.lsr==0x60)?'<span class="ok">in Ordnung</span>':'<span class="bad">fehlerhaft</span>')+': '+r.good+' von '+r.total+' Werten richtig zurück';
 head('sc16tab',['Kanal','Scratchpad geschrieben → gelesen','Kanalwahl','Line-Status','Line-Control'],r.ch.map((c,i)=>{const w=i?0xA5:0x5A,g=r.cross[i];return[c.name+(c.name=='A'?' (Pack 2)':' (Pack 1)'),c.spr.map(p=>h2(p[0])+'→'+(p[0]==p[1]?h2(p[1]):'<span class="bad">'+h2(p[1])+'</span>')).join('  '),
  h2(w)+'→'+(g==w?h2(g):'<span class="bad">'+h2(g)+'</span>'),c.lsr==0x60?h2(c.lsr):'<span class="bad">'+h2(c.lsr)+'</span>',h2(c.lcr)]}))}
async function devPoll(id,on){const r=await api('dev?id='+id+'&poll='+on);if(r&&r.err)alertx(r.err);setTimeout(t,300)}
function devEdit(id){const d=S.cmt.devs.find(v=>v.id==id);if(!d)return;$('fid').value=d.serial||d.id;$('fname').value=d.name;$('fiv').value=d.serial?d.iv:'';$('fres').textContent='';$('fname').focus()}
function devNew(id){show('dtu');$('fid').value=id;$('fname').value='';$('fiv').value='';$('fres').textContent='';$('fname').focus()}
async function devSave(){const id=$('fid').value.trim(),iv=$('fiv').value.trim();const r=await api('dev?id='+encodeURIComponent(id)+'&name='+encodeURIComponent($('fname').value)+(iv?'&iv='+iv:''));
 $('fres').textContent=r&&r.err?r.err:r?'gespeichert':'Fehler';setTimeout(t,300)}
async function devDel(){const r=await api('dev?del=1&id='+encodeURIComponent($('fid').value.trim()));$('fres').textContent=r&&r.err?r.err:r?'gelöscht':'Fehler';setTimeout(t,300)}
async function cantest(){$('cantestres').textContent='läuft …';const r=await api('can_selftest');
 $('cantestres').innerHTML=!r?'Fehler':r.err?esc(r.err):r.start=='ESP_ERR_INVALID_STATE'?'<span class="bad">nicht gesendet</span>, Pins als Eingang: TX '+r.pin_tx+', RX '+r.pin_rx+' (erwartet: TX offen, RX high getrieben)':(r.ok==10?'<span class="ok">in Ordnung</span>':'<span class="bad">fehlerhaft</span>')+
 ': '+r.sent+' gesendet, '+r.ok+' richtig zurück, '+r.wrong+' falsch, '+r.lost+' verloren · TX-Fehlerzähler '+r.tx_err+', Busfehler '+r.bus_err+', Zustand '+r.state+' · RX vorher '+r.rx_high+' % high · Pins als Eingang: TX '+r.pin_tx+', RX '+r.pin_rx}
async function upload(){const fl=$('fw').files[0];if(!fl)return;$('upres').textContent='lade hoch …';const fd=new FormData();fd.append('fw',fl);
 try{const r=await fetch('/update',{method:'POST',body:fd});$('upres').textContent=await r.text()}catch(e){$('upres').textContent='Fehler: '+e}}
for(const id of['rfreq','dfreq'])for(let c=12;c<=40;c++){const o=document.createElement('option');o.value=c;o.textContent=(860+c*0.25).toFixed(2)+' MHz';if(c==20)o.selected=true;$(id).appendChild(o)}
function sysbtn(){const y=S&&S.sys;if(!y)return;$('sysbtn').innerHTML=(y.other?'<button class="btn r" onclick="if(confirm2(this))api(\'boot_other\').then(r=>alertx(r&&r.ok?\'startet vorherige Firmware …\':\'Fehler: \'+(r?r.msg:\'\')))">vorherige Firmware starten</button> ':'')+
 (y.coredump?'<a class="btn g" href="/coredump.elf">Absturz herunterladen</a> <button class="btn g" onclick="if(confirm2(this))api(\'coredump_erase\').then(t)">Absturz löschen</button>':'<span class="dim">kein Absturz gespeichert</span>')}
t();logt();setInterval(t,2000);setInterval(sysbtn,2000);setInterval(logt,1500);
</script></body></html>
)HTML";
