#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#define NUM_SERVOS   5
#define MAX_PRESETS  16   // max stored presets
#define MAX_STEPS    20   // max steps per preset

const char* ssid     = "RoboHand-S3";
const char* password = "12345678";

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

int  currentAngles[NUM_SERVOS]      = {0,   0,   0,   0,   0};
int  calibrationOffsets[NUM_SERVOS] = {0,   0,   0,   0,   0};

int  servoMin[NUM_SERVOS]           = {150, 150, 150, 150, 150};
int  servoMax[NUM_SERVOS]           = {600, 600, 600, 600, 600};
int  servoMaxAngle[NUM_SERVOS]      = {180, 180, 180, 180, 180};
int  servoSpeed[NUM_SERVOS]         = {0, 0, 0, 0, 0};
bool servoReversed[NUM_SERVOS]      = {false, false, false, false, false};
char servoLabels[NUM_SERVOS][16]    = {"Thumb","Index","Middle","Ring","Pinky"};

float currentPhysicalAngles[NUM_SERVOS] = {0, 0, 0, 0, 0};
unsigned long lastUpdateTime = 0;

// ── Preset data structures ──────────────────────────────────────────────────
struct PresetStep {
  int  angles[NUM_SERVOS]; // target angle for each servo
  int  speed;              // deg/sec for this step (0 = instant)
  int  holdMs;             // how long to hold after reaching position (ms)
};

struct Preset {
  bool       used;
  char       name[24];
  int        stepCount;
  PresetStep steps[MAX_STEPS];
};

Preset presets[MAX_PRESETS];

// ── Preset playback state ───────────────────────────────────────────────────
bool     presetRunning    = false;
int      presetRunId      = -1;
int      presetRunStep    = -1;
unsigned long presetHoldEnd = 0;        // millis when current hold expires
bool     presetMoving     = false;      // waiting for servos to reach target
int      presetSpeedOverride[NUM_SERVOS]; // per-step speed stored here
int      presetTargets[NUM_SERVOS];       // current step's angle targets

WebServer server(80);
Preferences prefs;

// ══════════════════════════════════════════════════════════════════════════════
//  HTML / CSS / JS  (single embedded page)
// ══════════════════════════════════════════════════════════════════════════════
const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no"/>
<title>RoboHand Controller</title>
<style>
:root{--bg:#05111f;--s1:#0d1f35;--s2:#152840;--ac:#00d4ff;--pu:#7c3aed;--tx:#e2e8f0;--mu:#64748b;--ok:#22c55e;--er:#ef4444;--wa:#f59e0b;--bd:#1e3a5f}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:var(--tx);min-height:100vh}
.hdr{background:linear-gradient(135deg,var(--s1),var(--s2));padding:16px 20px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--bd);position:sticky;top:0;z-index:99}
.hdr-title{font-size:20px;font-weight:800;background:linear-gradient(135deg,var(--ac),var(--pu));-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.hdr-sub{font-size:12px;color:var(--mu);display:flex;align-items:center;gap:6px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--ok);animation:blink 2s infinite;display:inline-block}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.3}}
.tabs{display:flex;background:var(--s1);border-bottom:1px solid var(--bd);overflow-x:auto}
.tab{flex:1;min-width:70px;padding:14px 8px;text-align:center;font-size:13px;font-weight:600;color:var(--mu);cursor:pointer;border-bottom:3px solid transparent;transition:.2s;white-space:nowrap}
.tab.on{color:var(--ac);border-color:var(--ac)}
.pane{display:none}.pane.on{display:block}
.wrap{padding:14px;max-width:560px;margin:0 auto}
/* ── Quick Actions ── */
.qa{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:16px}
.qb{background:var(--s1);border:1px solid var(--bd);border-radius:16px;padding:14px 8px;text-align:center;cursor:pointer;transition:.15s;user-select:none}
.qb:active{transform:scale(.93);background:var(--s2)}
.qb .ico{font-size:26px;line-height:1.2}
.qb .lbl{font-size:11px;font-weight:600;color:var(--mu);margin-top:4px}
.qb.hi{border-color:var(--ac)}.qb.hi .lbl{color:var(--ac)}
/* ── Finger cards ── */
.fc{display:flex;flex-direction:column;gap:12px}
.fcard{background:var(--s1);border:2px solid var(--bd);border-radius:20px;padding:18px;transition:border-color .3s}
.fcard.lit{border-color:var(--ac)}
.fhdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}
.fbadge{width:36px;height:36px;border-radius:10px;display:flex;align-items:center;justify-content:center;font-weight:800;font-size:15px;color:#fff;flex-shrink:0;margin-right:10px}
.fname{font-size:16px;font-weight:700}
.fsub{font-size:12px;color:var(--mu);margin-top:2px}
.anum{font-size:24px;font-weight:800;color:var(--ac);min-width:56px;text-align:right}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:8px;background:var(--s2);border-radius:999px;outline:none;margin:6px 0;display:block}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:28px;height:28px;border-radius:50%;background:linear-gradient(135deg,var(--ac),var(--pu));cursor:pointer;box-shadow:0 0 10px #00d4ff55}
input[type=range]:active::-webkit-slider-thumb{box-shadow:0 0 20px #00d4ffaa}
.row{display:flex;align-items:center;gap:10px;margin-top:10px}
.sbtn{width:44px;height:44px;border-radius:12px;border:1px solid var(--bd);background:var(--s2);color:var(--tx);font-size:22px;font-weight:700;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:.15s;flex-shrink:0;user-select:none;line-height:1}
.sbtn:active{background:var(--ac);color:#000}
.hint{flex:1;text-align:center;color:var(--mu);font-size:11px}
/* ── Calibrate ── */
.ccard{background:var(--s1);border:1px solid var(--bd);border-radius:18px;padding:18px;margin-bottom:12px}
.chdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
.cname{font-weight:600;font-size:15px}
.obadge{border-radius:8px;padding:5px 12px;font-size:14px;font-weight:700;background:var(--s2);color:var(--mu)}
.obadge.pos{color:var(--ok)}.obadge.neg{color:var(--er)}
.rst{width:100%;padding:16px;background:linear-gradient(135deg,var(--ac),var(--pu));border:none;border-radius:14px;color:#fff;font-weight:700;font-size:16px;cursor:pointer;margin-top:4px}
.rst:active{opacity:.8}
/* ── Config ── */
.cfcard{background:var(--s1);border:1px solid var(--bd);border-radius:18px;padding:18px;margin-bottom:12px}
.cfhdr{display:flex;align-items:center;gap:10px;margin-bottom:14px}
.fl{font-size:11px;color:var(--mu);font-weight:600;margin-bottom:5px}
.tinp{width:100%;padding:10px 12px;background:var(--s2);border:1px solid var(--bd);border-radius:10px;color:var(--tx);font-size:14px;outline:none;margin-bottom:12px}
.tinp:focus{border-color:var(--ac)}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}
.ninp{width:100%;padding:10px 8px;background:var(--s2);border:1px solid var(--bd);border-radius:10px;color:var(--tx);font-size:14px;outline:none;text-align:center}
.ninp:focus{border-color:var(--ac)}
.trow{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}
.trow span{font-size:14px}
.togbtn{padding:7px 16px;border-radius:8px;border:1px solid var(--bd);background:var(--s2);color:var(--mu);font-size:13px;font-weight:600;cursor:pointer;transition:.2s}
.togbtn.on{background:var(--ac);color:#000;border-color:var(--ac)}
.scfg{width:100%;padding:13px;background:linear-gradient(135deg,var(--ac),var(--pu));border:none;border-radius:12px;color:#fff;font-weight:700;font-size:14px;cursor:pointer}
.scfg:active{opacity:.8}
/* ══ PRESETS TAB ══════════════════════════════════════════════════════════ */
.pst-toprow{display:flex;gap:10px;margin-bottom:14px;margin-top:14px}
.pst-newbtn{flex:1;padding:13px;background:linear-gradient(135deg,var(--ac),var(--pu));border:none;border-radius:14px;color:#fff;font-weight:700;font-size:14px;cursor:pointer}
.pst-newbtn:active{opacity:.8}
.pst-list{display:flex;flex-direction:column;gap:10px}
.pst-card{background:var(--s1);border:1px solid var(--bd);border-radius:18px;padding:16px;display:flex;align-items:center;gap:12px}
.pst-card.running{border-color:var(--ok);box-shadow:0 0 12px #22c55e33}
.pst-icon{width:44px;height:44px;border-radius:12px;background:var(--s2);display:flex;align-items:center;justify-content:center;font-size:22px;flex-shrink:0}
.pst-info{flex:1;min-width:0}
.pst-name{font-weight:700;font-size:15px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pst-meta{font-size:11px;color:var(--mu);margin-top:3px}
.pst-btns{display:flex;gap:7px;flex-shrink:0}
.ic-btn{width:38px;height:38px;border-radius:10px;border:1px solid var(--bd);background:var(--s2);color:var(--tx);font-size:17px;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:.15s}
.ic-btn:active{opacity:.7}
.ic-btn.run{background:#0f3d1a;border-color:var(--ok);color:var(--ok)}
.ic-btn.edit{background:#0d2040;border-color:var(--ac);color:var(--ac)}
.ic-btn.del{background:#3d0f0f;border-color:var(--er);color:var(--er)}
.ic-btn.stop{background:#3d1f00;border-color:var(--wa);color:var(--wa)}
/* ── Preset Editor ── */
.pe-back{display:flex;align-items:center;gap:8px;margin-bottom:14px;cursor:pointer;color:var(--ac);font-weight:600;font-size:14px;margin-top:14px}
.pe-back:active{opacity:.7}
.pe-namebox{background:var(--s1);border:1px solid var(--bd);border-radius:16px;padding:16px;margin-bottom:12px}
.pe-steps{display:flex;flex-direction:column;gap:10px;margin-bottom:12px}
.step-card{background:var(--s1);border:2px solid var(--bd);border-radius:18px;padding:16px;position:relative}
.step-card.active-step{border-color:var(--ok)}
.step-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
.step-num{width:30px;height:30px;border-radius:8px;background:var(--pu);display:flex;align-items:center;justify-content:center;font-weight:800;font-size:13px;color:#fff;flex-shrink:0}
.step-title{font-weight:700;font-size:14px;flex:1;margin-left:8px}
.step-del{width:30px;height:30px;border-radius:8px;border:1px solid var(--er);background:#3d0f0f;color:var(--er);font-size:16px;cursor:pointer;display:flex;align-items:center;justify-content:center}
.step-del:active{opacity:.6}
.step-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}
.sg-cell{background:var(--s2);border:1px solid var(--bd);border-radius:10px;padding:10px;display:flex;flex-direction:column;gap:4px}
.sg-lbl{font-size:10px;color:var(--mu);font-weight:600}
.sg-val{font-size:18px;font-weight:800;color:var(--ac);text-align:center}
.sg-rng{width:100%;-webkit-appearance:none;appearance:none;height:6px;background:var(--bg);border-radius:999px;outline:none}
.sg-rng::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:linear-gradient(135deg,var(--ac),var(--pu));cursor:pointer}
.step-timing{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.st-cell{background:var(--s2);border:1px solid var(--bd);border-radius:10px;padding:10px}
.st-lbl{font-size:10px;color:var(--mu);font-weight:600;margin-bottom:4px}
.st-inp{width:100%;background:transparent;border:none;color:var(--tx);font-size:16px;font-weight:700;text-align:center;outline:none}
.add-step-btn{width:100%;padding:13px;background:var(--s1);border:2px dashed var(--bd);border-radius:14px;color:var(--mu);font-weight:600;font-size:14px;cursor:pointer;transition:.2s}
.add-step-btn:hover{border-color:var(--ac);color:var(--ac)}
.add-step-btn:active{opacity:.7}
.pe-save{width:100%;padding:15px;background:linear-gradient(135deg,var(--ac),var(--pu));border:none;border-radius:14px;color:#fff;font-weight:700;font-size:16px;cursor:pointer;margin-top:6px}
.pe-save:active{opacity:.8}
.pe-run{width:100%;padding:13px;background:linear-gradient(135deg,#166534,#15803d);border:none;border-radius:14px;color:#fff;font-weight:700;font-size:14px;cursor:pointer;margin-top:8px}
.pe-run:active{opacity:.8}
/* step move arrows */
.step-arrows{display:flex;gap:4px}
.arr-btn{width:26px;height:26px;border-radius:6px;border:1px solid var(--bd);background:var(--s2);color:var(--mu);font-size:13px;cursor:pointer;display:flex;align-items:center;justify-content:center}
.arr-btn:active{background:var(--ac);color:#000}
/* status pill */
.status-pill{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);background:var(--s1);border:1px solid var(--bd);border-radius:999px;padding:8px 20px;font-size:13px;font-weight:600;color:var(--mu);transition:.3s;z-index:200;pointer-events:none;opacity:0}
.status-pill.show{opacity:1}
.status-pill.ok{border-color:var(--ok);color:var(--ok)}
.status-pill.er{border-color:var(--er);color:var(--er)}
.status-pill.ru{border-color:var(--wa);color:var(--wa)}
</style>
</head>
<body>
<div class="hdr">
  <div class="hdr-title">&#129302; RoboHand</div>
  <div class="hdr-sub"><span class="dot"></span>192.168.4.1</div>
</div>
<div class="tabs">
  <div class="tab on"  id="tab-ctrl" onclick="sw('ctrl')">&#9654; Control</div>
  <div class="tab"     id="tab-pst"  onclick="sw('pst')">&#127381; Presets</div>
  <div class="tab"     id="tab-cal"  onclick="sw('cal')">&#9881; Calibrate</div>
  <div class="tab"     id="tab-cfg"  onclick="sw('cfg')">&#9965; Configure</div>
</div>

<!-- ════ CONTROL PANE ════ -->
<div id="pane-ctrl" class="pane on">
  <div class="wrap">
    <div class="qa" style="margin-top:14px">
      <div class="qb" onclick="setAll(0)"><div class="ico">&#x270A;</div><div class="lbl">Fist 0&#176;</div></div>
      <div class="qb hi" onclick="setAll(90)"><div class="ico">&#x1F590;</div><div class="lbl">Home 90&#176;</div></div>
      <div class="qb" onclick="setAll(180)"><div class="ico">&#x1F44B;</div><div class="lbl">Open 180&#176;</div></div>
    </div>
    <div class="fc" id="fingerCards"></div>
  </div>
</div>

<!-- ════ PRESETS PANE ════ -->
<div id="pane-pst" class="pane">
  <div id="pst-list-view">
    <div class="wrap">
      <div class="pst-toprow">
        <button class="pst-newbtn" onclick="newPreset()">&#43; New Preset</button>
      </div>
      <div class="pst-list" id="pstList"></div>
    </div>
  </div>
  <div id="pst-editor-view" style="display:none">
    <div class="wrap">
      <div class="pe-back" onclick="closeEditor()">&#8592; Back to Presets</div>
      <div class="pe-namebox">
        <div class="fl">Preset Name</div>
        <input type="text" class="tinp" id="pe-name" maxlength="23" placeholder="e.g. Wave Hello" style="margin-bottom:0"/>
      </div>
      <div class="pe-steps" id="peSteps"></div>
      <button class="add-step-btn" onclick="addStep()">&#43; Add Step</button>
      <button class="pe-save" onclick="savePreset()" style="margin-top:14px">&#10003; Save Preset</button>
      <button class="pe-run"  onclick="runEditorPreset()">&#9654; Save &amp; Run</button>
    </div>
  </div>
</div>

<!-- ════ CALIBRATE PANE ════ -->
<div id="pane-cal" class="pane">
  <div class="wrap">
    <div style="margin-top:14px" id="calCards"></div>
    <button class="rst" onclick="resetOffsets()">&#8635; Reset All Offsets to 0</button>
  </div>
</div>

<!-- ════ CONFIGURE PANE ════ -->
<div id="pane-cfg" class="pane">
  <div class="wrap" style="margin-top:14px" id="cfgCards"></div>
</div>

<div class="status-pill" id="statusPill"></div>

<script>
var NAMES  = ["Thumb","Index","Middle","Ring","Pinky"];
var COLORS = ["#00d4ff","#7c3aed","#10b981","#f59e0b","#f43f5e"];
var ABBR   = ["T","I","M","R","P"];
var MAX_ANGLES = [180,180,180,180,180];
var state  = {angles:[0,0,0,0,0], offsets:[0,0,0,0,0]};
var tmr    = null;
var revState = [false,false,false,false,false];

// ── Preset editor state ──────────────────────────────────────────────────────
var editingId  = -1;     // -1 = new preset
var editSteps  = [];     // array of {angles:[5], speed:int, holdMs:int}
var runningId  = -1;

// ════════════════════════════════════════════════════════════════════════════
//  TAB SWITCHER
// ════════════════════════════════════════════════════════════════════════════
function sw(name){
  ['ctrl','pst','cal','cfg'].forEach(function(t){
    document.getElementById('tab-'+t).classList.toggle('on',t===name);
    document.getElementById('pane-'+t).classList.toggle('on',t===name);
  });
  if(name==='pst') loadPresetList();
}

// ════════════════════════════════════════════════════════════════════════════
//  CONTROL TAB
// ════════════════════════════════════════════════════════════════════════════
function build(){
  var fc=document.getElementById('fingerCards');
  var cc=document.getElementById('calCards');
  for(var i=0;i<NAMES.length;i++){
    (function(idx){
      var c=document.createElement('div');
      c.className='fcard';c.id='fc'+idx;
      c.innerHTML='<div class="fhdr">'
        +'<div style="display:flex;align-items:center">'
        +'<div class="fbadge" style="background:'+COLORS[idx]+'">'+ABBR[idx]+'</div>'
        +'<div><div class="fname" id="fn'+idx+'">'+NAMES[idx]+'</div>'
        +'<div class="fsub">Servo '+(idx+1)
        +'<span id="revbadge'+idx+'" style="display:none;margin-left:6px;color:#f59e0b;font-size:11px;font-weight:700">&#8635; REV</span></div></div></div>'
        +'<div class="anum" id="an'+idx+'">90&#176;</div></div>'
        +'<input type="range" id="sl'+idx+'" min="0" max="180" value="90"/>'
        +'<div class="row">'
        +'<button class="sbtn" id="sm'+idx+'">&#8722;</button>'
        +'<div class="hint">0&#176; &larr; drag &rarr; 180&#176;</div>'
        +'<button class="sbtn" id="sp'+idx+'">+</button>'
        +'</div>';
      fc.appendChild(c);

      var k=document.createElement('div');
      k.className='ccard';
      k.innerHTML='<div class="chdr">'
        +'<div class="cname" id="cn'+idx+'" style="color:'+COLORS[idx]+'">'+NAMES[idx]+' Offset</div>'
        +'<div class="obadge" id="ob'+idx+'">0&#176;</div></div>'
        +'<input type="range" id="os'+idx+'" min="-60" max="60" value="0"/>'
        +'<div class="row">'
        +'<button class="sbtn" id="om'+idx+'">&#8722;</button>'
        +'<div class="hint">-60&#176; &larr; drag &rarr; +60&#176;</div>'
        +'<button class="sbtn" id="op'+idx+'">+</button>'
        +'</div>';
      cc.appendChild(k);

      document.getElementById('sl'+idx).addEventListener('input',function(e){setAngle(idx,+e.target.value,false);});
      document.getElementById('sl'+idx).addEventListener('change',function(e){setAngle(idx,+e.target.value,true);});
      document.getElementById('os'+idx).addEventListener('input',function(e){setOffset(idx,+e.target.value,false);});
      document.getElementById('os'+idx).addEventListener('change',function(e){setOffset(idx,+e.target.value,true);});

      addHold('sm'+idx,function(){step(idx,-5);});
      addHold('sp'+idx,function(){step(idx,+5);});
      addHold('om'+idx,function(){stepOff(idx,-1);});
      addHold('op'+idx,function(){stepOff(idx,+1);});
    })(i);
  }
}

function addHold(id,fn){
  var el=document.getElementById(id);
  function go(){fn();clearInterval(tmr);tmr=setInterval(fn,120);}
  function stop(){clearInterval(tmr);}
  el.addEventListener('mousedown',go);
  el.addEventListener('touchstart',function(e){e.preventDefault();go();},{passive:false});
  el.addEventListener('mouseup',stop);
  el.addEventListener('mouseleave',stop);
  el.addEventListener('touchend',stop);
}

function step(i,d){setAngle(i,Math.min(MAX_ANGLES[i],Math.max(0,state.angles[i]+d)),true);}
function stepOff(i,d){setOffset(i,Math.min(60,Math.max(-60,state.offsets[i]+d)),true);}

function setAngle(i,v,send){
  v=Math.min(MAX_ANGLES[i],Math.max(0,Math.round(v)));
  state.angles[i]=v;
  document.getElementById('sl'+i).value=v;
  document.getElementById('an'+i).textContent=v+'\u00b0';
  var fc=document.getElementById('fc'+i);
  fc.classList.add('lit');
  clearTimeout(fc._t);
  fc._t=setTimeout(function(){fc.classList.remove('lit');},900);
  if(send)fetch('/api/set?servo='+i+'&angle='+v).catch(function(e){console.error(e);});
}

function setOffset(i,v,send){
  v=Math.min(60,Math.max(-60,Math.round(v)));
  state.offsets[i]=v;
  document.getElementById('os'+i).value=v;
  var b=document.getElementById('ob'+i);
  b.textContent=(v>=0?'+':'')+v+'\u00b0';
  b.className='obadge'+(v>0?' pos':v<0?' neg':'');
  if(send)fetch('/api/offset?servo='+i+'&offset='+v).catch(function(e){console.error(e);});
}

function setAll(a){fetch('/api/all?angle='+a).then(refreshStatus).catch(function(e){console.error(e);});}
function resetOffsets(){fetch('/api/reset').then(refreshStatus).catch(function(e){console.error(e);});}

function refreshStatus(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    state=d;
    for(var i=0;i<NAMES.length;i++){
      setAngle(i,d.angles[i],false);
      setOffset(i,d.offsets[i],false);
    }
  }).catch(function(e){console.error(e);});
}

// ════════════════════════════════════════════════════════════════════════════
//  CONFIG TAB
// ════════════════════════════════════════════════════════════════════════════
function buildCfg(){
  var cc=document.getElementById('cfgCards');
  for(var i=0;i<NAMES.length;i++){
    (function(idx){
      var d=document.createElement('div');
      d.className='cfcard';
      d.innerHTML=
        '<div class="cfhdr"><div class="fbadge" style="background:'+COLORS[idx]+'">'+ABBR[idx]+'</div>'
        +'<div style="font-weight:700;font-size:16px" id="cft'+idx+'">'+NAMES[idx]+'</div></div>'
        +'<div class="fl">Finger Name</div>'
        +'<input type="text" class="tinp" id="cfn'+idx+'" maxlength="15" placeholder="e.g. Thumb" value="'+NAMES[idx]+'"/>'
        +'<div class="g2">'
        +'<div><div class="fl">Min Pulse (0&#176;)</div><input type="number" class="ninp" id="cfmn'+idx+'" min="100" max="400" value="150"/></div>'
        +'<div><div class="fl">Max Pulse (180&#176;)</div><input type="number" class="ninp" id="cfmx'+idx+'" min="300" max="800" value="600"/></div>'
        +'</div>'
        +'<div class="g2">'
        +'<div><div class="fl">Max Angle (&#176;)</div><input type="number" class="ninp" id="cfma'+idx+'" min="0" max="180" value="180"/></div>'
        +'<div><div class="fl">Speed (&#176;/s, 0=Inst)</div><input type="number" class="ninp" id="cfsp'+idx+'" min="0" max="1000" value="0"/></div>'
        +'</div>'
        +'<div class="trow"><span>Reverse Direction</span>'
        +'<button class="togbtn" id="rv'+idx+'" onclick="toggleRev('+idx+')">&#x2715; Normal</button></div>'
        +'<button class="scfg" onclick="saveCfg('+idx+')">&#10003; Save '+NAMES[idx]+' Config</button>';
      cc.appendChild(d);
    })(i);
  }
}

function toggleRev(i){
  revState[i]=!revState[i];
  var b=document.getElementById('rv'+i);
  b.classList.toggle('on',revState[i]);
  b.innerHTML=revState[i]?'&#x2714; Reversed':'&#x2715; Normal';
  fetch('/api/servoconfig?servo='+i+'&rev='+(revState[i]?'1':'0'))
    .then(function(r){return r.json();})
    .then(function(d){
      var badge=document.getElementById('revbadge'+i);
      if(badge) badge.style.display=revState[i]?'inline':'none';
    }).catch(function(e){console.error(e);});
}

function updateLabels(){
  NAMES.forEach(function(nm,i){
    var fn=document.getElementById('fn'+i); if(fn) fn.textContent=nm;
    var cn=document.getElementById('cn'+i); if(cn) cn.textContent=nm+' Offset';
    var ct=document.getElementById('cft'+i); if(ct) ct.textContent=nm;
    var sb=document.querySelector('#cfgCards .cfcard:nth-child('+(i+1)+') .scfg');
    if(sb) sb.textContent='\u2713 Save '+nm+' Config';
  });
}

function loadConfig(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    NAMES=d.labels.slice();
    MAX_ANGLES=d.maxAngles.slice();
    d.mins.forEach(function(v,i){var el=document.getElementById('cfmn'+i);if(el)el.value=v;});
    d.maxs.forEach(function(v,i){var el=document.getElementById('cfmx'+i);if(el)el.value=v;});
    d.maxAngles.forEach(function(v,i){
      var el=document.getElementById('cfma'+i);if(el)el.value=v;
      var sl=document.getElementById('sl'+i);if(sl)sl.max=v;
      var hint=document.querySelector('#fc'+i+' .hint');
      if(hint) hint.innerHTML='0&#176; &larr; drag &rarr; '+v+'&#176;';
    });
    d.speeds.forEach(function(v,i){var el=document.getElementById('cfsp'+i);if(el)el.value=v;});
    d.reversed.forEach(function(v,i){
      revState[i]=v;
      var b=document.getElementById('rv'+i);
      if(b){b.classList.toggle('on',v);b.innerHTML=v?'&#x2714; Reversed':'&#x2715; Normal';}
      var badge=document.getElementById('revbadge'+i);
      if(badge) badge.style.display=v?'inline':'none';
    });
    d.labels.forEach(function(nm,i){
      var el=document.getElementById('cfn'+i);if(el)el.value=nm;
    });
    updateLabels();
  }).catch(function(e){console.error(e);});
}

function saveCfg(i){
  var nm=encodeURIComponent((document.getElementById('cfn'+i).value.trim())||'Servo '+(i+1));
  var mn=document.getElementById('cfmn'+i).value;
  var mx=document.getElementById('cfmx'+i).value;
  var ma=document.getElementById('cfma'+i).value;
  var sp=document.getElementById('cfsp'+i).value;
  var rv=revState[i]?'1':'0';
  fetch('/api/servoconfig?servo='+i+'&min='+mn+'&max='+mx+'&rev='+rv+'&name='+nm+'&maxang='+ma+'&speed='+sp)
    .then(function(r){return r.json();})
    .then(function(d){
      NAMES=d.labels.slice();
      MAX_ANGLES=d.maxAngles.slice();
      MAX_ANGLES.forEach(function(v,idx){
         var sl=document.getElementById('sl'+idx);if(sl)sl.max=v;
         var hint=document.querySelector('#fc'+idx+' .hint');
         if(hint) hint.innerHTML='0&#176; &larr; drag &rarr; '+v+'&#176;';
      });
      updateLabels();
    }).catch(function(e){console.error(e);});
}

// ════════════════════════════════════════════════════════════════════════════
//  PRESETS TAB — List
// ════════════════════════════════════════════════════════════════════════════
function loadPresetList(){
  fetch('/api/presets').then(function(r){return r.json();}).then(function(list){
    var el=document.getElementById('pstList');
    el.innerHTML='';
    if(!list||!list.length){
      el.innerHTML='<div style="text-align:center;color:var(--mu);padding:40px 0;font-size:14px">No presets yet.<br>Tap <b style=\'color:var(--ac)\'>+ New Preset</b> to create one.</div>';
      return;
    }
    list.forEach(function(p){
      var card=document.createElement('div');
      card.className='pst-card'+(runningId===p.id?' running':'');
      card.id='pcard'+p.id;
      var icons=['&#127381;','&#9994;','&#128400;','&#9995;','&#128406;','&#127381;'];
      var ico=icons[p.id%icons.length];
      card.innerHTML=
        '<div class="pst-icon">'+ico+'</div>'
        +'<div class="pst-info">'
        +'<div class="pst-name">'+escHtml(p.name)+'</div>'
        +'<div class="pst-meta">'+p.stepCount+' step'+(p.stepCount!==1?'s':'')+'</div>'
        +'</div>'
        +'<div class="pst-btns">'
        +(runningId===p.id
          ?'<button class="ic-btn stop" onclick="stopPreset()" title="Stop">&#9632;</button>'
          :'<button class="ic-btn run" onclick="runPreset('+p.id+')" title="Run">&#9654;</button>')
        +'<button class="ic-btn edit" onclick="editPreset('+p.id+')" title="Edit">&#9998;</button>'
        +'<button class="ic-btn del"  onclick="deletePreset('+p.id+')" title="Delete">&#128465;</button>'
        +'</div>';
      el.appendChild(card);
    });
  }).catch(function(e){console.error(e);});
}

function runPreset(id){
  fetch('/api/runpreset?id='+id).then(function(r){return r.json();}).then(function(d){
    runningId=id;
    showPill('Running preset…','ru');
    loadPresetList();
    pollRunStatus();
  }).catch(function(e){console.error(e);});
}

function stopPreset(){
  fetch('/api/stoppreset').then(function(){
    runningId=-1;
    showPill('Stopped','ok');
    loadPresetList();
  }).catch(function(e){console.error(e);});
}

function pollRunStatus(){
  fetch('/api/runstatus').then(function(r){return r.json();}).then(function(d){
    if(d.running){
      setTimeout(pollRunStatus,600);
    } else {
      runningId=-1;
      loadPresetList();
      showPill('Preset done','ok');
    }
  }).catch(function(e){console.error(e);});
}

function deletePreset(id){
  if(!confirm('Delete this preset?')) return;
  fetch('/api/delpreset?id='+id).then(function(r){return r.json();}).then(function(){
    showPill('Deleted','er');
    loadPresetList();
  }).catch(function(e){console.error(e);});
}

function newPreset(){
  editingId=-1;
  document.getElementById('pe-name').value='';
  editSteps=[{angles:[90,90,90,90,90],speed:60,holdMs:500}];
  renderEditor();
  document.getElementById('pst-list-view').style.display='none';
  document.getElementById('pst-editor-view').style.display='block';
}

function editPreset(id){
  fetch('/api/preset?id='+id).then(function(r){return r.json();}).then(function(d){
    editingId=id;
    document.getElementById('pe-name').value=d.name;
    editSteps=d.steps.map(function(s){
      return {angles:s.angles.slice(),speed:s.speed,holdMs:s.holdMs};
    });
    renderEditor();
    document.getElementById('pst-list-view').style.display='none';
    document.getElementById('pst-editor-view').style.display='block';
  }).catch(function(e){console.error(e);showPill('Error loading preset','er');});
}

function closeEditor(){
  document.getElementById('pst-list-view').style.display='';
  document.getElementById('pst-editor-view').style.display='none';
  loadPresetList();
}

// ════════════════════════════════════════════════════════════════════════════
//  PRESET EDITOR — Step rendering
// ════════════════════════════════════════════════════════════════════════════
function renderEditor(){
  var container=document.getElementById('peSteps');
  container.innerHTML='';
  editSteps.forEach(function(s,si){
    var card=document.createElement('div');
    card.className='step-card';
    card.id='step-card-'+si;
    // per-servo sliders
    var servoCells='';
    for(var j=0;j<NUM_SERVOS;j++){
      servoCells+=
        '<div class="sg-cell">'
        +'<div class="sg-lbl">'+NAMES[j]+'</div>'
        +'<div class="sg-val" id="sv_'+si+'_'+j+'">'+s.angles[j]+'&#176;</div>'
        +'<input type="range" class="sg-rng" id="sr_'+si+'_'+j+'" min="0" max="'+MAX_ANGLES[j]+'" value="'+s.angles[j]+'" '
        +'oninput="stepAngleChange('+si+','+j+',this.value)"/>'
        +'</div>';
    }
    card.innerHTML=
      '<div class="step-hdr">'
      +'<div class="step-num">'+(si+1)+'</div>'
      +'<div class="step-title">Step '+(si+1)+'</div>'
      +'<div class="step-arrows">'
      +(si>0?'<button class="arr-btn" onclick="moveStep('+si+',-1)" title="Move up">&#8679;</button>':'')
      +(si<editSteps.length-1?'<button class="arr-btn" onclick="moveStep('+si+',1)" title="Move down">&#8681;</button>':'')
      +'</div>'
      +'<button class="step-del" onclick="delStep('+si+')" title="Remove step">&#215;</button>'
      +'</div>'
      +'<div class="step-grid">'+servoCells+'</div>'
      +'<div class="step-timing">'
      +'<div class="st-cell">'
      +'<div class="st-lbl">&#9201; Speed (&#176;/s, 0=Inst)</div>'
      +'<input type="number" class="st-inp" id="sspd_'+si+'" min="0" max="1000" value="'+s.speed+'" oninput="editSteps['+si+'].speed=+this.value"/>'
      +'</div>'
      +'<div class="st-cell">'
      +'<div class="st-lbl">&#9202; Hold (ms)</div>'
      +'<input type="number" class="st-inp" id="shld_'+si+'" min="0" max="30000" value="'+s.holdMs+'" oninput="editSteps['+si+'].holdMs=+this.value"/>'
      +'</div>'
      +'</div>'
      +'<div style="margin-top:8px;text-align:right">'
      +'<button style="padding:6px 12px;border-radius:8px;border:1px solid var(--bd);background:var(--s2);color:var(--mu);font-size:11px;font-weight:600;cursor:pointer" '
      +'onclick="captureStep('+si+')">&#128247; Capture current angles</button>'
      +'</div>';
    container.appendChild(card);
  });
}

var NUM_SERVOS=5;

function stepAngleChange(si,j,val){
  val=Math.min(MAX_ANGLES[j],Math.max(0,Math.round(+val)));
  editSteps[si].angles[j]=val;
  var el=document.getElementById('sv_'+si+'_'+j);
  if(el) el.textContent=val+'\u00b0';
}

function captureStep(si){
  // populate step with current servo angles
  for(var j=0;j<NUM_SERVOS;j++){
    editSteps[si].angles[j]=state.angles[j];
    var sr=document.getElementById('sr_'+si+'_'+j);
    var sv=document.getElementById('sv_'+si+'_'+j);
    if(sr) sr.value=state.angles[j];
    if(sv) sv.textContent=state.angles[j]+'\u00b0';
  }
  var card=document.getElementById('step-card-'+si);
  if(card){card.classList.add('active-step');setTimeout(function(){card.classList.remove('active-step');},800);}
  showPill('Captured step '+(si+1),'ok');
}

function addStep(){
  var prev=editSteps.length>0?editSteps[editSteps.length-1]:null;
  var newStep={
    angles: prev?prev.angles.slice():[90,90,90,90,90],
    speed: prev?prev.speed:60,
    holdMs: prev?prev.holdMs:500
  };
  editSteps.push(newStep);
  renderEditor();
}

function delStep(si){
  if(editSteps.length<=1){showPill('Need at least 1 step','er');return;}
  editSteps.splice(si,1);
  renderEditor();
}

function moveStep(si,dir){
  var ni=si+dir;
  if(ni<0||ni>=editSteps.length) return;
  var tmp=editSteps[si];
  editSteps[si]=editSteps[ni];
  editSteps[ni]=tmp;
  renderEditor();
}

function savePreset(){
  var name=(document.getElementById('pe-name').value.trim())||'Preset';
  var body=JSON.stringify({id:editingId,name:name,steps:editSteps});
  fetch('/api/savepreset',{method:'POST',headers:{'Content-Type':'application/json'},body:body})
    .then(function(r){return r.json();})
    .then(function(d){
      editingId=d.id;
      showPill('Saved!','ok');
    }).catch(function(e){console.error(e);showPill('Save failed','er');});
}

function runEditorPreset(){
  savePreset();
  setTimeout(function(){
    if(editingId>=0) runPreset(editingId);
    else showPill('Save first','er');
  },400);
}

// ════════════════════════════════════════════════════════════════════════════
//  UTILS
// ════════════════════════════════════════════════════════════════════════════
function escHtml(s){
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

var pillTimer=null;
function showPill(msg,cls){
  var p=document.getElementById('statusPill');
  p.textContent=msg;
  p.className='status-pill show '+(cls||'');
  clearTimeout(pillTimer);
  pillTimer=setTimeout(function(){p.classList.remove('show');},2200);
}

// ════════════════════════════════════════════════════════════════════════════
//  INIT
// ════════════════════════════════════════════════════════════════════════════
build();
buildCfg();
refreshStatus();
loadConfig();
</script>
</body>
</html>
)rawliteral";

// ══════════════════════════════════════════════════════════════════════════════
//  Servo helpers (unchanged)
// ══════════════════════════════════════════════════════════════════════════════
uint16_t angleToPulse(int id, float angle) {
  if (servoReversed[id]) angle = 180.0 - angle;
  return (uint16_t)(servoMin[id] + (angle / 180.0) * (servoMax[id] - servoMin[id]));
}

void applyAllServos() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    int target = currentAngles[i] + calibrationOffsets[i];
    target = constrain(target, 0, servoMaxAngle[i]);
    currentPhysicalAngles[i] = target;
    pwm.setPWM(i, 0, angleToPulse(i, currentPhysicalAngles[i]));
  }
}

// ══════════════════════════════════════════════════════════════════════════════
//  NVS persistence (servo config + calibration + angles — unchanged)
// ══════════════════════════════════════════════════════════════════════════════
void saveCalibration() {
  prefs.begin("servo-cal", false);
  for (int i = 0; i < NUM_SERVOS; i++)
    prefs.putInt((String("offset") + i).c_str(), calibrationOffsets[i]);
  prefs.end();
}
void loadCalibration() {
  prefs.begin("servo-cal", true);
  for (int i = 0; i < NUM_SERVOS; i++)
    calibrationOffsets[i] = prefs.getInt((String("offset") + i).c_str(), 0);
  prefs.end();
}
void saveAngles() {
  prefs.begin("servo-ang", false);
  for (int i = 0; i < NUM_SERVOS; i++)
    prefs.putInt((String("ang") + i).c_str(), currentAngles[i]);
  prefs.end();
}
void loadAngles() {
  prefs.begin("servo-ang", true);
  for (int i = 0; i < NUM_SERVOS; i++)
    currentAngles[i] = prefs.getInt((String("ang") + i).c_str(), 90);
  prefs.end();
}
void saveServoConfig() {
  prefs.begin("servo-cfg", false);
  for (int i = 0; i < NUM_SERVOS; i++) {
    prefs.putInt((String("min") + i).c_str(), servoMin[i]);
    prefs.putInt((String("max") + i).c_str(), servoMax[i]);
    prefs.putInt((String("ma") + i).c_str(), servoMaxAngle[i]);
    prefs.putInt((String("sp") + i).c_str(), servoSpeed[i]);
    prefs.putBool((String("rev") + i).c_str(), servoReversed[i]);
    prefs.putString((String("lbl") + i).c_str(), servoLabels[i]);
  }
  prefs.end();
}
void loadServoConfig() {
  prefs.begin("servo-cfg", true);
  for (int i = 0; i < NUM_SERVOS; i++) {
    servoMin[i]      = prefs.getInt((String("min") + i).c_str(), 150);
    servoMax[i]      = prefs.getInt((String("max") + i).c_str(), 600);
    servoMaxAngle[i] = prefs.getInt((String("ma") + i).c_str(), 180);
    servoSpeed[i]    = prefs.getInt((String("sp") + i).c_str(), 0);
    servoReversed[i] = prefs.getBool((String("rev") + i).c_str(), false);
    String lbl = prefs.getString((String("lbl") + i).c_str(), "");
    if (lbl.length() > 0) { strncpy(servoLabels[i], lbl.c_str(), 15); servoLabels[i][15] = '\0'; }
  }
  prefs.end();
}

// ══════════════════════════════════════════════════════════════════════════════
//  Preset NVS  — compact binary storage
//  Key scheme: "pN-hdr"  = header (name + stepCount)
//              "pN-sS"   = step S data
//  N = preset index (0-15), S = step index (0-19)
// ══════════════════════════════════════════════════════════════════════════════
void savePresets() {
  prefs.begin("pst", false);
  for (int i = 0; i < MAX_PRESETS; i++) {
    String hk = "p" + String(i) + "-u";
    prefs.putBool(hk.c_str(), presets[i].used);
    if (presets[i].used) {
      prefs.putString(("p" + String(i) + "-n").c_str(), presets[i].name);
      prefs.putInt(("p" + String(i) + "-sc").c_str(), presets[i].stepCount);
      for (int s = 0; s < presets[i].stepCount; s++) {
        String sp = "p" + String(i) + "s" + String(s);
        for (int j = 0; j < NUM_SERVOS; j++)
          prefs.putInt((sp + "a" + String(j)).c_str(), presets[i].steps[s].angles[j]);
        prefs.putInt((sp + "sp").c_str(), presets[i].steps[s].speed);
        prefs.putInt((sp + "hd").c_str(), presets[i].steps[s].holdMs);
      }
    }
  }
  prefs.end();
}

void loadPresets() {
  prefs.begin("pst", true);
  for (int i = 0; i < MAX_PRESETS; i++) {
    presets[i].used      = prefs.getBool(("p" + String(i) + "-u").c_str(), false);
    presets[i].stepCount = 0;
    memset(presets[i].name, 0, sizeof(presets[i].name));
    if (presets[i].used) {
      String nm = prefs.getString(("p" + String(i) + "-n").c_str(), "Preset");
      strncpy(presets[i].name, nm.c_str(), 23); presets[i].name[23] = '\0';
      presets[i].stepCount = prefs.getInt(("p" + String(i) + "-sc").c_str(), 0);
      for (int s = 0; s < presets[i].stepCount && s < MAX_STEPS; s++) {
        String sp = "p" + String(i) + "s" + String(s);
        for (int j = 0; j < NUM_SERVOS; j++)
          presets[i].steps[s].angles[j] = prefs.getInt((sp + "a" + String(j)).c_str(), 90);
        presets[i].steps[s].speed  = prefs.getInt((sp + "sp").c_str(), 60);
        presets[i].steps[s].holdMs = prefs.getInt((sp + "hd").c_str(), 500);
      }
    }
  }
  prefs.end();
}

// ══════════════════════════════════════════════════════════════════════════════
//  Preset playback helpers (called from loop())
// ══════════════════════════════════════════════════════════════════════════════
void startPresetStep(int pid, int stepIdx) {
  presetRunStep = stepIdx;
  PresetStep &s = presets[pid].steps[stepIdx];
  for (int i = 0; i < NUM_SERVOS; i++) {
    presetTargets[i]       = constrain(s.angles[i], 0, servoMaxAngle[i]);
    presetSpeedOverride[i] = s.speed;
  }
  presetMoving  = true;
  presetHoldEnd = 0;
}

bool stepReached() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    if (abs(currentPhysicalAngles[i] - presetTargets[i]) > 0.5) return false;
  }
  return true;
}

// ══════════════════════════════════════════════════════════════════════════════
//  HTTP helpers
// ══════════════════════════════════════════════════════════════════════════════
void sendJson(const String &payload) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", payload);
}

// ── Parse a JSON array of integers from a body string, populating dst[n] ──
// Minimal hand-rolled parser – avoids pulling in ArduinoJson.
bool parseIntArray(const String &body, const String &key, int *dst, int n) {
  int ki = body.indexOf("\"" + key + "\"");
  if (ki < 0) return false;
  int lb = body.indexOf('[', ki);
  int rb = body.indexOf(']', lb);
  if (lb < 0 || rb < 0) return false;
  String inner = body.substring(lb + 1, rb);
  int idx = 0;
  int pos = 0;
  while (idx < n && pos < (int)inner.length()) {
    int cm = inner.indexOf(',', pos);
    if (cm < 0) cm = inner.length();
    dst[idx++] = inner.substring(pos, cm).toInt();
    pos = cm + 1;
  }
  return true;
}

String extractString(const String &body, const String &key) {
  int ki = body.indexOf("\"" + key + "\"");
  if (ki < 0) return "";
  int q1 = body.indexOf('"', ki + key.length() + 3);
  if (q1 < 0) return "";
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return "";
  return body.substring(q1 + 1, q2);
}

int extractInt(const String &body, const String &key, int def = 0) {
  int ki = body.indexOf("\"" + key + "\"");
  if (ki < 0) return def;
  int col = body.indexOf(':', ki);
  if (col < 0) return def;
  return body.substring(col + 1).toInt();
}

// ══════════════════════════════════════════════════════════════════════════════
//  Route handlers
// ══════════════════════════════════════════════════════════════════════════════
void handleRoot() {
  server.sendHeader("Content-Type", "text/html");
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String json = "{";
  json += "\"angles\":[";
  for (int i = 0; i < NUM_SERVOS; i++) { json += String(currentAngles[i]); if (i<NUM_SERVOS-1) json+=','; }
  json += "],\"offsets\":[";
  for (int i = 0; i < NUM_SERVOS; i++) { json += String(calibrationOffsets[i]); if (i<NUM_SERVOS-1) json+=','; }
  json += "]}";
  sendJson(json);
}

void handleSetServo() {
  if (!server.hasArg("servo")||!server.hasArg("angle")){server.send(400,"text/plain","Missing param");return;}
  int id=server.arg("servo").toInt(), angle=server.arg("angle").toInt();
  if(id<0||id>=NUM_SERVOS){server.send(400,"text/plain","Bad id");return;}
  currentAngles[id]=constrain(angle,0,servoMaxAngle[id]);
  saveAngles(); handleStatus();
}

void handleSetOffset() {
  if(!server.hasArg("servo")||!server.hasArg("offset")){server.send(400,"text/plain","Missing param");return;}
  int id=server.arg("servo").toInt(), offset=server.arg("offset").toInt();
  if(id<0||id>=NUM_SERVOS){server.send(400,"text/plain","Bad id");return;}
  calibrationOffsets[id]=constrain(offset,-60,60);
  saveCalibration(); handleStatus();
}

void handleResetOffsets() {
  for(int i=0;i<NUM_SERVOS;i++) calibrationOffsets[i]=0;
  saveCalibration(); handleStatus();
}

void handleSetAllAngles() {
  if(!server.hasArg("angle")){server.send(400,"text/plain","Missing angle");return;}
  int angle=server.arg("angle").toInt();
  for(int i=0;i<NUM_SERVOS;i++) currentAngles[i]=constrain(angle,0,servoMaxAngle[i]);
  saveAngles(); handleStatus();
}

void handleGetConfig() {
  String j="{\"labels\":[";
  for(int i=0;i<NUM_SERVOS;i++){j+='\"';j+=servoLabels[i];j+='\"';if(i<NUM_SERVOS-1)j+=',';}
  j+="],\"mins\":[";  for(int i=0;i<NUM_SERVOS;i++){j+=String(servoMin[i]);if(i<NUM_SERVOS-1)j+=',';}
  j+="],\"maxs\":[";  for(int i=0;i<NUM_SERVOS;i++){j+=String(servoMax[i]);if(i<NUM_SERVOS-1)j+=',';}
  j+="],\"maxAngles\":["; for(int i=0;i<NUM_SERVOS;i++){j+=String(servoMaxAngle[i]);if(i<NUM_SERVOS-1)j+=',';}
  j+="],\"speeds\":["; for(int i=0;i<NUM_SERVOS;i++){j+=String(servoSpeed[i]);if(i<NUM_SERVOS-1)j+=',';}
  j+="],\"reversed\":["; for(int i=0;i<NUM_SERVOS;i++){j+=servoReversed[i]?"true":"false";if(i<NUM_SERVOS-1)j+=',';}
  j+="]}";
  sendJson(j);
}

void handleSetServoConfig() {
  if(!server.hasArg("servo")){server.send(400,"text/plain","Missing servo");return;}
  int id=server.arg("servo").toInt();
  if(id<0||id>=NUM_SERVOS){server.send(400,"text/plain","Bad id");return;}
  if(server.hasArg("min"))    servoMin[id]      =constrain(server.arg("min").toInt(),100,400);
  if(server.hasArg("max"))    servoMax[id]      =constrain(server.arg("max").toInt(),300,800);
  if(server.hasArg("maxang")) servoMaxAngle[id] =constrain(server.arg("maxang").toInt(),0,180);
  if(server.hasArg("speed"))  servoSpeed[id]    =constrain(server.arg("speed").toInt(),0,1000);
  if(server.hasArg("rev"))    servoReversed[id] =server.arg("rev").toInt()==1;
  if(server.hasArg("name")){
    String nm=server.arg("name"); nm=nm.substring(0,15);
    strncpy(servoLabels[id],nm.c_str(),15); servoLabels[id][15]='\0';
  }
  saveServoConfig(); handleGetConfig();
}

// ── Preset API ──────────────────────────────────────────────────────────────
void handleGetPresets() {
  // Return a lightweight list: id, name, stepCount for each used slot
  String j = "[";
  bool first = true;
  for (int i = 0; i < MAX_PRESETS; i++) {
    if (!presets[i].used) continue;
    if (!first) j += ',';
    first = false;
    j += "{\"id\":" + String(i) + ",\"name\":\"" + String(presets[i].name) + "\",\"stepCount\":" + String(presets[i].stepCount) + "}";
  }
  j += "]";
  sendJson(j);
}

void handleGetPreset() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "Missing id"); return; }
  int id = server.arg("id").toInt();
  if (id < 0 || id >= MAX_PRESETS || !presets[id].used) { server.send(404, "text/plain", "Not found"); return; }
  Preset &p = presets[id];
  String j = "{\"id\":" + String(id) + ",\"name\":\"" + String(p.name) + "\",\"steps\":[";
  for (int s = 0; s < p.stepCount; s++) {
    if (s) j += ',';
    j += "{\"angles\":[";
    for (int a = 0; a < NUM_SERVOS; a++) { j += String(p.steps[s].angles[a]); if (a < NUM_SERVOS-1) j += ','; }
    j += "],\"speed\":" + String(p.steps[s].speed) + ",\"holdMs\":" + String(p.steps[s].holdMs) + "}";
  }
  j += "]}";
  sendJson(j);
}

// POST /api/savepreset  body: {"id":-1,"name":"...","steps":[{"angles":[...],"speed":60,"holdMs":500},...]}
void handleSavePreset() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  String body = server.arg("plain");
  if (body.isEmpty()) { server.send(400, "text/plain", "Empty body"); return; }

  int reqId = extractInt(body, "id", -1);
  String name = extractString(body, "name");
  if (name.isEmpty()) name = "Preset";

  // Find slot
  int slot = -1;
  if (reqId >= 0 && reqId < MAX_PRESETS) {
    slot = reqId; // overwrite existing
  } else {
    for (int i = 0; i < MAX_PRESETS; i++) { if (!presets[i].used) { slot = i; break; } }
  }
  if (slot < 0) { server.send(507, "text/plain", "No free preset slots"); return; }

  presets[slot].used = true;
  strncpy(presets[slot].name, name.c_str(), 23); presets[slot].name[23] = '\0';

  // Parse steps — locate "steps" array manually
  int si = body.indexOf("\"steps\"");
  int lb = body.indexOf('[', si);
  int stepCount = 0;

  int pos = lb + 1;
  while (stepCount < MAX_STEPS) {
    int ob = body.indexOf('{', pos);
    if (ob < 0) break;
    // find matching }
    int depth = 1; int p2 = ob + 1;
    while (p2 < (int)body.length() && depth > 0) {
      if (body[p2] == '{') depth++;
      else if (body[p2] == '}') depth--;
      p2++;
    }
    if (depth != 0) break;
    String stepStr = body.substring(ob, p2);

    // Parse angles array
    int ab = stepStr.indexOf("\"angles\"");
    if (ab >= 0) {
      int al = stepStr.indexOf('[', ab);
      int ar = stepStr.indexOf(']', al);
      if (al >= 0 && ar >= 0) {
        String inner = stepStr.substring(al+1, ar);
        int ai = 0, apos = 0;
        while (ai < NUM_SERVOS && apos < (int)inner.length()) {
          int cm = inner.indexOf(',', apos);
          if (cm < 0) cm = inner.length();
          presets[slot].steps[stepCount].angles[ai++] = inner.substring(apos, cm).toInt();
          apos = cm + 1;
        }
        while (ai < NUM_SERVOS) presets[slot].steps[stepCount].angles[ai++] = 90;
      }
    }
    presets[slot].steps[stepCount].speed  = extractInt(stepStr, "speed",  60);
    presets[slot].steps[stepCount].holdMs = extractInt(stepStr, "holdMs", 500);
    stepCount++;
    pos = p2;
  }
  presets[slot].stepCount = max(1, stepCount);
  savePresets();
  sendJson("{\"id\":" + String(slot) + ",\"stepCount\":" + String(presets[slot].stepCount) + "}");
}

void handleDeletePreset() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "Missing id"); return; }
  int id = server.arg("id").toInt();
  if (id < 0 || id >= MAX_PRESETS) { server.send(400, "text/plain", "Bad id"); return; }
  presets[id].used = false;
  presets[id].stepCount = 0;
  savePresets();
  sendJson("{\"ok\":true}");
}

void handleRunPreset() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "Missing id"); return; }
  int id = server.arg("id").toInt();
  if (id < 0 || id >= MAX_PRESETS || !presets[id].used || presets[id].stepCount == 0) {
    server.send(404, "text/plain", "Preset not found or empty"); return;
  }
  presetRunId   = id;
  presetRunning = true;
  startPresetStep(id, 0);
  sendJson("{\"ok\":true,\"id\":" + String(id) + "}");
}

void handleStopPreset() {
  presetRunning = false;
  presetRunId   = -1;
  presetRunStep = -1;
  sendJson("{\"ok\":true}");
}

void handleRunStatus() {
  sendJson("{\"running\":" + String(presetRunning ? "true" : "false") +
           ",\"id\":" + String(presetRunId) +
           ",\"step\":" + String(presetRunStep) + "}");
}

void handleNotFound() { server.send(404, "text/plain", "Not Found"); }

// ══════════════════════════════════════════════════════════════════════════════
//  setup()
// ══════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  delay(10);

  loadServoConfig();
  loadCalibration();
  loadAngles();
  loadPresets();
  applyAllServos();
  lastUpdateTime = millis();

  WiFi.softAP(ssid, password);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("WiFi AP: %s / %s  IP: %s\n", ssid, password, ip.toString().c_str());

  // Original routes
  server.on("/",                  HTTP_GET,  handleRoot);
  server.on("/api/status",        HTTP_GET,  handleStatus);
  server.on("/api/set",           HTTP_GET,  handleSetServo);
  server.on("/api/offset",        HTTP_GET,  handleSetOffset);
  server.on("/api/reset",         HTTP_GET,  handleResetOffsets);
  server.on("/api/all",           HTTP_GET,  handleSetAllAngles);
  server.on("/api/config",        HTTP_GET,  handleGetConfig);
  server.on("/api/servoconfig",   HTTP_GET,  handleSetServoConfig);
  // Preset routes
  server.on("/api/presets",       HTTP_GET,  handleGetPresets);
  server.on("/api/preset",        HTTP_GET,  handleGetPreset);
  server.on("/api/savepreset",    HTTP_POST, handleSavePreset);
  server.on("/api/delpreset",     HTTP_GET,  handleDeletePreset);
  server.on("/api/runpreset",     HTTP_GET,  handleRunPreset);
  server.on("/api/stoppreset",    HTTP_GET,  handleStopPreset);
  server.on("/api/runstatus",     HTTP_GET,  handleRunStatus);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Web server started on port 80");
}

// ══════════════════════════════════════════════════════════════════════════════
//  loop()
// ══════════════════════════════════════════════════════════════════════════════
void loop() {
  server.handleClient();

  unsigned long now = millis();
  unsigned long dt  = now - lastUpdateTime;
  if (dt == 0) { delay(5); return; }
  lastUpdateTime = now;

  // ── Preset playback ────────────────────────────────────────────────────────
  if (presetRunning && presetRunId >= 0) {
    Preset &p = presets[presetRunId];

    if (presetMoving) {
      // Drive servos toward presetTargets using presetSpeedOverride per servo
      bool allDone = true;
      for (int i = 0; i < NUM_SERVOS; i++) {
        int target = presetTargets[i];
        int spd    = presetSpeedOverride[i];
        if (spd <= 0) {
          currentPhysicalAngles[i] = target;
        } else {
          float step = (spd * dt) / 1000.0f;
          if (currentPhysicalAngles[i] < target) {
            currentPhysicalAngles[i] = min((float)target, currentPhysicalAngles[i] + step);
          } else {
            currentPhysicalAngles[i] = max((float)target, currentPhysicalAngles[i] - step);
          }
        }
        pwm.setPWM(i, 0, angleToPulse(i, currentPhysicalAngles[i]));
        if (abs(currentPhysicalAngles[i] - target) > 0.5f) allDone = false;
      }
      if (allDone) {
        presetMoving  = false;
        presetHoldEnd = now + p.steps[presetRunStep].holdMs;
        // Sync logical angles
        for (int i = 0; i < NUM_SERVOS; i++)
          currentAngles[i] = presetTargets[i];
      }
    } else {
      // Holding — wait for hold time then advance to next step
      if (now >= presetHoldEnd) {
        int next = presetRunStep + 1;
        if (next < p.stepCount) {
          startPresetStep(presetRunId, next);
        } else {
          presetRunning = false;
          presetRunId   = -1;
          presetRunStep = -1;
        }
      }
    }
    delay(10);
    return; // skip normal servo update while running preset
  }

  // ── Normal servo update ────────────────────────────────────────────────────
  for (int i = 0; i < NUM_SERVOS; i++) {
    int target = constrain(currentAngles[i] + calibrationOffsets[i], 0, servoMaxAngle[i]);
    if (servoSpeed[i] <= 0) {
      if (currentPhysicalAngles[i] != target) {
        currentPhysicalAngles[i] = target;
        pwm.setPWM(i, 0, angleToPulse(i, currentPhysicalAngles[i]));
      }
    } else {
      if (abs(currentPhysicalAngles[i] - target) > 0.1f) {
        float step = (servoSpeed[i] * dt) / 1000.0f;
        currentPhysicalAngles[i] += (currentPhysicalAngles[i] < target ? step : -step);
        if ((step > 0 && currentPhysicalAngles[i] > target) ||
            (step < 0 && currentPhysicalAngles[i] < target))
          currentPhysicalAngles[i] = target;
        pwm.setPWM(i, 0, angleToPulse(i, currentPhysicalAngles[i]));
      } else if (currentPhysicalAngles[i] != target) {
        currentPhysicalAngles[i] = target;
        pwm.setPWM(i, 0, angleToPulse(i, currentPhysicalAngles[i]));
      }
    }
  }
  delay(10);
}
