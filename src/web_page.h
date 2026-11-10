// The page a Wi-Fi light serves at /: which light this is (with Blink), the color it shows, Fun or CheerLights mode,
// the fun palette and any color, its animations, speed for LED lights, and brightness.
// It uses the same API as the app (docs/wifi.md), loads nothing from the internet, and must stay one document
// small enough for flash (about 16 KB). The literal must never contain the closing delimiter.
#pragma once

namespace cheerlights {

constexpr char WEB_PAGE[] = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" id="tc" content="#f3f4f1">
<title>CheerLights light</title>
<link rel="icon" id="fav" href="data:,">
<style>
:root{--ground:#f3f4f1;--surface:#fbfbfa;--ink:#16181b;--muted:#4c5056;--line:#c4c7c0;--hover:#33363b;--focus:#2f62f0;--ease:cubic-bezier(.22,1,.36,1)}
@media (prefers-color-scheme:dark){:root{--ground:#141517;--surface:#1d1f22;--ink:#e9eae6;--muted:#a9ada6;--line:#45494e;--hover:#fbfbfa;--focus:#6fa4ff}}
*{box-sizing:border-box}
body{margin:0;background:var(--ground);color:var(--ink);font:16px/1.5 "Source Sans 3","Source Sans Pro",system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-text-size-adjust:100%}
.page{max-width:1120px;margin:0 auto;padding:0 16px 88px}
header{display:grid;grid-template-columns:1fr auto;gap:6px 16px;align-items:center;padding:20px 0}
h1{margin:0;font-size:28px;line-height:1.1;letter-spacing:-.01em}
header p{grid-column:1/-1;margin:0;max-width:62ch;text-wrap:pretty}
.mono{font-family:ui-monospace,"Cascadia Mono",Menlo,Consolas,monospace;font-size:.92em}
.btn{font:inherit;font-weight:600;min-height:44px;min-width:104px;padding:0 20px;border-radius:6px;border:1px solid var(--ink);background:var(--ink);color:var(--ground);cursor:pointer;transition:transform .12s var(--ease)}
.btn:hover{background:var(--hover)}
.btn:active{transform:scale(.97)}
.btn:disabled{cursor:default;transform:none;background:var(--ink)}
a{color:inherit;text-underline-offset:3px;text-decoration-thickness:1px}
a:hover{text-decoration-thickness:2px}
:focus-visible{outline:3px solid var(--focus);outline-offset:2px}
.now{margin:0 -16px}
.field{position:relative;min-height:248px;padding:24px 16px;display:flex;flex-direction:column;justify-content:flex-end;border-block:1px solid var(--line);background:var(--surface);transition:background-color .5s var(--ease),color .5s var(--ease)}
.field svg{position:absolute;top:20px;right:12px;width:104px;height:104px}
.field circle{fill:var(--fc,var(--surface));transition:fill .5s var(--ease)}
.cname{margin:0;padding-right:8px;font-size:48px;line-height:1;letter-spacing:-.025em;font-weight:700;overflow-wrap:anywhere}
.cline{margin:12px 16px 0;max-width:60ch;text-wrap:pretty}
.v:empty::before{content:"";display:inline-block;width:var(--w,9ch);height:.75em;border-radius:3px;background:var(--line)}
fieldset{border:0;margin:0;padding:0;min-width:0}
.ctl{padding:28px 0 0;display:flex;flex-direction:column;gap:36px}
.ctl:disabled{opacity:.6}
legend,.range label,h2{display:block;padding:0;margin:0 0 12px;font-size:16px;font-weight:700}
.hint{margin:10px 0 0;color:var(--muted);text-wrap:pretty}
.r{position:absolute;opacity:0;width:1px;height:1px;margin:0}
.seg{display:grid;grid-template-columns:1fr 1fr;border:1px solid var(--ink);border-radius:6px;overflow:hidden}
.seg label,.fx label{position:relative}
.seg span{display:flex;align-items:center;justify-content:center;min-height:48px;font-weight:600;cursor:pointer}
.seg span:hover{background:var(--surface)}
.seg input:checked+span,.fx input:checked+span{background:var(--ink);color:var(--ground);border-color:var(--ink)}
.seg input:focus-visible+span{outline:3px solid var(--focus);outline-offset:-6px}
.bulbs{display:grid;grid-template-columns:repeat(6,1fr);row-gap:14px;isolation:isolate}
.bulb{position:relative;display:flex;flex-direction:column;align-items:center;gap:10px;font-size:14px;line-height:1.2;cursor:pointer}
.bulb::before{content:"";position:absolute;left:0;right:0;top:21px;height:2px;background:var(--ink);z-index:-1}
.bulb i,.pick input{display:block;width:44px;height:44px;border-radius:50%;border:2px solid var(--ink);background:var(--c)}
.bulb input:checked+i,.pick.on input{outline:3px solid var(--ink);outline-offset:3px}
.bulb input:checked~span,.pick.on span{font-weight:700}
.bulb input:focus-visible+i,.pick input:focus-visible{outline:3px solid var(--focus);outline-offset:3px}
.pick input{-webkit-appearance:none;appearance:none;padding:3px;margin:0;border-style:dashed;background:var(--ground);cursor:pointer}
.pick.on input{border-style:solid}
.pick input::-webkit-color-swatch-wrapper{padding:0}
.pick input::-webkit-color-swatch{border:0;border-radius:50%}
.pick input::-moz-color-swatch{border:0;border-radius:50%}
.fx{display:flex;flex-wrap:wrap;gap:8px}
.fx span{display:flex;align-items:center;min-height:44px;padding:0 14px;border:1px solid var(--line);border-radius:6px;background:var(--surface);font-weight:600;cursor:pointer}
.fx span:hover{border-color:var(--ink)}
.fx input:disabled+span{border-style:dashed;background:none;color:var(--muted);cursor:not-allowed}
.fx input:focus-visible+span{outline:3px solid var(--focus);outline-offset:2px}
.range{display:grid;grid-template-columns:1fr auto;align-items:baseline}
.range label{margin:0}
.range output{color:var(--muted);font-variant-numeric:tabular-nums}
.range input{grid-column:1/-1;width:100%;height:44px;margin:0;accent-color:var(--ink);cursor:pointer}
.setup{border-top:1px solid var(--line);margin-top:40px;padding-top:24px}
.setup p{margin:0 0 12px;max-width:60ch}
.setup .out{display:inline-flex;align-items:center;min-height:44px}
.status{position:fixed;left:16px;right:16px;bottom:16px;max-width:560px;margin:0 auto;display:flex;gap:16px;align-items:center;justify-content:space-between;padding:10px 10px 10px 16px;border-radius:6px;background:var(--ink);color:var(--ground);box-shadow:0 8px 28px rgba(12,13,15,.22)}
.status .btn{background:var(--ground);color:var(--ink);border-color:var(--ground);flex:none}
[hidden]{display:none!important}
@media (min-width:880px){
.page{padding:0 32px 72px}
header{padding:32px 0 28px}
main{display:grid;grid-template-columns:minmax(0,1fr) 420px;gap:56px;align-items:start}
.now{margin:0;position:sticky;top:24px}
.field{min-height:min(540px,calc(100vh - 200px));padding:32px;border:1px solid var(--line)}
.field svg{top:28px;right:28px;width:168px;height:168px}
.cname{font-size:64px}
.cline{margin:16px 0 0}
.ctl{padding-top:0}
}
@media (prefers-reduced-motion:reduce){.btn{transition:none}}
</style>
</head>
<body>
<div class="page">
<header>
<h1><span class="v" id="n" style="--w:5ch"></span></h1>
<button class="btn" id="blink" type="button">Blink</button>
<p class="v" id="about" style="--w:30ch"></p>
</header>
<main>
<section class="now" aria-labelledby="cname">
<div class="field" id="field">
<svg viewBox="16 16 480 480" aria-hidden="true"><g transform="rotate(-18 256 256)" fill="none" stroke="currentColor" stroke-width="24"><path d="M176,256A80,172 0 1 1 336,256A80,172 0 1 1 176,256Z"/><path d="M84,256A172,172 0 1 1 428,256A172,172 0 1 1 84,256Z"/><path d="M66,236Q256,316 446,236" stroke-linecap="round"/><g stroke-width="21.6"><circle cx="66" cy="236" r="30"/><circle cx="180" cy="269.6" r="30"/><circle cx="332" cy="269.6" r="30"/><circle cx="446" cy="236" r="30"/></g></g></svg>
<p class="cname v" id="cname" style="--w:4ch"></p>
</div>
<p class="cline v" id="cline" style="--w:36ch"></p>
</section>
<div class="side">
<fieldset class="ctl" id="ctl" disabled>
<fieldset>
<legend>Mode</legend>
<div class="seg"><label><input class="r" type="radio" name="mode" value="fun" aria-describedby="modehint"><span>Fun</span></label><label><input class="r" type="radio" name="mode" value="cheerlights" aria-describedby="modehint"><span>CheerLights</span></label></div>
<p class="hint" id="modehint">Fun mode changes only this light. The CheerLights color everyone shares stays as it is.</p>
</fieldset>
<fieldset id="fun" hidden>
<legend>Fun color</legend>
<div class="bulbs" id="bulbs"><label class="bulb pick" id="pick"><input type="color" id="any"><span>Any color</span></label></div>
</fieldset>
<fieldset>
<legend>Animation</legend>
<div class="fx" id="fx"></div>
<p class="hint" id="fxnote" hidden></p>
</fieldset>
<div class="range" id="speedrow" hidden><label for="speed">Speed</label><output id="sv" for="speed"></output><input type="range" id="speed" min="1" max="255"></div>
<div class="range"><label for="bright">Brightness</label><output id="bv" for="bright"></output><input type="range" id="bright" min="1" max="255"></div>
</fieldset>
<section class="setup" aria-labelledby="setuph">
<h2 id="setuph">Wi-Fi and wiring</h2>
<p>You change these over USB, not on this page. Plug the light into a computer and open <a href="https://cheerlights.com/setup" target="_blank" rel="noopener">cheerlights.com/setup</a>.</p>
<p><a class="out" href="https://cheerlights.com" target="_blank" rel="noopener">About CheerLights</a></p>
</section>
</div>
</main>
</div>
<div class="status" id="status" role="status" hidden><span id="msg"></span><button class="btn" id="retry" type="button" hidden>Try again</button></div>
<script>
const PALETTE=[["red","#ff0000"],["green","#008000"],["blue","#0000ff"],["cyan","#00ffff"],["white","#ffffff"],["oldlace","#fdf5e6"],["purple","#800080"],["magenta","#ff00ff"],["yellow","#ffff00"],["orange","#ffa500"],["pink","#ffc0cb"]];
const OUT={pixels:"its LEDs",rgb:"its RGB LED",screen:"its screen"};
const DARK="#0c0d0f",LIGHT="#fbfbfa";
const $=id=>document.getElementById(id),E=t=>document.createElement(t),N=n=>document.getElementsByName(n);
let S=null,I=null,gen=0,heard=0,down=false,fxKey="",holdB=0,holdS=0,holdP=0,tB,tS,tP,msgKind="";
const api=(p,body)=>fetch("/api/v1/"+p,body?{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)}:{cache:"no-store"});
function lum(h){const c=[1,3,5].map(i=>parseInt(h.substr(i,2),16)/255).map(v=>v<=.04045?v/12.92:Math.pow((v+.055)/1.055,2.4));return .2126*c[0]+.7152*c[1]+.0722*c[2];}
function inkFor(h){const L=lum(h);return (L+.05)/(lum(DARK)+.05)>=(lum(LIGHT)+.05)/(L+.05)?DARK:LIGHT;}
function dim(h){return "#"+[1,3,5].map(i=>Math.round(parseInt(h.substr(i,2),16)*.5).toString(16).padStart(2,"0")).join("");}
function nameOf(h){const p=PALETTE.find(x=>x[1]===h.toLowerCase());return p?p[0]:null;}
function who(){return I?I.name:"The light";}
function pct(v){return Math.max(1,Math.round(v/255*100))+"%";}
function pace(v){return v<86?"Slow":v<171?"Medium":"Fast";}
function say(text,kind){msgKind=kind;$("msg").textContent=text;$("retry").hidden=kind!=="down";$("status").hidden=false;}
function hush(kind){if(!kind||msgKind===kind){msgKind="";$("status").hidden=true;}}
function setAbout(){
 const n=I.name,host="cheerlights-"+n.slice(3).toLowerCase()+".local",ip=/^\d+\.\d+\.\d+\.\d+$/.test(location.hostname)?location.hostname:"";
 $("n").textContent=n;document.title=n+", a CheerLights light";
 $("blink").setAttribute("aria-label","Blink "+n+" so you can find it");
 const p=$("about");p.textContent=(I.board==="Custom"?"A board wired by hand":I.board)+". It shows the color on "+(OUT[I.output]||I.output)+", runs firmware "+I.fw+", and answers at ";
 const a=E("span");a.className="mono";a.textContent=host;p.append(a);
 if(ip){const b=E("span");b.className="mono";b.textContent=ip;p.append(" or ",b);}
 p.append(".");
 $("speedrow").hidden=I.output==="screen";
}
function buildFx(){
 const key=S.effects.map(e=>e.id+e.name+e.showsColor).join();if(key===fxKey)return;fxKey=key;
 const box=$("fx");box.textContent="";
 for(const e of S.effects){const l=E("label"),r=E("input"),s=E("span");
  r.type="radio";r.name="fx";r.className="r";r.value=e.id;r.onchange=()=>send({effect:e.id});s.textContent=e.name;l.append(r,s);box.append(l);}
}
function render(){
 if(!S)return;
 const fun=S.mode==="fun",cl=S.cheerlights,fx=S.effects.find(e=>e.id===S.effect);
 let hex=null,name="Waiting",line;
 if(fun){hex=S.fun.hex;name=nameOf(hex)||hex.toUpperCase();
  line=fx&&!fx.showsColor?fx.name+" plays its own colors. Pick another animation to see your fun color.":"Your fun color."+(cl?" The CheerLights color is "+cl.name+" right now, and this light goes back to it when you switch.":"");}
 else if(cl){hex=cl.live?cl.hex:dim(cl.hex);name=cl.name;
  line=cl.live?"The CheerLights color right now. Anyone can change it, and lights all over the world change with it.":"This light has lost the CheerLights feed for now, so it shows the last color, dimmed.";}
 else line="This light is waiting for its first CheerLights color.";
 const shown=fun?S.fun.hex:cl?cl.hex:null;
 if(shown&&nameOf(shown))line+=" Its hex code is "+shown.toUpperCase()+".";
 const f=$("field");
 f.style.background=hex||"";f.style.color=hex?inkFor(hex):"";f.style.setProperty("--fc",hex||"var(--surface)");
 $("cname").textContent=name;$("cline").textContent=line;
 $("tc").content=hex||getComputedStyle(document.body).backgroundColor;
 $("fav").href="data:image/svg+xml,"+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><circle cx="8" cy="8" r="7" fill="'+(hex||"#c4c7c0")+'" stroke="#16181b"/></svg>');
 for(const r of N("mode"))r.checked=r.value===S.mode;
 $("fun").hidden=!fun;
 const pal=nameOf(S.fun.hex);
 for(const r of N("fun"))r.checked=fun&&r.value===S.fun.hex.toLowerCase();
 $("pick").classList.toggle("on",fun&&!pal);
 if(Date.now()-holdP>1500)$("any").value=S.fun.hex;
 buildFx();
 const no=[];
 for(const r of N("fx")){const e=S.effects.find(x=>x.id==r.value);r.checked=e.id===S.effect;r.disabled=!fun&&!e.showsColor;if(r.disabled)no.push(e.name);}
 $("fxnote").hidden=!no.length;
 $("fxnote").textContent=no.length>1?no.join(" and ")+" make their own colors, so they play in Fun mode only.":no[0]+" makes its own colors, so it plays in Fun mode only.";
 if(Date.now()-holdB>1500)$("bright").value=S.brightness;
 if(Date.now()-holdS>1500)$("speed").value=S.speed;
 $("bv").textContent=pct($("bright").value);$("sv").textContent=pace($("speed").value);
}
async function send(change){
 gen++;hush("refused");
 try{
  const r=await api("state",change);let j={};try{j=await r.json();}catch(e){}
  if(r.ok){S=j;heard=Date.now();if(down){down=false;$("ctl").disabled=false;hush("down");}}
  else say(who()+" did not take that change"+(j.error?": "+j.error:"."),"refused");
 }catch(e){say(who()+" did not answer, so nothing changed. Check that it is on and on this Wi-Fi.","down");}
 render();
}
async function load(){
 const g=gen;
 if(!I)api("info").then(r=>r.json()).then(j=>{I=j;setAbout();if(down)lost();}).catch(()=>{});
 try{const r=await api("state");if(!r.ok)throw 0;const j=await r.json();if(g!==gen)return;
  S=j;heard=Date.now();if(down||$("ctl").disabled){down=false;$("ctl").disabled=false;hush("down");}render();}
 catch(e){down=true;lost();}
}
function lost(){
 $("ctl").disabled=true;
 say(who()+" is not answering. Check that it is on and on this Wi-Fi."+(heard?" Last heard at "+new Date(heard).toLocaleTimeString([],{hour:"numeric",minute:"2-digit"})+".":" Still trying."),"down");
}
for(const [n,h] of PALETTE){const l=E("label"),r=E("input"),i=E("i"),s=E("span");
 l.className="bulb";r.type="radio";r.name="fun";r.className="r";r.value=h;r.onchange=()=>send({fun:{hex:h}});i.style.setProperty("--c",h);s.textContent=n;l.append(r,i,s);$("bulbs").insertBefore(l,$("pick"));}
for(const r of N("mode"))r.onchange=()=>send({mode:r.value});
$("any").oninput=e=>{holdP=Date.now();clearTimeout(tP);tP=setTimeout(()=>send({fun:{hex:e.target.value}}),150);};
$("bright").oninput=e=>{holdB=Date.now();$("bv").textContent=pct(e.target.value);clearTimeout(tB);tB=setTimeout(()=>send({brightness:+e.target.value}),150);};
$("speed").oninput=e=>{holdS=Date.now();$("sv").textContent=pace(e.target.value);clearTimeout(tS);tS=setTimeout(()=>send({speed:+e.target.value}),150);};
$("retry").onclick=load;
$("blink").onclick=async()=>{
 const b=$("blink");b.disabled=true;b.textContent="Blinking";
 try{const r=await api("identify",{});
  if(r.status===404)say("Update the light's firmware to blink it.","refused");
  else if(!r.ok)throw 0;
  else{const j=await r.json();await new Promise(z=>setTimeout(z,j.identifying||3000));}}
 catch(e){say(who()+" did not answer, so it did not blink. Check that it is on and on this Wi-Fi.","down");}
 b.disabled=false;b.textContent="Blink";
};
document.addEventListener("visibilitychange",()=>{if(!document.hidden)load();});
load();setInterval(()=>{if(!document.hidden)load();},4000);
</script>
</body>
</html>
)html";

}  // namespace cheerlights
