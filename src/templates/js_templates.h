#ifndef JS_TEMPLATES_H
#define JS_TEMPLATES_H

#include <Arduino.h>

// =============================================================================
// SPA 页面 JavaScript
// =============================================================================
const char SPA_PAGE_JS[] PROGMEM = R"rawliteral(
var ws,rc=0;
// === 保存初始 WiFi 配置用于变更检测 ===
var initialWifiConfig = {ssid: '', pass: '', ipMode: 'dhcp', staticIp: '', staticGateway: '', staticSubnet: '', staticDns: ''};
document.addEventListener('DOMContentLoaded', function(){
    // 记录初始 WiFi 配置
    initialWifiConfig = {
        ssid: $('ws').value,
        pass: $('wp').value,
        ipMode: $('ipMode').value,
        staticIp: $('sip').value,
        staticGateway: $('sgw').value,
        staticSubnet: $('ssn').value,
        staticDns: $('sdns').value
    };
});

// === 面板切换 ===
function show(n,el){
document.querySelectorAll('.pnl').forEach(function(p){p.classList.remove('active')});
document.querySelectorAll('.side .si').forEach(function(s){s.classList.remove('active')});
document.getElementById('p-'+n).classList.add('active');
if(el)el.classList.add('active');
if(n==='config'){var first=document.getElementById('p-cfg-system');if(first)first.classList.add('active');}
}
function showCfg(n,el){
document.querySelectorAll('#p-config .pnl').forEach(function(p){p.classList.remove('active')});
document.querySelectorAll('#p-config .si').forEach(function(s){s.classList.remove('active')});
document.getElementById('p-cfg-'+n).classList.add('active');
if(el)el.classList.add('active');
}
// === WebSocket ===
function conn(){
ws=new WebSocket('ws://'+location.host+'/ws');
ws.onopen=function(){document.getElementById('wsSt').className='ws-st ok';document.getElementById('wsSt').textContent='已连接';rc=0};
ws.onclose=function(){document.getElementById('wsSt').className='ws-st fail';document.getElementById('wsSt').textContent='断开';setTimeout(conn,Math.min(1e3*Math.pow(2,rc++),3e4))};
ws.onmessage=function(e){upd(JSON.parse(e.data))};
}
function badge(c,t){return'<span class="badge '+c+'">'+t+'</span>'}
function renderCells(cs,max,min){var h='';for(var i=0;i<5;i++){var v=cs[i]||0;h+='<div class="cell"><div class="cn">C'+(i+1)+'</div><div class="cv" style="color:'+(v===max?'#52c41a':v===min?'#f5222d':'#333')+'">'+v+'</div></div>'}return h}
function setStat(id,val,c){var e=$(id);e.textContent=val+' mV';e.className='stat-value'+c}
// === 数据刷新 ===
var $=function(id){return document.getElementById(id)}
function upd(d){
if(d.status==='config_mode'){$('hSt').textContent='配置模式';return}
var b=d.bms||{},p=d.power||{},s=d.system||{};
$('uptime').textContent='运行：'+fmtUp(s.uptime||0);
$('wifi').textContent=s.wifi_ssid||'--';
$('rssi').textContent=(s.wifi_rssi||0)+' dBm';
var sc=b.soc||0;
$('soc').innerHTML=sc.toFixed(1)+'<span class="bigu">%</span>';
var bar=$('socBar');bar.style.width=sc+'%';bar.textContent=sc.toFixed(1)+'%';bar.style.background=sc>20?'#52c41a':sc>10?'#faad14':'#f5222d';
$('battV').textContent=(b.voltage||0)+' mV';
var ci=b.current||0;$('battI').textContent=ci+' mA';$('battI').className='vl'+(ci>0?' g':ci<0?' w':'');
$('battT').textContent=(b.temperature||0).toFixed(1)+' °C';$('soh').textContent=(b.soh||0).toFixed(1)+' %';
$('cycles').textContent=b.cycle_count||0;$('capR').textContent=(b.capacity_remaining||0)+' mAh';
var cs=b.cell_voltages||[];$('cells').innerHTML=renderCells(cs,b.cell_voltage_max||0,b.cell_voltage_min||0);
$('cellMM').textContent=(b.cell_voltage_max||0)+'/'+(b.cell_voltage_min||0)+' mV';
$('cellD').textContent=((b.cell_voltage_max||0)-(b.cell_voltage_min||0))+' mV';
$('balSt').innerHTML=b.balancing_active?badge('g','均衡中'):badge('b','未激活');
$('acSt').innerHTML=p.ac_present?badge('g','在线'):badge('r','离线');
$('inV').textContent=(p.input_voltage||0)+' mV';$('inI').textContent=(p.input_current||0)+' mA';
$('outP').textContent=(p.output_power||0)+' W';
$('pmBattV').textContent=(p.battery_voltage||0)+' mV';$('pmBattI').textContent=(p.battery_current||0)+' mA';
$('chgSt').innerHTML=p.charger_enabled?badge('g','充电中'):badge('b','未充电');
$('pwrMd').innerHTML=badge('b',['AC','电池','混合','充电'][d.power_mode]||'未知');
$('brdT').textContent=(s.board_temperature||0).toFixed(1)+' °C';$('envT').textContent=(s.environment_temperature||0).toFixed(1)+' °C';
// BMS panel
var bsc=b.soc||0,bb=$('b_soc_bar');$('b_soc').textContent=bsc.toFixed(1)+' %';bb.style.width=bsc+'%';bb.textContent=bsc.toFixed(1)+'%';bb.style.background=bsc>20?'#52c41a':bsc>10?'#faad14':'#f5222d';
$('b_soh').textContent=(b.soh||0).toFixed(1)+' %';$('b_v').textContent=(b.voltage||0)+' mV';
var bci=b.current||0;$('b_i').textContent=bci+' mA';$('b_i').className='vl'+(bci>0?' g':bci<0?' w':'');
$('b_t').textContent=(b.temperature||0).toFixed(1)+' °C';$('b_cyc').textContent=b.cycle_count||0;$('b_cap').textContent=(b.capacity_remaining||0)+' mAh';
$('b_bal').innerHTML=b.balancing_active?badge('g','均衡中'):badge('b','未激活');$('b_fault').textContent=b.fault_type||0;$('b_mode').textContent=b.bms_mode||0;
$('b_cells').innerHTML=renderCells(cs,b.cell_voltage_max||0,b.cell_voltage_min||0);
setStat('b_max',b.cell_voltage_max||0,' g');setStat('b_min',b.cell_voltage_min||0,' r');setStat('b_avg',b.cell_voltage_avg||0,' b');
setStat('b_dlt',(b.cell_voltage_max||0)-(b.cell_voltage_min||0),' w');
// Power panel
$('p_ac').innerHTML=p.ac_present?badge('g','在线'):badge('r','离线');$('p_iv').textContent=(p.input_voltage||0)+' mV';$('p_ii').textContent=(p.input_current||0)+' mA';
$('p_ip').textContent=((p.input_voltage||0)*(p.input_current||0)/1e6).toFixed(2)+' W';$('p_op').textContent=(p.output_power||0)+' W';
$('p_bv').textContent=(p.battery_voltage||0)+' mV';$('p_bi').textContent=(p.battery_current||0)+' mA';
$('p_ce').innerHTML=p.charger_enabled?badge('g','是'):badge('b','否');$('p_hy').innerHTML=p.hybrid_mode?badge('g','是'):badge('b','否');
$('p_ft').textContent=p.fault_type||0;$('p_ph').innerHTML=!p.prochot_status?badge('r','触发'):badge('g','正常');$('p_tb').innerHTML=!p.tbstat_status?badge('w','触发'):badge('g','正常');
$('updT').textContent='更新：'+new Date().toLocaleTimeString();
// Regs
var r2=p.bq24780s_registers||[],r7=b.bq76920_registers||[];
var a2=['0x12','0x3B','0x38','0x37','0x3C','0x3D','0x3A','0x14','0x15','0x39','0x3F'];
var n2=['CHARGE_OPTION0','CHARGE_OPTION1','CHARGE_OPTION2','CHARGE_OPTION3','PROCHOT_OPTION0','PROCHOT_OPTION1','PROCHOT_STATUS','CHARGE_CURRENT','CHARGE_VOLTAGE','DISCHARGE_CURRENT','INPUT_CURRENT'];
var h2='';for(var i=0;i<11;i++){var v=r2[i]||0;h2+='<div class="card"><div class="card-t" style="font-size:13px">'+a2[i]+' '+n2[i]+'</div><div style="color:#1677ff;font-weight:700;font-size:16px">0x'+v.toString(16).toUpperCase().padStart(4,'0')+'</div>'+pB2(i,v)+'</div>'}
$('r24').innerHTML=h2;
var a7=['0x00','0x01','0x04','0x05','0x06','0x07','0x08','0x09','0x0A','0x0B','0x50','0x51'];
var n7=['SYS_STAT','CELLBAL1','SYS_CTRL1','SYS_CTRL2','PROTECT1','PROTECT2','PROTECT3','OV_TRIP','UV_TRIP','CC_CFG','GAIN_uV','OFFSET_mV'];
var h7='';for(var i=0;i<12;i++){var v=r7[i]||0,bt='',hx='0x'+v.toString(16).toUpperCase().padStart(2,'0');for(var j=7;j>=0;j--)bt+=(v>>j&1);h7+='<div class="card"><div class="card-t" style="font-size:13px">'+a7[i]+' '+n7[i]+'</div><div style="color:#1677ff;font-weight:700;font-size:16px">'+hx+' <span style="color:#bbb;font-size:10px;font-family:monospace">'+bt+'</span></div>'+pB7(i,v)+'</div>'}
$('r76').innerHTML=h7;
}
function pB2(i,v){
var r='',st='<div style="font-size:11px;',sb=st+'color:#888">',sc=st+'color:#52c41a;font-weight:600">';
switch(i){
case 0:r+=sc+'模式：'+(v>>15&1?'低功耗':'性能')+'</div>'+sb+'看门狗：'+['禁用','5s','88s','175s'][(v>>13)&3]+'</div>'+sb+'PWM: '+['600kHz','800kHz','1MHz','-'][(v>>8)&3]+'</div>'+sb+'LEARN: '+(v>>5&1?'使能':'禁用')+'</div>'+sb+'IADP 增益：'+(v>>4&1?'40x':'20x')+'</div>'+sb+'IDCHG 增益：'+(v>>3&1?'16x':'8x')+'</div>'+st+'color:#888">充电：'+(v&1?'抑制':'使能')+'</div>';break;
case 1:r+=sb+'欠压阈值：'+['59.19%','62.65%','66.55%','70.97%'][(v>>14)&3]+'</div>'+sb+'IDCHG: '+(v>>11&1?'使能':'禁用')+'</div>'+sb+'PMON: '+(v>>10&1?'使能':'禁用')+'</div>';break;
case 2:r+=sb+'外部 ILIM: '+(v>>7&1?'使能':'禁用')+'</div>';break;
case 3:r+=sb+'放电调节：'+(v>>15&1?'使能':'禁用')+'</div>'+sb+'ACOK 去抖：'+(v>>12&1?'1.3s':'150ms')+'</div>'+sb+'AC 存在：'+(v>>11&1?'是':'否')+'</div>';break;
case 4:var ic=(v>>11)&31;r+=sb+'ICRIT: '+(ic>26?'溢出':(110+ic*5)+'%')+'</div>';break;
case 5:r+=sb+'IDCHG 阈值：'+((v>>10)&63)*512+'mA</div>';break;
case 6:var nm=['ACOK','BATPRES','VSYS','IDCHG','INOM','ICRIT','CMP'];r+=st+'color:#888">PROCHOT:</div>';for(var j=0;j<7;j++)r+=st+'color:'+(v>>j&1?'#f5222d':'#52c41a')+'">'+nm[j]+': '+(v>>j&1?'触发':'正常')+'</div>';break;
case 7:r+=sc+'充电电流：'+((v>>6)&127)*64+'mA</div>';break;
case 8:r+=sc+'充电电压：'+((v>>4)&1023)*16+'mV</div>';break;
case 9:r+=sc+'放电电流：'+((v>>9)&63)*512+'mA</div>';break;
case 10:r+=sc+'输入电流：'+((v>>7)&63)*128+'mA</div>';break;
}return r;}
function pB7(i,v){
var r='',st='<div style="font-size:11px;margin-top:4px;',sb=st+'color:#888">',sg=st+'color:#52c41a">',sr=st+'color:#f5222d;font-weight:600">',sy=st+'color:#d48806;font-weight:600">',sw=st+'color:#d48806">';
switch(i){
case 0:r+=sg+'CC 就绪：'+(v>>7&1?'新数据':'无')+'</div>'+st+'color:#888">芯片故障：'+(v>>5&1?'错误':'正常')+'</div>'+st+'color:#888">UV: '+(v>>3&1?'触发':'正常')+'</div>'+st+'color:#888">OV: '+(v>>2&1?'触发':'正常')+'</div>'+st+'color:#888">SCD: '+(v>>1&1?'触发':'正常')+'</div>'+st+'color:#888">OCD: '+(v&1?'触发':'正常')+'</div>';break;
case 1:r+=st+'color:#888">均衡状态:</div>';for(var j=0;j<5;j++)r+=sw+'Cell'+(j+1)+': '+(((v>>j)&1)?'均衡中':'关闭')+'</div>';break;
case 2:r+=st+'color:#888">负载检测：'+(v>>7&1?'有':'无')+'</div>'+sb+'ADC: '+(v>>4&1?'使能':'禁用')+'</div>';break;
case 3:r+=sg+'放电 MOS: '+((v>>1)&1?'开启':'关闭')+'</div>'+sg+'充电 MOS: '+(v&1?'开启':'关闭')+'</div>';break;
case 4:var rs=(v>>7)&1,sd=['70us','100us','200us','400us'],sc=v&7;r+=sg+'量程：'+(rs?'高 (x2)':'低')+'</div>'+sb+'短路延时：'+sd[(v>>3)&3]+'</div>'+sb+'短路阈值：~'+(rs?(44+sc*22):(22+sc*11))+'mV</div>';break;
case 5:r+=sb+'过流延时：~'+(8<<((v>>4)&7))+'ms</div>'+sb+'过流阈值：~'+(8+((v&15)*2.8)).toFixed(1)+'mV</div>';break;
case 6:r+=sb+'欠压延时：'+[1,4,8,16][(v>>6)&3]+'s</div>'+sb+'过压延时：'+[1,2,4,8][(v>>4)&3]+'s</div>';break;
case 7:r+=sr+'OV 阈值：0x'+(0x2008|(v<<4)).toString(16).toUpperCase()+'</div>';break;
case 8:r+=sy+'UV 阈值：0x'+(0x1000|(v<<4)).toString(16).toUpperCase()+'</div>';break;
case 9:var cc=v&0x3F;r+=sb+'CC 配置：0x'+cc.toString(16).toUpperCase()+' '+(cc===0x19?'正确':'异常')+'</div>';break;
case 10:var g=v+365;r+=sb+'增益：'+g+' ('+(g/1000).toFixed(3)+' mV/LSB)</div>';break;
case 11:r+=sb+'偏移：'+v+' mV</div>';break;
}return r;}
function fmtUp(s){var m=Math.floor(s/60),h=Math.floor(m/60),d=Math.floor(h/24);if(d>0)return d+'天'+(h%24)+'时';if(h>0)return h+'时'+(m%60)+'分';if(m>0)return m+'分'+(s%60)+'秒';return s+'秒';}
// === 配置：时间窗口 ===
var cw=[],wc=0;
var days=[{v:1,l:'周日'},{v:2,l:'周一'},{v:4,l:'周二'},{v:8,l:'周三'},{v:16,l:'周四'},{v:32,l:'周五'},{v:64,l:'周六'}];
document.addEventListener('DOMContentLoaded',function(){initW(window.IW||[])});
function initW(w){cw=w;wc=cw.length;renW();updB()}
function addW(){if(cw.length>=5)return;cw.push({id:wc++,day_mask:0,start_hour:8,end_hour:20});renW();updB()}
function rmW(i){cw.splice(i,1);renW();updB()}
function recW(i){var m=0;$('wc').querySelectorAll('input[data-wi="'+i+'"]').forEach(function(c){if(c.checked)m|=+c.getAttribute('data-db')});cw[i].day_mask=m;renW()}
function renW(){
var c=$('wc');c.innerHTML='';
if(!cw.length){c.innerHTML='<p style="color:#bbb;text-align:center;padding:14px">暂无窗口</p>';return}
cw.forEach(function(w,i){
var m='',selS='',selE='';
for(var h=0;h<24;h++)selS+='<option value="'+h+'"'+(h===w.start_hour?' selected':'')+'>'+(h<10?'0':'')+h+':00</option>';
for(var h=0;h<24;h++)selE+='<option value="'+h+'"'+(h===w.end_hour?' selected':'')+'>'+(h<10?'0':'')+h+':00</option>';
days.forEach(function(d){var ck=(w.day_mask&d.v)?'checked':'';m+='<label style="display:inline-block;margin:3px"><input type="checkbox" data-wi="'+i+'" data-db="'+d.v+'" '+ck+' onchange="recW('+i+')"> '+d.l+'</label>';});
c.innerHTML+='<div style="border:1px solid #e8e8e8;border-radius:6px;padding:10px;margin-bottom:10px;background:#fafafa"><div style="font-weight:600;margin-bottom:6px;color:#333;font-size:12px">窗口 #'+(i+1)+'</div><div style="margin-bottom:6px"><strong style="color:#888;font-size:11px">星期:</strong><br>'+m+'</div><div style="display:flex;align-items:center;gap:10px;margin-bottom:6px"><div style="font-size:12px"><strong style="color:#888">开始:</strong><select onchange="cw['+i+'].start_hour=parseInt(this.value)">'+selS+'</select></div><div style="font-size:12px"><strong style="color:#888">结束:</strong><select onchange="cw['+i+'].end_hour=parseInt(this.value)">'+selE+'</select></div><button type="button" class="btn btn-r" style="padding:4px 10px;font-size:11px" onclick="rmW('+i+')">删除</button></div><div style="font-size:10px;color:#bbb">掩码：0x'+w.day_mask.toString(16).toUpperCase().padStart(2,'0')+'</div></div>';
});
}
function updB(){var b=$('ab');if(cw.length>=5){b.disabled=true;b.textContent='已满 (5/5)';b.style.opacity='.5'}else{b.disabled=false;b.textContent='添加窗口 ('+cw.length+'/5)';b.style.opacity='1'}}
function toggleIP(){$('sIP').style.display=$('ipMode').value==='static'?'block':'none'}
// === 配置：保存 ===
function gVal(id){return $(id).value}
function gChk(id){return $(id).checked}

// 检测 WiFi 配置是否变更
function isWifiConfigChanged(){
    return gVal('ws') !== initialWifiConfig.ssid ||
           gVal('wp') !== initialWifiConfig.pass ||
           $('ipMode').value !== initialWifiConfig.ipMode ||
           gVal('sip') !== initialWifiConfig.staticIp ||
           gVal('sgw') !== initialWifiConfig.staticGateway ||
           gVal('ssn') !== initialWifiConfig.staticSubnet ||
           gVal('sdns') !== initialWifiConfig.staticDns;
}

function save(){
var d={
system:{wifi_ssid:gVal('ws'),wifi_pass:gVal('wp'),use_static_ip:$('ipMode').value==='static',static_ip:gVal('sip'),static_gateway:gVal('sgw'),static_subnet:gVal('ssn'),static_dns:gVal('sdns'),ntp_server:gVal('ntp'),buzzer_enabled:gChk('be'),volume_level:+gVal('vl'),led_brightness:+gVal('lb'),hid_enabled:gChk('hid_en'),hid_report_mode:+gVal('hid_mode'),mqtt_enabled:gChk('mqtt_en'),mqtt_broker:gVal('mqtt_brk'),mqtt_port:+gVal('mqtt_port'),mqtt_username:gVal('mqtt_usr'),mqtt_password:gVal('mqtt_pwd')},
bms:{cell_count:+gVal('bc'),nominal_capacity_mAh:+gVal('bn'),cell_ov_threshold:+gVal('bo'),cell_uv_threshold:+gVal('bu'),cell_ov_recover:+gVal('bor'),cell_uv_recover:+gVal('bur'),max_charge_current:+gVal('bmc'),max_discharge_current:+gVal('bmd'),short_circuit_threshold:+gVal('bsc'),temp_overheat_threshold:+gVal('both'),balancing_enabled:gChk('bbe'),balancing_voltage_diff:+gVal('bbd')},
power:{max_charge_current:+gVal('pmc'),charge_voltage_limit:+gVal('pcv'),charge_soc_start:+gVal('pcs'),charge_soc_stop:+gVal('pcp'),max_discharge_current:+gVal('pmd'),discharge_soc_stop:+gVal('pds'),enable_hybrid_boost:gChk('phe'),over_current_threshold:+gVal('poc'),over_temp_threshold:+gVal('pot'),charge_temp_high_limit:+gVal('pth'),charge_temp_low_limit:+gVal('ptl'),charging_windows:cw,charging_window_count:cw.length}};

// 检测 WiFi 配置是否变更
var wifiChanged = isWifiConfigChanged();

if(wifiChanged){
    // WiFi 配置变更：立即提示，不等待响应
    alert('网络变动，保存成功。设备即将重启...');
    // 发送请求但不等待响应（因为网络会断开）
    fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).catch(function(){});
    // 延迟跳转，给服务器时间保存配置
    setTimeout(function(){location.href='/'},2000);
} else {
    // 非 WiFi 配置变更：正常等待响应
    fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)}).then(function(r){return r.json()}).then(function(r){if(r.success){alert('保存成功'+(r.restart_required?'，设备即将重启':''));if(r.restart_required)setTimeout(function(){location.href='/'},2e3)}else{alert('保存失败：'+(r.message||'未知'))}}).catch(function(){alert('网络错误')});
}
}
// === OTA: 固件上传 ===
var sel=null,area=$('upArea');
area.addEventListener('dragover',function(e){e.preventDefault();this.classList.add('drag')});
area.addEventListener('dragleave',function(e){e.preventDefault();this.classList.remove('drag')});
area.addEventListener('drop',function(e){e.preventDefault();this.classList.remove('drag');if(e.dataTransfer.files.length)handle(e.dataTransfer.files[0])});
function selFile(inp){if(inp.files.length)handle(inp.files[0])}
function handle(f){if(!f.name.endsWith('.bin')){showOtaMsg('请选择 .bin 文件','er');return}sel=f;$('fName').textContent=f.name+' ('+(f.size/1024).toFixed(1)+' KB)';$('upBtn').disabled=false;showOtaMsg('就绪，点击升级','nf')}
function showOtaMsg(m,t){var s=$('stMsg');s.textContent=m;s.className='oms '+t}
function upload(){
if(!sel)return;var fd=new FormData();fd.append('firmware',sel,sel.name);var xhr=new XMLHttpRequest();
xhr.open('POST','/firmware',true);
xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);$('prgF').style.width=p+'%';$('prgF').textContent=p+'%'}};
xhr.onloadstart=function(){$('prgC').style.display='block';$('upBtn').disabled=true;$('upBtn').textContent='升级中...';showOtaMsg('上传中...','nf')};
xhr.onload=function(){if(xhr.status===200){showOtaMsg('升级成功，重启中...','ok');setTimeout(function(){location.href='/'},3e3)}else{showOtaMsg('失败：HTTP '+xhr.status,'er');$('upBtn').disabled=false;$('upBtn').textContent='重新升级'}};
xhr.onerror=function(){showOtaMsg('网络错误','er');$('upBtn').disabled=false;$('upBtn').textContent='重新升级'};
xhr.send(fd);}

// === 配置向导 (手机适配) ===
var wzCurStep=0,wzTotalSteps=6;
var wzData={wifi:{ssid:'',pass:''},network:{mode:'dhcp',ip:'',gateway:'',subnet:'',dns:'',ntp:'ntp.aliyun.com'},battery:{cells:4,capacity:2000,chargeCur:1000,dischargeCur:2000,ovThresh:4200,uvThresh:3000,balancing:true},hardware:{buzzer:true,volume:70,brightness:80,hid:true,hidMode:0},mqtt:{enabled:false,broker:'',port:1883,user:'',pass:''}};

function wzShowStep(n){
wzCurStep=n;
document.querySelectorAll('.wz-step').forEach(function(s,i){s.classList.remove('active','completed');if(i<n)s.classList.add('completed');if(i===n)s.classList.add('active')});
document.querySelectorAll('.wz-card').forEach(function(c,i){c.style.display=i===n?'block':'none'});
$('wz-prev').disabled=n===0;
var btn=$('wz-next');if(n===wzTotalSteps-1){btn.textContent='保存并重启';btn.className='wz-btn wz-btn-save'}else{btn.textContent='下一步';btn.className='wz-btn wz-btn-next'}
}
function wzPrev(){if(wzCurStep>0)wzShowStep(wzCurStep-1);}
function wzNext(){if(wzCurStep<wzTotalSteps-1){if(!wzValidateStep(wzCurStep))return;wzSaveStep(wzCurStep);wzShowStep(wzCurStep+1)}else{wzSaveAll()}}
function wzSaveStep(n){if(n===1)wzToggleIP();}
function wzGetVal(id){return $(id).value}
function wzValidateStep(n){
if(n===0){var ss=$('wz-wifi-ssid').value.trim();if(!ss){alert('请输入 WiFi 名称');return false}wzData.wifi={ssid:ss,pass:wzGetVal('wz-wifi-pass')}}
if(n===1){wzData.network.mode=wzGetVal('wz-ip-mode');wzData.network.ntp=wzGetVal('wz-ntp-server');if(wzData.network.mode==='static'){var ip=wzGetVal('wz-static-ip-addr');if(!ip){alert('请输入静态 IP');return false}wzData.network={ip:ip,gateway:wzGetVal('wz-static-gateway'),subnet:wzGetVal('wz-static-subnet'),dns:wzGetVal('wz-static-dns'),ntp:wzData.network.ntp}}}
if(n===2){var cap=+$('wz-capacity').value;if(!cap||cap<100){alert('请输入有效的电池容量');return false}var ov=+$('wz-ov-threshold').value||4200,uv=+$('wz-uv-threshold').value||3000;if(ov<4000||ov>4500){alert('过压阈值必须在 4000-4500mV 之间');return false}if(uv<2500||uv>3500){alert('欠压阈值必须在 2500-3500mV 之间');return false}wzData.battery={cells:+$('wz-cell-count').value,capacity:cap,chargeCur:+$('wz-charge-current').value||1000,dischargeCur:+$('wz-discharge-current').value||2000,ovThresh:ov,uvThresh:uv,balancing:$('wz-balancing').checked}}
if(n===3){wzData.hardware={buzzer:$('wz-buzzer').checked,volume:+$('wz-volume').value,brightness:+$('wz-brightness').value,hid:$('wz-hid').checked,hidMode:+$('wz-hid-mode').value}}
if(n===4){var mqttEn=$('wz-mqtt-en').checked;wzData.mqtt={enabled:mqttEn};if(mqttEn){var broker=$('wz-mqtt-broker').value.trim();if(!broker){alert('请输入 MQTT Broker 地址');return false}wzData.mqtt={enabled:true,broker:broker,port:+$('wz-mqtt-port').value||1883,user:$('wz-mqtt-user').value,pass:$('wz-mqtt-pass').value}}}
return true;
}
function wzToggleIP(){var d=$('wz-static-ip');d.style.display=$('wz-ip-mode').value==='static'?'block':'none'}
function wzSaveAll(){
var doc={
system:{wifi_ssid:wzData.wifi.ssid,wifi_pass:wzData.wifi.pass,use_static_ip:wzData.network.mode==='static',static_ip:wzData.network.ip||'',static_gateway:wzData.network.gateway||'',static_subnet:wzData.network.subnet||'',static_dns:wzData.network.dns||'',ntp_server:wzData.network.ntp,buzzer_enabled:wzData.hardware.buzzer,volume_level:wzData.hardware.volume,led_brightness:wzData.hardware.brightness,hid_enabled:wzData.hardware.hid,hid_report_mode:wzData.hardware.hidMode,mqtt_enabled:wzData.mqtt.enabled,mqtt_broker:wzData.mqtt.broker||'',mqtt_port:wzData.mqtt.port||1883,mqtt_username:wzData.mqtt.user||'',mqtt_password:wzData.mqtt.pass||''},
bms:{cell_count:wzData.battery.cells,nominal_capacity_mAh:wzData.battery.capacity,max_charge_current:wzData.battery.chargeCur,max_discharge_current:wzData.battery.dischargeCur,cell_ov_threshold:wzData.battery.ovThresh,cell_uv_threshold:wzData.battery.uvThresh,cell_ov_recover:wzData.battery.ovThresh-100,cell_uv_recover:wzData.battery.uvThresh+100,short_circuit_threshold:30000,temp_overheat_threshold:65,balancing_enabled:wzData.battery.balancing,balancing_voltage_diff:20},
power:{max_charge_current:wzData.battery.chargeCur,charge_voltage_limit:wzData.battery.cells==3?12600:wzData.battery.cells==4?16800:21000,charge_soc_start:20,charge_soc_stop:90,max_discharge_current:wzData.battery.dischargeCur,discharge_soc_stop:10,enable_hybrid_boost:false,over_current_threshold:20000,over_temp_threshold:60,charge_temp_high_limit:55,charge_temp_low_limit:5,charging_windows:[],charging_window_count:0}};
var r=new XMLHttpRequest();r.open('POST','/save',true);r.setRequestHeader('Content-Type','application/json');r.timeout=15000;
r.onload=function(){try{var j=JSON.parse(r.responseText);if(j.success){$('wz-nav').style.display='none';startRebootCountdown()}else{alert('保存失败：'+(j.message||'未知'))}}catch(e){startRebootCountdown()}};
r.onerror=r.ontimeout=function(){startRebootCountdown()};
r.send(JSON.stringify(doc));
}
function startRebootCountdown(){var c=5,el=$('wz-reboot-count');var t=setInterval(function(){c--;el.textContent=c;if(c<=0){clearInterval(t);el.textContent='重启中...';setTimeout(function(){location.href='/'},3e3)}},1e3)}

// === 运输模式 ===
function enterShipMode(){
    if(!confirm('一旦进入此模式，需要点按对应的硬件开关才能启用电池\n\n确定要进入运输模式吗？'))return;
    var xhr=new XMLHttpRequest();
    xhr.open('POST','/bms/shipmode',true);
    xhr.setRequestHeader('Content-Type','application/json');
    xhr.timeout=5000;
    xhr.onload=function(){
        if(xhr.status===200){
            try{
                var j=JSON.parse(xhr.responseText);
                if(j.success){
                    alert('已进入运输模式！\n\nBMS 芯片已进入睡眠模式，需要点按硬件开关才能重新启用电池。');
                }else{
                    alert('操作失败：'+(j.message||'未知错误'));
                }
            }catch(e){
                alert('已进入运输模式');
            }
        }else{
            alert('请求失败：HTTP '+xhr.status);
        }
    };
    xhr.onerror=function(){alert('网络错误，请检查连接')};
    xhr.ontimeout=function(){alert('请求超时')};
    xhr.send('');
}

// === ADC 校准系数 ===
var calPins=[
{name:'BQ24780S_IADP',desc:'充电电流检测',pin:1},
{name:'BQ24780S_IDCHG',desc:'放电电流检测',pin:2},
{name:'BQ24780S_PMON',desc:'系统功率监测',pin:9},
{name:'INPUT_VOLTAGE',desc:'输入电压',pin:4},
{name:'BATTERY_VOLTAGE',desc:'电池电压',pin:5},
{name:'BOARD_TEMP',desc:'主板温度 (NTC)',pin:7},
{name:'ENVIRONMENT_TEMP',desc:'环境温度 (NTC)',pin:8}
];
var calData=null;
function loadCalibration(){
fetch('/api/calibration').then(function(r){return r.json()}).then(function(d){
if(d.success){calData=d.calibration;renderCalibration()}
}).catch(function(){$('calStatus').textContent='加载失败'});
}
function renderCalibration(){
var c=$('calContainer');c.innerHTML='';
calPins.forEach(function(p,i){
var v=calData[i]||100;
c.innerHTML+='<div style="border:1px solid #e8e8e8;border-radius:6px;padding:12px"><div style="font-weight:600;font-size:13px;margin-bottom:4px">'+p.desc+'</div><div style="font-size:11px;color:#888;margin-bottom:8px">'+p.name+' (GPIO'+p.pin+')</div><div style="display:flex;align-items:center;gap:8px"><input type="range" min="50" max="255" value="'+v+'" id="cal_'+i+'" style="flex:1" oninput="document.getElementById(\'calv_'+i+'\').textContent=(this.value/100).toFixed(2)+\'x\'"><span id="calv_'+i+'" style="min-width:50px;font-weight:600;color:#1677ff">'+(v/100).toFixed(2)+'x</span></div></div>';
});
$('calStatus').textContent='';
}
function saveCalibration(){
var vals=[];
for(var i=0;i<calPins.length;i++){
var v=+$('cal_'+i).value;
if(v<50||v>255){alert('系数范围 50-255');return}
vals.push(v);
}
fetch('/api/calibration',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({calibration:vals})}).then(function(r){return r.json()}).then(function(d){
if(d.success){$('calStatus').style.color='#52c41a';$('calStatus').textContent='保存成功！';loadCalibration()}
else{$('calStatus').style.color='#f5222d';$('calStatus').textContent='保存失败：'+(d.message||'未知')}}).catch(function(){$('calStatus').textContent='网络错误'});
}
// 在切换到校面板时加载数据
var _origShow=show;
show=function(n,el){_origShow(n,el);if(n==='calibration')loadCalibration()};

// 检测配置模式 - 在 DOM 加载完成后执行
if(window.CONFIG_MODE===1){document.addEventListener('DOMContentLoaded',function(){var m=document.querySelector('.main-wrap'),w=$('wz-page'),f=document.querySelector('.foot'),t=document.querySelector('.topbar-info');if(m){m.style.display='none';}if(w){w.style.display='block';}if(f)f.style.display='none';if(t)t.style.display='none'})}

conn();
)rawliteral";

#endif // JS_TEMPLATES_H