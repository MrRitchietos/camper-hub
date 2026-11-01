// The phone page (one file, no internet needed). Talks to /api/* on the board.
#include <Arduino.h>
namespace app {
extern const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Camper hub</title>
<style>
:root{--bg:#0a0f1a;--card:#172133;--card2:#223049;--line:#2c3b58;--tx:#eef3fa;--dim:#7f91ae;--teal:#2bd9a8;--amber:#ffc54a;--red:#f0524f;--blue:#5aa8ff;--green:#42dd7e}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.4 system-ui,sans-serif}
header{position:sticky;top:0;background:#0f1624;padding:10px 14px 0;z-index:2}
h1{font-size:17px;margin:0 0 8px;display:flex;justify-content:space-between}h1 small{color:var(--dim);font-weight:400}
nav{display:flex;gap:4px;overflow-x:auto;padding-bottom:8px}nav button{flex:none;background:var(--card);color:var(--dim);border:0;border-radius:16px;padding:7px 13px;font:inherit}
nav button.on{background:var(--teal);color:#06231b;font-weight:600}
main{padding:12px 14px 40px;max-width:640px;margin:auto}.card{background:var(--card);border-radius:12px;padding:12px 14px;margin-bottom:10px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.big{font-size:26px;font-weight:600}.dim{color:var(--dim);font-size:13px}
.row{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:8px 0;border-top:1px solid var(--line)}.row:first-child{border-top:0}
select,input{background:var(--card2);color:var(--tx);border:1px solid var(--line);border-radius:8px;padding:7px 8px;font:inherit;max-width:60%}
button.b{background:var(--card2);color:var(--tx);border:0;border-radius:8px;padding:8px 12px;font:inherit}button.t{background:var(--teal);color:#06231b;font-weight:600}
button.r{background:var(--red)}.sw{min-width:64px}.sw.on{background:var(--green);color:#062312;font-weight:600}
.warn{color:var(--amber)}.bad{color:var(--red)}.ok{color:var(--green)}a{color:var(--blue)}h2{font-size:14px;color:var(--dim);margin:14px 0 6px;font-weight:600;text-transform:uppercase;letter-spacing:.04em}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:6px 4px;border-top:1px solid var(--line)}
.chk{display:flex;gap:10px;align-items:center;padding:8px 0;border-top:1px solid var(--line)}.chk span{flex:1}.chk.done span{color:var(--dim);text-decoration:line-through}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:var(--card2);padding:9px 16px;border-radius:18px;display:none}
.bar{height:8px;background:var(--card2);border-radius:4px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;background:var(--teal)}
</style></head><body>
<header><h1>Camper hub <small id="clk"></small></h1><nav id="nav"></nav></header><main id="main"></main><div id="toast"></div>
<script>
const TABS=['Live','Bank','Weather','Level','Lists','Trips','Settings','Files'];let tab=localStorage.tab||'Live',S=null,L=null,busy=false;
const $=s=>document.querySelector(s),E=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function toast(t){const e=$('#toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',2200)}
async function post(u,p){const r=await fetch(u,{method:'POST',body:new URLSearchParams(p)});if(!r.ok)toast('Failed: '+await r.text());setTimeout(load,400);return r.ok}
const WX=c=>c==0?'Clear':c<=2?'Partly cloudy':c==3?'Cloudy':c<50?'Fog':c<60?'Drizzle':c<70?'Rain':c<80?'Snow':c<83?'Showers':c<90?'Snow showers':'Thunderstorm';
const n=(v,d=1,u='')=>v==null?'--':(+v).toFixed(d)+u;
function nav(){$('#nav').innerHTML=TABS.map(t=>`<button class="${t==tab?'on':''}" onclick="go('${t}')">${t}</button>`).join('')}
function go(t){tab=t;localStorage.tab=t;nav();render(true)}
async function load(){if(busy)return;busy=true;try{const r=await fetch('/api/state');S=await r.json();$('#clk').textContent=(S.demo?'DEMO  ':'')+(S.time||'');render(false)}catch(e){$('#clk').textContent='offline'}busy=false}
function sel(k,v,opts){return `<select onchange="post('/api/set',{k:'${k}',v:this.value})">${opts.map(([a,b])=>`<option value="${a}"${a==v?' selected':''}>${b}</option>`).join('')}</select>`}
function roleSel(s){return `<select onchange="post('/api/role',{a:'${s.a}',r:this.value})">${['(not named)','Inside','Outside','Fridge','Freezer','Other'].map((b,a)=>`<option value="${a}"${a==s.r?' selected':''}>${b}</option>`).join('')}</select>`}
const R=['Sensor','Inside','Outside','Fridge','Freezer','Other'];
function live(){const v=S.vic,b=S.bank,w=S.wx,t=S.today||{};let h='<div class="grid">';
h+=`<div class="card"><div class="dim">Camper battery</div><div class="big">${v.ok?(v.soc>=0?v.soc+'%':'--'):'--'}</div><div class="dim">${v.ok?n(v.v,2,' V')+'  '+n(v.a,1,' A'):(v.key?'waiting for the charger':'Victron key missing')}</div></div>`;
h+=`<div class="card"><div class="dim">Solar</div><div class="big">${v.ok&&v.pv!=null?v.pv+' W':'--'}</div><div class="dim">${v.ok?E(v.state)+', today '+n(v.kwh,2,' kWh'):''}</div></div>`;
h+=`<div class="card"><div class="dim">Power bank</div><div class="big">${b.soc!=null?b.soc+'%':'--'}</div><div class="dim">${b.soc!=null?'in '+b.in+' W, out '+b.out+' W':E(b.status)}</div></div>`;
h+=`<div class="card"><div class="dim">Outside</div><div class="big">${w.ok?n(w.t,1,'°'):'--'}</div><div class="dim">${w.ok?E(w.city)+', '+WX(w.code):'no forecast yet'}</div></div></div>`;
if(w.ok&&w.warn)h+=`<div class="card warn">⚠ ${E(w.warn)}</div>`;
h+='<div class="card"><h2 style="margin-top:0">Sensors</h2>';
if(!S.sens.length)h+='<div class="dim">No Bluetooth thermometers heard yet.</div>';
for(const s of S.sens){const tr=s.tr!=null?(s.tr<-1.5?' ↓':s.tr>1.5?' ↑':' ='):'';h+=`<div class="row"><div><b>${R[s.r]}</b> <span class="dim">${E(s.k)}${s.b>=0?', batt '+s.b+'%':''}${s.age>300?', not heard for '+Math.round(s.age/60)+' min':''}</span><div class="dim">${s.p!=null?n(s.p,0,' hPa')+tr+(s.tr!=null&&s.tr<=-4?' storm risk':''):(s.lo!=null?'today '+n(s.lo)+'° .. '+n(s.hi)+'°':'')}</div></div><div style="text-align:right"><span class="big" style="font-size:20px">${n(s.t,1,'°')}</span><div class="dim">${s.h!=null?s.h+'%':''}</div></div></div>`}
h+='</div>';if(t.place!=null)h+=`<div class="card"><div class="dim">Today so far</div>${E(t.place||'place unknown')}: outside ${n(t.omin,0)}..${n(t.omax,0)}°, inside ${n(t.imin,0)}..${n(t.imax,0)}°${t.kwh!=null?', solar '+n(t.kwh,2,' kWh'):''}${t.vmin!=null?', battery low '+n(t.vmin,2,' V'):''}</div>`;
h+=`<div class="dim">WiFi: ${S.wifi.on?E(S.wifi.ssid)+(S.wifi.portal?' (login page)':S.wifi.net?'':' (no internet)'):'offline'} · board memory ${Math.round(S.heap/1024)} KB free</div>`;return h}
const BS=[['Light',27,[[0,'off'],[1,'on'],[2,'SOS'],[3,'strobe']]],['Key sound',56,[[0,'off'],[1,'on']]],['Silent AC charging',57,[[0,'off'],[1,'on']]],['Solar / car charge',20,[[2,'2 A'],[4,'4 A'],[6,'6 A'],[8,'8 A']]],['Screen off after',62,[[30,'30 s'],[60,'1 min'],[180,'3 min'],[300,'5 min'],[0,'never']]],['AC off when idle',60,[[0,'never'],[60,'1 h'],[240,'4 h'],[480,'8 h'],[960,'16 h'],[1440,'24 h']]],['DC off when idle',61,[[0,'never'],[60,'1 h'],[240,'4 h'],[480,'8 h'],[1440,'24 h']]],['USB off when idle',59,[[0,'never'],[3,'3 min'],[5,'5 min'],[10,'10 min'],[30,'30 min']]],['Power off when idle',68,[[5,'5 min'],[10,'10 min'],[30,'30 min'],[60,'1 h'],[480,'8 h']]],['Stop discharge at',66,[[0,'0 %'],[50,'5 %'],[100,'10 %'],[150,'15 %'],[200,'20 %'],[250,'25 %'],[300,'30 %']]],['AC charge up to',67,[[600,'60 %'],[700,'70 %'],[800,'80 %'],[900,'90 %'],[1000,'100 %']]]];
function bank(){const b=S.bank;let h=`<div class="card"><div class="dim">${E(b.name||'Fossibot')} · ${E(b.status)}</div>`;
if(b.soc==null)return h+'<div style="margin-top:8px">Not connected. Close the BrightEMS app on your phone, the power bank takes one connection at a time.</div></div>';
h+=`<div class="big">${b.soc}%</div><div class="bar"><i style="width:${b.soc}%"></i></div><div class="dim" style="margin-top:6px">in ${b.in} W · out ${b.out} W${b.left?' · '+Math.floor(b.left/60)+'h'+String(b.left%60).padStart(2,'0')+' left':''}${b.full?' · full in '+Math.floor(b.full/60)+'h'+String(b.full%60).padStart(2,'0'):''}</div></div>`;
h+='<div class="card"><div class="grid">'+[['USB',0,b.usb],['12V',1,b.dc],['AC 230V',2,b.ac],['Light',3,b.light>0]].map(([t,o,on])=>`<button class="b sw ${on?'on':''}" onclick="post('/api/bank',{o:${o},on:${on?0:1}})">${t} ${on?'on':'off'}</button>`).join('')+'</div>';
h+=`<div class="dim" style="margin-top:8px">USB-A ${n(b.usba1,0)}/${n(b.usba2,0)} W · USB-C ${n(b.usbc1,0)}/${n(b.usbc2,0)} W · car ${n(b.car,0)} W · AC ${b.acw} W</div></div>`;
if(b.set){h+='<div class="card"><h2 style="margin-top:0">Power bank settings</h2>'+BS.map(([t,r,o])=>`<div class="row"><span>${t}</span><select onchange="post('/api/bankset',{r:${r},v:this.value})">${o.map(([a,c])=>`<option value="${a}"${a==b.set[r]?' selected':''}>${c}</option>`).join('')}</select></div>`).join('')+'</div>'}return h}
function weather(){const w=S.wx;if(!w.ok)return '<div class="card">No forecast yet, the board needs internet once.</div>'+place();
let h=`<div class="card"><div class="big">${n(w.t,1,'°')} <span style="font-size:15px">${WX(w.code)}</span></div><div class="dim">${E(w.city)} · wind ${w.wind} km/h, gusts ${w.gust} · humidity ${w.hum}%</div>${w.warn?'<div class="warn">⚠ '+E(w.warn)+'</div>':''}</div><div class="card"><table>`;
for(const d of w.days)h+=`<tr><td><b>${E(d.n)}</b></td><td>${WX(d.c)}</td><td>${d.lo}..<b>${d.hi}°</b></td><td class="${d.r>=50?'warn':'dim'}">${d.r}% ${n(d.mm,1)} mm</td><td class="${d.g>=50?'bad':'dim'}">${d.g} km/h</td></tr>`;
return h+'</table></div>'+place()}
function place(){const p=S.place;let h=`<div class="card"><h2 style="margin-top:0">Weather place: ${E(p.name)}</h2><div class="row"><input id="pq" placeholder="Search a town" style="max-width:none;flex:1"><button class="b t" onclick="post('/api/place',{op:'search',q:$('#pq').value})">Search</button></div>`;
if(p.busy)h+='<div class="dim">Searching...</div>';else if(p.err)h+=`<div class="warn">${E(p.err)}</div>`;
p.hits.forEach((x,i)=>h+=`<div class="row"><span>${E(x.n)} <span class="dim">${E(x.a)}</span></span><button class="b" onclick="post('/api/place',{op:'hit',i:${i}})">Use</button></div>`);
if(p.recent.length)h+='<div class="dim" style="margin-top:8px">Used before:</div>';p.recent.forEach((x,i)=>h+=`<div class="row"><span>${E(x.n)} <span class="dim">${E(x.a)}</span></span><button class="b" onclick="post('/api/place',{op:'recent',i:${i}})">Use</button></div>`);
return h+`<div class="row"><span class="dim">Auto = from the internet connection (often wrong on campsite WiFi)</span><button class="b" onclick="post('/api/place',{op:'auto'})">Auto</button></div></div>`}
function level(){const l=S.lvl,s=S.set;if(!l.ok)return '<div class="card">No RuuviTag heard. Lay the tag flat in the camper, arrow to the front.</div>';
const f=Math.tan(l.p*Math.PI/180)*s.wb,r=Math.tan(l.r*Math.PI/180)*s.trk;let a=[];
if(Math.abs(f)>=1)a.push(f>0?`raise the <b>rear</b> by ${Math.abs(f).toFixed(0)} cm`:`raise the <b>front</b> by ${Math.abs(f).toFixed(0)} cm`);
if(Math.abs(r)>=1)a.push(r>0?`raise the <b>left</b> side by ${Math.abs(r).toFixed(0)} cm`:`raise the <b>right</b> side by ${Math.abs(r).toFixed(0)} cm`);
return `<div class="card"><div class="big ${a.length?'warn':'ok'}">${a.length?'Not level':'Level'}</div><div>${a.join('<br>')||'Nothing to do.'}</div><div class="dim" style="margin-top:8px">front/back ${n(l.p,1,'°')} · side ${n(l.r,1,'°')} · wheelbase ${s.wb} cm, track ${s.trk} cm</div></div><div class="card"><div class="row"><span>Parked on flat ground?</span><button class="b" onclick="post('/api/set',{k:'zero',v:1})">Zero here</button></div></div>`}
async function lists(force){if(force||!L){L=await (await fetch('/api/lists')).json()}let h='';
for(const[k,t,i]of[['leave','Leaving',0],['arrive','Arriving',1]]){const it=L[k].items,d=it.filter(x=>x.d).length;
h+=`<div class="card"><h2 style="margin-top:0;display:flex;justify-content:space-between">${t} <span>${d}/${it.length} <button class="b" onclick="ck('reset',${i},0)">Reset</button></span></h2>`;
it.forEach((x,j)=>h+=`<div class="chk ${x.d?'done':''}"><input type="checkbox" ${x.d?'checked':''} onchange="ck('tick',${i},${j})"><span>${E(x.t)}</span><button class="b" onclick="ck('del',${i},${j})">✕</button></div>`);
h+=`<div class="row"><input id="add${i}" placeholder="New item" style="max-width:none;flex:1" maxlength="40"><button class="b t" onclick="ck('add',${i},0,$('#add${i}').value)">Add</button></div></div>`}return h}
async function ck(op,l,i,t=''){await post('/api/check',{op,l,i,t});setTimeout(async()=>{L=null;$('#main').innerHTML=await lists(true)},500)}
async function trips(){const r=await fetch('/f?n=trips.csv');let h='<div class="card"><h2 style="margin-top:0">Trip diary</h2>';
if(!r.ok)return h+'<div class="dim">Starts by itself: one line per day with place, weather, temperatures and solar.</div></div>';
const rows=(await r.text()).trim().split('\n').slice(1).reverse();h+='<table>';
for(const l of rows){const c=l.split(',');h+=`<tr><td><b>${E(c[0].slice(5))}</b></td><td>${E(c[1])}<div class="dim">${WX(+c[4])}${+c[5]>=1?', '+c[5]+' mm':''}</div></td><td>${c[8]?(+c[8]).toFixed(0)+'..'+(+c[9]).toFixed(0):c[7]+'..'+c[6]}°</td><td class="dim">${c[13]?c[13]+' kWh':''}<br>${c[12]?c[12]+' V':''}</td></tr>`}
return h+'</table><div class="dim" style="margin-top:8px"><a href="/f?n=trips.csv&dl=1">Download trips.csv</a></div></div>'}
function settings(){const s=S.set;const al=[[0,'off'],...[1150,1180,1200,1220,1250,1280].map(v=>[v,'below '+(v/100).toFixed(2)+' V'])];
const rows=[['Brightness',sel('bright',s.bright,[[0,'Auto'],[1,'Low'],[2,'Medium'],[3,'High']])],['Alarm sound',sel('beep',+s.beep,[[1,'on'],[0,'off']])],
['Screensaver',sel('saver',s.saver,[[0,'off'],[1,'after 1 min'],[2,'after 2 min'],[3,'after 5 min'],[4,'after 10 min'],[5,'after 30 min']])],['Screensaver look',sel('look',s.look,[[0,'camper'],[1,'my photos']])],
['Screen off',sel('scroff',s.scroff,[[0,'never'],[1,'15 min later'],[2,'30 min later'],[3,'60 min later'],[4,'120 min later']])],
['Battery type',sel('batt',s.batt,[[0,'AGM / lead'],[1,'Gel'],[2,'LiFePO4']])],['Low battery alarm',sel('alarm',s.alarm,al)],
['Wheelbase (cm)',`<input type="number" value="${s.wb}" min="250" max="500" onchange="post('/api/set',{k:'wb',v:this.value})">`],['Track (cm)',`<input type="number" value="${s.trk}" min="120" max="250" onchange="post('/api/set',{k:'trk',v:this.value})">`],
['Level tag turned',sel('rot',s.rot,[[0,'0°'],[1,'90°'],[2,'180°'],[3,'270°']])],['Own WiFi CamperHub',sel('ap',+s.ap,[[0,'off'],[1,'on']])],['Demo mode',sel('demo',+s.demo,[[0,'off'],[1,'on']])]];
let h='<div class="card">'+rows.map(([a,b])=>`<div class="row"><span>${a}</span>${b}</div>`).join('')+'</div>';
h+=`<div class="card"><h2 style="margin-top:0">Victron key ${S.vic.key?'<span class="ok">set</span>':'<span class="warn">missing</span>'}</h2><div class="dim">VictronConnect > charger > ⚙ > ⋮ > Product info > Instant readout > Show</div><div class="row"><input id="vk" placeholder="32 characters" style="max-width:none;flex:1" autocomplete="off"><button class="b t" onclick="post('/api/vkey',{k:$('#vk').value.replace(/\\s/g,'')});$('#vk').value=''">Save</button></div></div>`;
h+='<div class="card"><h2 style="margin-top:0">Sensors</h2>'+(S.sens.map(s=>`<div class="row"><span>${E(s.k)} ${n(s.t,1,'°')} <span class="dim">${E(s.a.slice(-5))}</span></span>${roleSel(s)}</div>`).join('')||'<div class="dim">none heard</div>')+'</div>';
return h}
function size(b){return b>1048576?(b/1048576).toFixed(1)+' MB':b>1024?(b/1024).toFixed(0)+' KB':b+' B'}
async function files(){const f=await (await fetch('/api/files')).json(),p=await (await fetch('/api/files?d=photos')).json(),sd=S.sd;
let h=`<div class="card"><h2 style="margin-top:0">Screensaver photos (${p.length})</h2><div class="dim">Photos are made screen-sized on your phone before sending.</div><div class="row"><input type="file" id="ph" accept="image/*" multiple style="max-width:none;flex:1"><button class="b t" onclick="sendPhotos()">Send</button></div><div id="php" class="dim"></div>`;
h+=p.map(x=>`<div class="row"><a href="/f?d=photos&n=${encodeURIComponent(x.n)}" target="_blank">${E(x.n)}</a><button class="b" onclick="del('photos','${E(x.n)}')">✕</button></div>`).join('')+'</div>';
h+=`<div class="card"><h2 style="margin-top:0">Settings backup</h2><div class="dim">${sd.backup?'last backup '+E(sd.backup):'no backup yet'} · ${E(sd.job)}</div><div class="row"><button class="b" onclick="post('/api/job',{j:'backup'})">Back up now</button><button class="b" onclick="confirm('Restore all settings from the card and restart?')&&post('/api/job',{j:'restore'})">Restore</button></div></div>`;
h+=`<div class="card"><h2 style="margin-top:0">Update</h2><div class="row"><span>Version ${E(S.ver)} <span class="dim">${E(S.upd)}</span></span><button class="b" onclick="confirm('The board restarts, looks for a new version on GitHub and installs it. Go?')&&post('/api/job',{j:'check'})">Check now</button></div><div class="dim">Or send a firmware .bin straight to the board${sd.upd?' · update.bin on the card: '+sd.upd+' KB':''}</div><div class="row"><input type="file" id="fw" accept=".bin" style="max-width:none;flex:1"><button class="b t" onclick="sendFw()">Install</button></div><div id="fwp" class="dim"></div></div>`;
h+=`<div class="card"><h2 style="margin-top:0">Log files on the card</h2><div class="dim">${E(sd.msg)}</div><table>`+f.sort((a,b)=>a.n<b.n?1:-1).map(x=>`<tr><td><a href="/f?n=${encodeURIComponent(x.n)}&dl=1">${E(x.n)}</a></td><td class="dim">${size(x.s)}</td></tr>`).join('')+'</table></div>';return h}
async function del(d,n){if(confirm('Delete '+n+'?')){await post('/api/del',{d,n});render(true)}}
async function shrink(file){const bmp=await createImageBitmap(file,{imageOrientation:'from-image'}).catch(()=>createImageBitmap(file));const c=document.createElement('canvas');c.width=320;c.height=240;const x=c.getContext('2d');
const port=bmp.height>bmp.width;x.fillStyle='#000';x.fillRect(0,0,320,240);const z=port?Math.min(320/bmp.width,240/bmp.height):Math.max(320/bmp.width,240/bmp.height);const w=bmp.width*z,h=bmp.height*z;x.drawImage(bmp,(320-w)/2,(240-h)/2,w,h);
return new Promise(r=>c.toBlob(r,'image/jpeg',0.88))}
async function sendPhotos(){const fs=[...$('#ph').files];let i=0;for(const f of fs){i++;$('#php').textContent=`Sending ${i}/${fs.length}...`;const b=await shrink(f);const fd=new FormData();
fd.append('f',b,'p'+Date.now().toString(36)+i+'.jpg');const r=await fetch('/upload?d=photos',{method:'POST',body:fd});if(!r.ok){$('#php').textContent='Failed: '+await r.text();return}}$('#php').textContent='Done: '+fs.length+' photos';setTimeout(()=>render(true),300)}
function sendFw(){const f=$('#fw').files[0];if(!f)return;if(!confirm('Install '+f.name+'? The board restarts.'))return;const x=new XMLHttpRequest(),fd=new FormData();fd.append('fw',f,f.name);
x.upload.onprogress=e=>$('#fwp').textContent='Sending '+Math.round(e.loaded*100/e.total)+'%';x.onload=()=>$('#fwp').textContent=x.status==200?'Installed, the board restarts...':'Failed: '+x.responseText;x.open('POST','/update');x.send(fd)}
let last='',shown='';async function render(force){if(!S)return;const m=$('#main');if(shown!=tab){force=true;shown=tab}
if(['Lists','Trips','Files','Settings'].includes(tab)){if(!force)return;
const fn={Lists:lists,Trips:trips,Files:files,Settings:async()=>settings()}[tab];m.innerHTML=await fn(true);return}
const h={Live:live,Bank:bank,Weather:weather,Level:level}[tab]();if(h!=last||force){if(document.activeElement&&document.activeElement.tagName=='INPUT'&&!force)return;m.innerHTML=h;last=h}}
nav();load();setInterval(()=>{if(!document.hidden)load()},4000);
</script></body></html>)HTML";
}  // namespace app
