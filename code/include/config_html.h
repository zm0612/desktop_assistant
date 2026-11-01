//
// Created by Zhang Zhimeng on 2026/8/28.
//

#pragma once
#include <Arduino.h>

// ============================ 配置门户前端 (Task 6) ============================
// 单文件、零外部依赖（无 CDN/字体/JS 库），内联 CSS + 原生 JS，整体存于 PROGMEM。
// 由 WebPortal 的 GET / 用 send_P 直接吐出。数据流：
//   加载：GET /api/status 填状态栏；GET /api/config 预填各字段并按序建页面行
//   保存：按 schema 组装 JSON -> POST /api/config -> {ok:true} 后遮罩 + 5s 重载
// 字段绑定用 data-bind="点路径"（与 Config.cpp 的 toJson/fromJson 键名逐一对应）。
// 密码/令牌回读为空串：留空提交则后端保留原值（见 Config::fromJson）。
// ==============================================================================
static constexpr char CONFIG_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>设备配置</title>
<style>
:root{--bg:#0e1116;--card:#171c24;--line:#232a34;--tx:#e6e9ee;--mut:#8a94a3;--ac:#22d3ee;--fld:#0e131a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.5 -apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;-webkit-font-smoothing:antialiased}
a{color:var(--ac);text-decoration:none}
.wrap{max-width:560px;margin:0 auto;padding:0 14px 92px}
.top{position:sticky;top:0;z-index:5;background:rgba(14,17,22,.86);backdrop-filter:blur(10px);border-bottom:1px solid var(--line)}
.top .in{max-width:560px;margin:0 auto;padding:10px 14px;display:flex;align-items:center;gap:10px;font-size:13px;color:var(--mut)}
.dot{width:8px;height:8px;border-radius:50%;flex:none}
.dot.g{background:#34d399;box-shadow:0 0 8px #34d399}
.dot.a{background:#fbbf24;box-shadow:0 0 8px #fbbf24}
.top b{color:var(--tx);font-weight:600}
.top .sp{margin-left:auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-top:14px;box-shadow:0 1px 3px rgba(0,0,0,.35)}
.card h2{margin:0 0 14px;font-size:15px;font-weight:600;display:flex;align-items:center;gap:9px}
.card h2::before{content:"";width:3px;height:15px;background:var(--ac);border-radius:2px}
.f{margin-bottom:13px}
.f:last-child{margin-bottom:0}
.f>label{display:block;font-size:13px;color:var(--mut);margin-bottom:6px}
.hint{color:#5f6b7a;font-weight:400}
input[type=text],input[type=password],input[type=number],select{width:100%;background:var(--fld);border:1px solid var(--line);border-radius:9px;color:var(--tx);padding:10px 12px;font-size:15px;outline:none;transition:border-color .15s,box-shadow .15s;-webkit-appearance:none;appearance:none}
input:focus,select:focus{border-color:var(--ac);box-shadow:0 0 0 3px rgba(34,211,238,.15)}
input::placeholder{color:#4b5563}
select{background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8' viewBox='0 0 12 8'%3E%3Cpath fill='%238a94a3' d='M6 8 0 0h12z'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 14px center;padding-right:32px}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.sw{position:relative;display:inline-block;width:44px;height:26px;flex:none}
.sw input{position:absolute;opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:#2a323d;border-radius:20px;transition:.2s;cursor:pointer}
.sw span::before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.sw input:checked+span{background:var(--ac)}
.sw input:checked+span::before{transform:translateX(18px)}
.tog{display:flex;align-items:center;justify-content:space-between}
#pages .prow{display:flex;align-items:center;gap:10px;padding:9px 0;border-top:1px solid var(--line)}
#pages .prow:first-child{border-top:0}
.prow .pn{flex:1;font-size:14px}
.prow .pdwell{width:62px;padding:7px 8px;text-align:center}
.prow .unit{color:var(--mut);font-size:12px;margin-left:-4px}
.mv{width:32px;height:32px;flex:none;background:var(--fld);border:1px solid var(--line);border-radius:8px;color:var(--tx);font-size:14px;line-height:1;cursor:pointer}
.mv:active{background:var(--ac);color:#04151a;border-color:var(--ac)}
.radios{display:flex;flex-direction:column;gap:9px}
.rad{display:flex;align-items:center;gap:11px;padding:11px 13px;border:1px solid var(--line);border-radius:9px;cursor:pointer;font-size:14px}
.rad input{accent-color:var(--ac);width:18px;height:18px;flex:none}
.rad.on{border-color:var(--ac);background:rgba(34,211,238,.06)}
.sub{font-size:13px;color:var(--mut);margin:15px 0 4px;font-weight:600}
#faceMap .mrow,#gestMap .mrow{display:flex;align-items:center;gap:10px;padding:8px 0;border-top:1px solid var(--line)}
#faceMap .mrow:first-child,#gestMap .mrow:first-child{border-top:0}
.mrow .mlab{flex:1;font-size:14px}
.mrow select{width:150px;flex:none}
#motionCfg.off,#autoCfg.off{display:none}
.ota input[type=file]{width:100%;font-size:13px;color:var(--mut);margin-bottom:12px}
.otaprog{margin:12px 0 4px}
.otaprog .bar{height:10px;background:var(--fld);border:1px solid var(--line);border-radius:6px;overflow:hidden}
.otaprog .bar i{display:block;height:100%;width:0;background:var(--ac);transition:width .2s}
.note{font-size:12px;color:var(--mut);margin-top:10px;line-height:1.6}
.btn{background:var(--ac);color:#04151a;border:0;border-radius:9px;padding:12px 16px;font-size:15px;font-weight:700;cursor:pointer;width:100%}
.btn:disabled{opacity:.6}
.btn.sec{background:var(--fld);color:var(--tx);border:1px solid var(--line);font-weight:600}
.bot{position:fixed;left:0;right:0;bottom:0;z-index:5;background:rgba(14,17,22,.92);backdrop-filter:blur(10px);border-top:1px solid var(--line);padding:12px 14px calc(12px + env(safe-area-inset-bottom))}
.bot .in{max-width:560px;margin:0 auto}
.ov{position:fixed;inset:0;z-index:50;background:rgba(8,10,14,.95);display:none;flex-direction:column;align-items:center;justify-content:center;gap:18px;text-align:center;padding:24px}
.ov.on{display:flex}
.spin{width:44px;height:44px;border:3px solid var(--line);border-top-color:var(--ac);border-radius:50%;animation:sp 1s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<div class="top"><div class="in">
  <span class="dot a" id="dot"></span>
  <span id="mode">…</span>
  <span id="ip">-</span>
  <span class="sp">v<b id="ver">-</b></span>
  <span>堆 <b id="heap">-</b></span>
</div></div>

<div class="wrap">

  <div class="card">
    <h2>网络</h2>
    <div class="f"><label>WiFi 名称 (SSID)</label><input type="text" data-bind="net.ssid" autocomplete="off"></div>
    <div class="f"><label>WiFi 密码</label><input type="password" data-bind="net.pass" placeholder="已设置，留空=不改" autocomplete="new-password"></div>
    <div class="f"><label>主机名</label><input type="text" data-bind="net.host"></div>
  </div>

  <div class="card">
    <h2>天气</h2>
    <div class="f"><label>心知天气 API Key</label><input type="text" data-bind="weather.key" autocomplete="off"></div>
    <div class="f"><label>城市</label><input type="text" data-bind="weather.loc"></div>
  </div>

  <div class="card">
    <h2>股票</h2>
    <div class="f"><label>代码列表 <span class="hint">逗号分隔</span></label><input type="text" data-bind="stock.symbols"></div>
    <div class="row2">
      <div class="f"><label>指数个数</label><input type="number" data-bind="stock.indexCount" min="0" max="12"></div>
      <div class="f"><label>概览代码</label><input type="text" data-bind="stock.overview"></div>
    </div>
  </div>

  <div class="card">
    <h2>米家 (HA)</h2>
    <div class="f"><label>HA 地址</label><input type="text" data-bind="ha.url" placeholder="http://192.168.x.x:8123" autocomplete="off"></div>
    <div class="f"><label>长期访问令牌</label><input type="password" data-bind="ha.token" placeholder="已设置，留空=不改" autocomplete="new-password"></div>
    <div class="row2">
      <div class="f"><label>温度</label><input type="text" data-bind="ha.temp"></div>
      <div class="f"><label>湿度</label><input type="text" data-bind="ha.humi"></div>
      <div class="f"><label>PM2.5</label><input type="text" data-bind="ha.pm25"></div>
      <div class="f"><label>净化器</label><input type="text" data-bind="ha.fan"></div>
      <div class="f"><label>灯</label><input type="text" data-bind="ha.light"></div>
      <div class="f"><label>电视</label><input type="text" data-bind="ha.tv"></div>
      <div class="f"><label>其它</label><input type="text" data-bind="ha.other"></div>
    </div>
  </div>

  <div class="card">
    <h2>消息 (MQTT)</h2>
    <div class="row2">
      <div class="f"><label>Broker</label><input type="text" data-bind="mqtt.broker"></div>
      <div class="f"><label>端口</label><input type="number" data-bind="mqtt.port" min="1" max="65535"></div>
    </div>
    <div class="f"><label>主题 (Topic)</label><input type="text" data-bind="mqtt.topic"></div>
  </div>

  <div class="card">
    <h2>新闻</h2>
    <div class="f"><label>RSS 地址</label><input type="text" data-bind="news.feed"></div>
    <div class="f"><label>来源名称</label><input type="text" data-bind="news.source"></div>
  </div>

  <div class="card">
    <h2>系统</h2>
    <div class="f"><label>时区</label><select data-bind="sys.tzOffset"></select></div>
    <div class="tog"><span>背光</span>
      <label class="sw"><input type="checkbox" data-bind="sys.backlight"><span></span></label>
    </div>
  </div>

  <div class="card">
    <h2>切换方式</h2>
    <div class="radios">
      <label class="rad"><input type="radio" name="sw.mode" value="auto"><span>定时轮播</span></label>
      <label class="rad"><input type="radio" name="sw.mode" value="orient"><span>体感（朝向 + 手势）</span></label>
    </div>
    <div id="autoCfg">
      <div class="sub">页面</div>
      <div id="pages"></div>
      <div class="note">开关控制是否显示，↑↓ 调整顺序，秒数为停留时长。</div>
    </div>
    <div id="motionCfg">
      <div class="f" style="margin-top:14px">
        <label>手势触发后停留 <span class="hint">秒，到期退回按朝向显示</span></label>
        <input type="number" data-bind="sw.holdS" min="1" max="60">
      </div>
      <div class="sub">朝向 → 页面</div>
      <div id="faceMap"></div>
      <div class="sub">手势 → 页面</div>
      <div id="gestMap"></div>
      <div class="note">“体感”用加速度计翻页：某朝向持续保持即锁定显示对应页；手势（摇一摇/双击/甩动）触发后跳到对应页并停留上面的秒数，到期再退回按朝向显示。当前朝向未映射时按各页停留时长自动轮播（不会卡死单页；页面的启用/顺序/停留时长在“定时轮播”下设置）。<br>朝向的物理含义随设备安装方向而定，可看串口日志确认 FaceUp/Left 等对应的实际摆放。选“（不切换）”表示该动作不翻页；仅“页面”里已启用的页可被选中生效。</div>
    </div>
  </div>

  <div class="card ota">
    <h2>固件升级 (OTA)</h2>
    <input type="file" id="fw" accept=".bin">
    <button class="btn sec" id="otaBtn" type="button">上传并升级</button>
    <div class="otaprog" id="otaProg" style="display:none"><div class="bar"><i id="otaBar"></i></div><div class="note" id="otaMsg"></div></div>
    <div class="note">选择固件 .bin 上传，升级中请勿断电；完成后设备自动重启并刷新本页。</div>
  </div>

</div>

<div class="bot"><div class="in">
  <button class="btn" id="save">保存并重启</button>
</div></div>

<div class="ov" id="ov">
  <div class="spin"></div>
  <div><b>已保存，设备重启中…</b></div>
  <div class="note">约 5 秒后自动刷新本页</div>
</div>

<script>
(function(){
var PN={weather:"天气",stock:"股票",home:"米家",message:"消息",system:"系统",pet:"萌宠",news:"新闻",pc:"电脑",clock:"时钟",gpu:"GPU",train:"训练",overview:"概览"};
var PAGE_IDS=["weather","stock","home","message","system","pet","news","pc","clock","gpu","train","overview"];
var FACES=[["up","朝上 FaceUp"],["down","朝下 FaceDown"],["left","左 Left"],["right","右 Right"],["top","顶 Top"],["bottom","底 Bottom"]];
var GESTS=[["swipeLeft","向左甩"],["swipeRight","向右甩"],["doubleTap","双击"],["shake","摇一摇"]];
function el(n){return document.querySelector('[data-bind="'+n+'"]');}
function v(n){var e=el(n);return e?e.value:"";}
function iv(n){var e=el(n);return e?(parseInt(e.value,10)||0):0;}
function byPath(o,p){return p.split(".").reduce(function(a,k){return a==null?a:a[k];},o);}
function buildTz(){
  var s=el("sys.tzOffset");if(!s||s.options.length)return;
  for(var h=-12;h<=14;h++){
    var o=document.createElement("option");
    o.value=h*3600;o.textContent="UTC"+(h>=0?"+":"")+h+":00";
    s.appendChild(o);
  }
}
function pageRow(p){
  var d=document.createElement("div");
  d.className="prow";d.dataset.id=p.id;
  d.innerHTML='<label class="sw"><input type="checkbox" class="pen"'+(p.en?" checked":"")+'><span></span></label>'
    +'<span class="pn">'+(PN[p.id]||p.id)+'</span>'
    +'<input type="number" class="pdwell" min="1" value="'+(parseInt(p.dwell,10)||8)+'"><span class="unit">秒</span>'
    +'<button type="button" class="mv" data-d="-1">↑</button>'
    +'<button type="button" class="mv" data-d="1">↓</button>';
  return d;
}
function markRadios(){
  var ls=document.querySelectorAll(".rad");
  for(var i=0;i<ls.length;i++)ls[i].classList.toggle("on",ls[i].querySelector("input").checked);
}
function mapSelect(cur){
  var s=document.createElement("select");
  var o0=document.createElement("option");o0.value="";o0.textContent="（不切换）";s.appendChild(o0);
  for(var i=0;i<PAGE_IDS.length;i++){
    var id=PAGE_IDS[i],o=document.createElement("option");
    o.value=id;o.textContent=PN[id]||id;
    if(id===cur)o.selected=true;
    s.appendChild(o);
  }
  return s;
}
function buildMap(boxId,defs,obj){
  var box=document.getElementById(boxId);box.innerHTML="";
  for(var i=0;i<defs.length;i++){
    var row=document.createElement("div");row.className="mrow";
    var lab=document.createElement("span");lab.className="mlab";lab.textContent=defs[i][1];
    var sel=mapSelect((obj&&obj[defs[i][0]])||"");sel.dataset.k=defs[i][0];
    row.appendChild(lab);row.appendChild(sel);box.appendChild(row);
  }
}
function collectMap(boxId){
  var o={},sels=document.querySelectorAll("#"+boxId+" select");
  for(var i=0;i<sels.length;i++)o[sels[i].dataset.k]=sels[i].value;
  return o;
}
function toggleMode(mode){
  var a=document.getElementById("autoCfg"),m=document.getElementById("motionCfg");
  if(a)a.className=(mode==="auto")?"":"off";
  if(m)m.className=(mode==="auto")?"off":"";
}
function fill(cfg){
  buildTz();
  var bs=document.querySelectorAll("[data-bind]");
  for(var i=0;i<bs.length;i++){
    var e=bs[i],val=byPath(cfg,e.getAttribute("data-bind"));
    if(val===undefined||val===null)continue;
    if(e.type==="checkbox")e.checked=!!val;else e.value=val;
  }
  var mode=(cfg.sw&&cfg.sw.mode)||"auto";
  var r=document.querySelector('input[name="sw.mode"][value="'+mode+'"]');
  if(r)r.checked=true;
  markRadios();
  buildMap("faceMap",FACES,(cfg.sw&&cfg.sw.faces)||{});
  buildMap("gestMap",GESTS,(cfg.sw&&cfg.sw.gestures)||{});
  toggleMode(mode);
  var box=document.getElementById("pages");box.innerHTML="";
  var ps=Array.isArray(cfg.pages)?cfg.pages:[];
  for(var j=0;j<ps.length;j++)box.appendChild(pageRow(ps[j]));
}
function collect(){
  var chk=el("sys.backlight");
  var c={
    net:{ssid:v("net.ssid"),pass:v("net.pass"),host:v("net.host")},
    weather:{key:v("weather.key"),loc:v("weather.loc")},
    stock:{symbols:v("stock.symbols"),overview:v("stock.overview"),indexCount:iv("stock.indexCount")},
    ha:{url:v("ha.url"),token:v("ha.token"),temp:v("ha.temp"),humi:v("ha.humi"),pm25:v("ha.pm25"),fan:v("ha.fan"),light:v("ha.light"),tv:v("ha.tv"),other:v("ha.other")},
    mqtt:{broker:v("mqtt.broker"),topic:v("mqtt.topic"),port:iv("mqtt.port")},
    news:{feed:v("news.feed"),source:v("news.source")},
    sys:{tzOffset:iv("sys.tzOffset"),backlight:chk?chk.checked:true},
    pages:[],
    sw:{
      mode:(document.querySelector('input[name="sw.mode"]:checked')||{}).value||"auto",
      holdS:iv("sw.holdS"),
      faces:collectMap("faceMap"),
      gestures:collectMap("gestMap")
    }
  };
  var rows=document.querySelectorAll("#pages .prow");
  for(var i=0;i<rows.length;i++){
    var row=rows[i];
    c.pages.push({id:row.dataset.id,en:row.querySelector(".pen").checked,dwell:parseInt(row.querySelector(".pdwell").value,10)||0});
  }
  return c;
}
function loadStatus(){
  fetch("/api/status").then(function(r){return r.json();}).then(function(s){
    var dot=document.getElementById("dot");
    if(s.mode==="sta"){dot.className="dot g";document.getElementById("mode").textContent="STA";}
    else{dot.className="dot a";document.getElementById("mode").textContent="配网";}
    var ip=document.getElementById("ip");
    if(s.mode==="sta"&&s.ip)ip.innerHTML='<a href="http://'+s.ip+'/">'+s.ip+"</a>";
    else ip.textContent=s.ip||"-";
    document.getElementById("ver").textContent=s.version||"-";
    if(s.heap!=null)document.getElementById("heap").textContent=((s.heap/1024)|0)+"K";
  }).catch(function(){});
}
function loadConfig(){
  fetch("/api/config").then(function(r){return r.json();}).then(function(c){fill(c||{});}).catch(function(){});
}
function save(){
  var btn=document.getElementById("save");btn.disabled=true;
  fetch("/api/config",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(collect())})
    .then(function(r){return r.json();})
    .then(function(j){
      if(j&&j.ok){document.getElementById("ov").classList.add("on");setTimeout(function(){location.reload();},5000);}
      else{btn.disabled=false;alert("保存失败："+((j&&j.err)||"未知错误"));}
    })
    .catch(function(e){btn.disabled=false;alert("保存失败："+e);});
}
document.getElementById("save").addEventListener("click",save);
document.getElementById("pages").addEventListener("click",function(e){
  var b=e.target.closest(".mv");if(!b)return;
  var row=b.closest(".prow"),d=+b.dataset.d;
  if(d<0&&row.previousElementSibling)row.parentNode.insertBefore(row,row.previousElementSibling);
  if(d>0&&row.nextElementSibling)row.parentNode.insertBefore(row.nextElementSibling,row);
});
var rs=document.querySelectorAll('input[name="sw.mode"]');
function onMode(){markRadios();toggleMode((document.querySelector('input[name="sw.mode"]:checked')||{}).value||"auto");}
for(var i=0;i<rs.length;i++)rs[i].addEventListener("change",onMode);
function ota(){
  var f=document.getElementById("fw");
  if(!f.files||!f.files.length){alert("请先选择固件 .bin 文件");return;}
  var btn=document.getElementById("otaBtn"),box=document.getElementById("otaProg"),bar=document.getElementById("otaBar"),msg=document.getElementById("otaMsg");
  btn.disabled=true;box.style.display="block";bar.style.width="0";msg.textContent="准备上传…";
  var reached=false,fd=new FormData();fd.append("update",f.files[0],f.files[0].name);
  var xhr=new XMLHttpRequest();xhr.open("POST","/update");
  xhr.upload.onprogress=function(e){
    if(!e.lengthComputable)return;
    var p=Math.round(e.loaded*100/e.total);bar.style.width=p+"%";
    if(p>=100){reached=true;msg.textContent="写入固件中，请勿断电…";}else msg.textContent="上传中 "+p+"%";
  };
  xhr.onload=function(){
    var t=xhr.responseText||"";
    if(xhr.status===200&&!/error/i.test(t)){bar.style.width="100%";msg.textContent="升级成功，设备重启中…约 10 秒后自动刷新";setTimeout(function(){location.reload();},10000);}
    else{btn.disabled=false;msg.textContent="升级失败："+(t||("HTTP "+xhr.status));}
  };
  xhr.onerror=function(){
    if(reached){msg.textContent="设备已重启，约 10 秒后自动刷新";setTimeout(function(){location.reload();},10000);}
    else{btn.disabled=false;msg.textContent="上传中断，请重试";}
  };
  xhr.send(fd);
}
document.getElementById("otaBtn").addEventListener("click",ota);
loadStatus();loadConfig();
})();
</script>
</body>
</html>
)HTML";
