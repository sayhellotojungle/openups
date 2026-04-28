#ifndef PAGE_TEMPLATES_H
#define PAGE_TEMPLATES_H

#include <Arduino.h>
#include "js_templates.h"

// =============================================================================
// SPA 单页模板 - HTML
// =============================================================================
const char SPA_PAGE_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>UPS 控制中心</title>
<style id="dynamic-css"></style>
</head>
<body>
<div class="topbar">
<div class="traffic-lights">
<span class="tl tl-close"></span>
<span class="tl tl-minimize"></span>
<span class="tl tl-maximize"></span>
</div>
<h1>UPS 控制中心</h1>
<div class="topbar-info">
<span class="ws-st" id="wsSt">连接中</span>
<span id="hSt">实时监控</span>
<span id="uptime">--</span>
<span id="wifi">--</span>
<span id="rssi">-- dBm</span>
</div>
</div>
<div class="main-wrap">
<div class="side">
<div class="si active" onclick="show('status',this)">状态概览</div>
<div class="si" onclick="show('bms',this)">BMS 状态</div>
<div class="si" onclick="show('power',this)">电源状态</div>
<div class="si" onclick="show('config',this)">系统配置</div>
<div class="si" onclick="show('calibration',this)">配置校准系数</div>
<div class="si" onclick="show('ota',this)">固件升级</div>
</div>
<div class="ct">
<!-- ===== 面板：状态概览 ===== -->
<div class="pnl active" id="p-status">
<div class="grid">
<div class="card"><div class="card-t">电池状态</div>
<div class="bigv" id="soc">--<span class="bigu">%</span></div>
<div class="pb"><div class="pf" id="socBar">0%</div></div>
<div class="row"><span class="lb">电压</span><span class="vl" id="battV">-- mV</span></div>
<div class="row"><span class="lb">电流</span><span class="vl" id="battI">-- mA</span></div>
<div class="row"><span class="lb">温度</span><span class="vl" id="battT">-- °C</span></div>
<div class="row"><span class="lb">健康度</span><span class="vl" id="soh">-- %</span></div>
<div class="row"><span class="lb">循环次数</span><span class="vl" id="cycles">--</span></div>
<div class="row"><span class="lb">剩余容量</span><span class="vl" id="capR">-- mAh</span></div>
</div>
<div class="card"><div class="card-t">单体电压</div>
<div class="cells" id="cells"></div>
<div class="row" style="margin-top:8px"><span class="lb">最高 / 最低</span><span class="vl" id="cellMM">--/-- mV</span></div>
<div class="row"><span class="lb">压差</span><span class="vl" id="cellD">-- mV</span></div>
<div class="row"><span class="lb">均衡状态</span><span class="vl" id="balSt">--</span></div>
</div>
<div class="card"><div class="card-t">电源状态</div>
<div class="row"><span class="lb">AC 电源</span><span class="vl" id="acSt">--</span></div>
<div class="row"><span class="lb">输入电压</span><span class="vl" id="inV">-- mV</span></div>
<div class="row"><span class="lb">输入电流</span><span class="vl" id="inI">-- mA</span></div>
<div class="row"><span class="lb">输出功率</span><span class="vl" id="outP">-- W</span></div>
<div class="row"><span class="lb">电池电压 (PM)</span><span class="vl" id="pmBattV">-- mV</span></div>
<div class="row"><span class="lb">放电电流 (PM)</span><span class="vl" id="pmBattI">-- mA</span></div>
<div class="row"><span class="lb">充电状态</span><span class="vl" id="chgSt">--</span></div>
</div>
<div class="card"><div class="card-t">系统状态</div>
<div class="row"><span class="lb">运行模式</span><span class="vl" id="pwrMd">--</span></div>
<div class="row"><span class="lb">板温</span><span class="vl" id="brdT">-- °C</span></div>
<div class="row"><span class="lb">环境温度</span><span class="vl" id="envT">-- °C</span></div>
</div>
</div>
</div>

<!-- ===== 面板：BMS 状态 (含 BQ76920 寄存器) ===== -->
<div class="pnl" id="p-bms">
<div class="grid">
<div class="card"><div class="card-t">电池状态</div>
<div class="bigv" id="b_soc">--<span class="bigu">%</span></div>
<div class="pb"><div class="pf" id="b_soc_bar">0%</div></div>
<div class="row"><span class="lb">健康度</span><span class="vl" id="b_soh">-- %</span></div>
<div class="row"><span class="lb">温度</span><span class="vl" id="b_t">-- °C</span></div>
</div>
<div class="card"><div class="card-t">电压 & 电流</div>
<div class="row"><span class="lb">总电压</span><span class="vl" id="b_v">-- mV</span></div>
<div class="row"><span class="lb">电流</span><span class="vl g" id="b_i">-- mA</span></div>
<div class="row"><span class="lb">循环次数</span><span class="vl" id="b_cyc">--</span></div>
<div class="row"><span class="lb">剩余容量</span><span class="vl" id="b_cap">-- mAh</span></div>
</div>
<div class="card"><div class="card-t">保护 & 均衡</div>
<div class="row"><span class="lb">均衡状态</span><span class="vl" id="b_bal">--</span></div>
<div class="row"><span class="lb">故障类型</span><span class="vl r" id="b_fault">--</span></div>
<div class="row"><span class="lb">BMS 模式</span><span class="vl" id="b_mode">--</span></div>
</div>
</div>
<div class="card" style="margin-top:16px"><div class="card-t">单体电压详情</div>
<div class="cells" id="b_cells"></div>
<div class="grid" style="grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;margin-top:12px">
<div class="stat-box"><div class="stat-label">最高</div><div class="stat-value" id="b_max">-- mV</div></div>
<div class="stat-box"><div class="stat-label">最低</div><div class="stat-value" id="b_min">-- mV</div></div>
<div class="stat-box"><div class="stat-label">平均</div><div class="stat-value" id="b_avg">-- mV</div></div>
<div class="stat-box"><div class="stat-label">压差</div><div class="stat-value" id="b_dlt">-- mV</div></div>
</div>
</div>
<div class="card-t" style="margin:20px 0 12px;font-size:14px;font-weight:600">BQ76920 寄存器状态</div>
<div id="r76" class="grid"></div>
</div>

<!-- ===== 面板：电源状态 (含 BQ24780S 寄存器) ===== -->
<div class="pnl" id="p-power">
<div class="grid">
<div class="card"><div class="card-t">输入电源</div>
<div class="row"><span class="lb">AC 状态</span><span class="vl" id="p_ac">--</span></div>
<div class="row"><span class="lb">输入电压</span><span class="vl" id="p_iv">-- mV</span></div>
<div class="row"><span class="lb">输入电流</span><span class="vl" id="p_ii">-- mA</span></div>
<div class="row"><span class="lb">输入功率</span><span class="vl" id="p_ip">-- W</span></div>
</div>
<div class="card"><div class="card-t">电池监测</div>
<div class="row"><span class="lb">电池电压</span><span class="vl" id="p_bv">-- mV</span></div>
<div class="row"><span class="lb">放电电流</span><span class="vl g" id="p_bi">-- mA</span></div>
<div class="row"><span class="lb">输出功率</span><span class="vl" id="p_op">-- W</span></div>
</div>
<div class="card"><div class="card-t">充电控制</div>
<div class="row"><span class="lb">充电使能</span><span class="vl" id="p_ce">--</span></div>
<div class="row"><span class="lb">混合模式</span><span class="vl" id="p_hy">--</span></div>
<div class="row"><span class="lb">故障类型</span><span class="vl r" id="p_ft">--</span></div>
</div>
<div class="card"><div class="card-t">芯片状态</div>
<div class="row"><span class="lb">PROCHOT</span><span class="vl" id="p_ph">--</span></div>
<div class="row"><span class="lb">TBSTAT</span><span class="vl" id="p_tb">--</span></div>
</div>
</div>
<div class="card-t" style="margin:20px 0 12px;font-size:14px;font-weight:600">BQ24780S 寄存器状态</div>
<div id="r24" class="grid"></div>
</div>

<!-- ===== 面板：系统配置 ===== -->
<div class="pnl" id="p-config">
<div style="display:flex;gap:0;margin-bottom:14px;background:#fff;border-radius:8px;border:1px solid #e8e8e8;overflow:hidden">
<div class="si active" onclick="showCfg('system',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">系统设置</div>
<div class="si" onclick="showCfg('hardware',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">硬件控制</div>
<div class="si" onclick="showCfg('bms',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">BMS 配置</div>
<div class="si" onclick="showCfg('windows',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">充电窗口</div>
<div class="si" onclick="showCfg('power',this)" style="border-left:none;border-bottom:3px solid transparent;padding:8px 14px">电源管理</div>
</div>

<div class="pnl active" id="p-cfg-system">
<fieldset class="fs">
<legend class="lg">WiFi 设置</legend>
<div class="fg"><label>WiFi 名称:</label><input type="text" id="ws" value="%WIFI_SSID%" required></div>
<div class="fg"><label>WiFi 密码:</label><input type="password" id="wp" value="%WIFI_PASS%"></div>
<div class="fg"><label>IP 获取方式:</label><select id="ipMode" onchange="toggleIP()"><option value="dhcp"%IP_MODE_DHCP%>动态获取 (DHCP)</option><option value="static"%IP_MODE_STATIC%>固定 IP</option></select></div>
</fieldset>
 <fieldset class="fs" id="sIP" style="display:%STATIC_IP_DISPLAY%">
<legend class="lg">静态 IP 配置</legend>
<div class="fg"><label>IP 地址:</label><input type="text" id="sip" value="%STATIC_IP%" placeholder="192.168.1.100"></div>
<div class="fg"><label>网关:</label><input type="text" id="sgw" value="%STATIC_GATEWAY%" placeholder="192.168.1.1"></div>
<div class="fg"><label>子网掩码:</label><input type="text" id="ssn" value="%STATIC_SUBNET%" placeholder="255.255.255.0"></div>
<div class="fg"><label>DNS 服务器:</label><input type="text" id="sdns" value="%STATIC_DNS%" placeholder="8.8.8.8"></div>
</fieldset>
<fieldset class="fs">
<legend class="lg">时间同步配置</legend>
<div class="fg"><label>NTP 服务器:</label><input type="text" id="ntp" value="%NTP_SERVER%" placeholder="ntp.aliyun.com"></div>
</fieldset>
<fieldset class="fs">
<legend class="lg">HID 配置</legend>
<div class="fg"><label>HID 服务:</label><label class="cl"><input type="checkbox" id="hid_en" %HID_CHECKED%><span style="margin-left:8px">启用</span></label></div>
<div class="fg"><label>电量模式:</label><select id="hid_mode"><option value="0"%HID_MODE_MAH%>毫安时 (mAh)</option><option value="1"%HID_MODE_MWH%>毫瓦时 (mWh)</option><option value="2"%HID_MODE_PCT%>百分比 (%)（linux 选这个）</option></select></div>
</fieldset>
<fieldset class="fs">
<legend class="lg">MQTT 配置</legend>
<div class="fg"><label>MQTT 服务:</label><label class="cl"><input type="checkbox" id="mqtt_en" %MQTT_CHECKED%><span style="margin-left:8px">启用</span></label></div>
<div class="fg"><label>Broker 地址:</label><input type="text" id="mqtt_brk" value="%MQTT_BROKER%" placeholder="192.168.1.100"></div>
<div class="fg"><label>端口:</label><input type="number" id="mqtt_port" value="%MQTT_PORT%" min="1" max="65535" placeholder="1883"><span class="u">端口</span></div>
<div class="fg"><label>用户名:</label><input type="text" id="mqtt_usr" value="%MQTT_USERNAME%" placeholder="可选"></div>
<div class="fg"><label>密码:</label><input type="text" id="mqtt_pwd" value="%MQTT_PASSWORD%" placeholder="可选"></div>
</fieldset>
<fieldset class="fs" style="border:2px solid #ff4d4f;border-radius:8px">
<legend class="lg" style="color:#ff4d4f">运输以及存储模式</legend>
<p style="color:#666;font-size:13px;margin:8px 0;line-height:1.6">此模式会让bq76920电池管理芯片进入运输模式，也就是睡眠模式，将不会响应任何指令，一旦进入此模式，需要点按对应的硬件开关才能启用电池，此模式推荐运输或者长时间不使用情况下再执行。</p>
<button type="button" class="btn" style="background:#ff4d4f;border-color:#ff4d4f" onclick="enterShipMode()">进入此模式</button>
</fieldset>
</div>

<div class="pnl" id="p-cfg-hardware">
<fieldset class="fs">
<legend class="lg">硬件控制</legend>
<div class="fg"><label>蜂鸣器:</label><label class="cl"><input type="checkbox" id="be" %BUZZER_CHECKED%><span style="margin-left:8px">启用</span></label></div>
<div class="fg"><label>音量:</label><input type="range" id="vl" min="0" max="100" value="%VOLUME_VALUE%" oninput="document.getElementById('vv').textContent=this.value+'%'"><span id="vv" style="color:#888;font-size:12px;min-width:36px">%VOLUME_LEVEL%</span></div>
<div class="fg"><label>LED 亮度:</label><input type="range" id="lb" min="0" max="100" value="%LIGHT_VALUE%" oninput="document.getElementById('lv').textContent=this.value+'%'"><span id="lv" style="color:#888;font-size:12px;min-width:36px">%LIGHT_BRIGHTNESS%</span></div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-bms">
<fieldset class="fs">
<legend class="lg">BMS 配置</legend>
<div class="ss"><div class="st">电池参数</div>
<div class="fg"><label>电池串数:</label><select id="bc"><option value="3"%BMS_CELL_COUNT_3%>3 串</option><option value="4"%BMS_CELL_COUNT_4%>4 串</option><option value="5"%BMS_CELL_COUNT_5%>5 串</option></select></div>
<div class="fg"><label>标称容量:</label><input type="number" id="bn" value="%BMS_NOMINAL_CAPACITY%" min="100" max="50000" step="100"><span class="u">mAh</span></div>
</div>
<div class="ss"><div class="st">电压保护</div>
<div class="fg"><label>过压阈值:</label><input type="number" id="bo" value="%BMS_CELL_OV%" min="4000" max="4500" step="10"><span class="u">mV</span></div>
<div class="fg"><label>欠压阈值:</label><input type="number" id="bu" value="%BMS_CELL_UV%" min="2500" max="3500" step="10"><span class="u">mV</span></div>
<div class="fg"><label>过压恢复:</label><input type="number" id="bor" value="%BMS_CELL_OV_RECOVER%" min="4000" max="4300" step="10"><span class="u">mV</span></div>
<div class="fg"><label>欠压恢复:</label><input type="number" id="bur" value="%BMS_CELL_UV_RECOVER%" min="2800" max="3300" step="10"><span class="u">mV</span></div>
</div>
<div class="ss"><div class="st">电流保护</div>
<div class="fg"><label>最大充电电流:</label><input type="number" id="bmc" value="%BMS_MAX_CHARGE%" min="100" max="10000" step="100"><span class="u">mA</span></div>
<div class="fg"><label>最大放电电流:</label><input type="number" id="bmd" value="%BMS_MAX_DISCHARGE%" min="100" max="20000" step="100"><span class="u">mA</span></div>
<div class="fg"><label>短路阈值:</label><input type="number" id="bsc" value="%BMS_SHORT_CIRCUIT%" min="1000" max="30000" step="500"><span class="u">mA</span></div>
</div>
<div class="ss"><div class="st">温度保护</div>
<div class="fg"><label>关闭充放电温度:</label><input type="number" id="both" value="%BMS_OVERHEAT_THRESHOLD%" min="50" max="80" step="1"><span class="u">°C</span><span class="hint">超过此温度将关闭充放电，需手动重启恢复</span></div>
</div>
<div class="ss"><div class="st">均衡配置</div>
<div class="fg"><label>电池均衡:</label><label class="cl"><input type="checkbox" id="bbe" %BMS_BALANCING_CHECKED%><span style="margin-left:8px">启用</span></label></div>
<div class="fg"><label>均衡压差:</label><input type="number" id="bbd" value="%BMS_BALANCING_DIFF%" min="5" max="100" step="5"><span class="u">mV</span></div>
</div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-windows">
<fieldset class="fs">
<legend class="lg">充电时间窗口</legend>
<p style="color:#aaa;margin-bottom:10px;font-size:12px">配置允许充电的时间段。bit0=周日，bit1=周一 ... bit6=周六</p>
<div id="wc"></div>
<div style="margin-top:10px"><button type="button" class="btn btn-d" onclick="addW()" id="ab">添加窗口</button></div>
</fieldset>
</div>

<div class="pnl" id="p-cfg-power">
<fieldset class="fs">
<legend class="lg">电源管理</legend>
<div class="ss"><div class="st">充电配置</div>
<div class="fg"><label>最大充电电流:</label><input type="number" id="pmc" value="%POWER_MAX_CHARGE%" min="100" max="10000" step="100"><span class="u">mA</span></div>
<div class="fg"><label>充电电压:</label><input type="number" id="pcv" value="%POWER_CHARGE_VOLTAGE%" min="10000" max="25000" step="100"><span class="u">mV</span></div>
<div class="fg"><label>启动 SOC:</label><input type="number" id="pcs" value="%POWER_CHARGE_SOC_START%" min="0" max="90" step="5"><span class="u">%</span></div>
<div class="fg"><label>停止 SOC:</label><input type="number" id="pcp" value="%POWER_CHARGE_SOC_STOP%" min="50" max="100" step="5"><span class="u">%</span></div>
</div>
<div class="ss"><div class="st">放电配置</div>
<div class="fg"><label>最大放电电流:</label><input type="number" id="pmd" value="%POWER_MAX_DISCHARGE%" min="100" max="20000" step="100"><span class="u">mA</span></div>
<div class="fg"><label>停止 SOC:</label><input type="number" id="pds" value="%POWER_DISCHARGE_SOC_STOP%" min="0" max="30" step="5"><span class="u">%</span></div>
</div>
<div class="ss"><div class="st">混合供电</div>
<div class="fg"><label>混合供电:</label><label class="cl"><input type="checkbox" id="phe" %POWER_HYBRID_CHECKED%><span style="margin-left:8px">启用</span></label></div>
</div>
<div class="ss"><div class="st">保护配置</div>
<div class="fg"><label>AC输入电流:</label><input type="number" id="poc" value="%POWER_OVER_CURRENT%" min="500" max="20000" step="100"><span class="u">mA</span><span class="hint">芯片配置最大值为8064，超过此值都会限制到8064</span></div>
<div class="fg"><label>过温阈值:</label><input type="number" id="pot" value="%POWER_OVER_TEMP%" min="40" max="100" step="0.5"><span class="u">°C</span></div>
</div>
<div class="ss"><div class="st">充电温度限制</div>
<div class="fg"><label>充电最高温:</label><input type="number" id="pth" value="%POWER_CHARGE_TEMP_HIGH%" min="30" max="60" step="0.5"><span class="u">°C</span></div>
<div class="fg"><label>充电最低温:</label><input type="number" id="ptl" value="%POWER_CHARGE_TEMP_LOW%" min="-20" max="10" step="0.5"><span class="u">°C</span></div>
</div>
</fieldset>
</div>

<div class="fa">
<button type="button" class="btn" onclick="save()">保存配置</button>
</div>
<div class="nt"><strong>注意：</strong>事关安全，保护参数请谨慎设置。</div>
</div>

<!-- ===== 面板：ADC 校准系数 ===== -->
<div class="pnl" id="p-calibration">
<fieldset class="fs">
<legend class="lg">ADC 校准系数</legend>
<p style="color:#666;font-size:13px;margin:8px 0;line-height:1.6">校正系数范围：50-255（表示 0.50x - 2.55x），100 = 1.00x（无校正）。修改后点击"保存校准"立即生效。</p>
<div id="calContainer" style="display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px;margin-top:12px"></div>
<div class="fa" style="margin-top:16px;display:flex;align-items:center;gap:12px">
<button type="button" class="btn" onclick="saveCalibration()">保存校准</button>
<span id="calStatus" style="font-size:14px;color:#999"></span>
</div>
</fieldset>
</div>

<!-- ===== 面板：固件升级 (OTA) ===== -->
<div class="pnl" id="p-ota">
<div class="grid">
<div class="card"><div class="card-t">设备信息</div>
<div class="row"><span class="lb">芯片型号</span><span class="vl">ESP32-S3</span></div>
<div class="row"><span class="lb">当前固件</span><span class="vl">%FIRMWARE_VERSION%</span></div>
<div class="row"><span class="lb">可用空间</span><span class="vl">%FREE_SKETCH_SPACE% KB</span></div>
<div class="row"><span class="lb">Flash 大小</span><span class="vl">%FLASH_SIZE% MB</span></div>
</div>
<div class="card"><div class="card-t">升级须知</div>
<ul style="font-size:13px;color:#666;margin:8px 0;padding-left:20px">
<li>仅支持 .bin 格式固件文件</li>
<li>升级过程中请勿断电或重启</li>
<li>升级约需 1-2 分钟</li>
<li>升级完成后设备自动重启</li>
</ul>
</div>
</div>
<div class="card" style="margin-top:16px"><div class="card-t">固件上传</div>
<div class="ua" id="upArea">
<div class="ic">&#8679;</div>
<label for="fwFile">点击选择固件文件或拖拽到此处</label>
<input type="file" id="fwFile" accept=".bin" onchange="selFile(this)" style="display:none">
<div class="fn" id="fName"></div>
</div>
<div class="prg" id="prgC"><div class="prg-bar"><div class="prg-fill" id="prgF">0%</div></div></div>
<div class="oms" id="stMsg"></div>
<button class="btn ota-btn" id="upBtn" onclick="upload()" disabled>开始升级</button>
</div>

</div>
</div>

<!-- ===== 配置向导页面 (手机适配) ===== -->
<div class="wz-container" id="wz-page" style="display:none">
<div class="wz-header">
<h2>🔧 初始配置</h2>
<p>欢迎！请完成以下步骤来配置您的设备</p>
</div>

<div class="wz-progress">
<div class="wz-step active" id="wz-step-0"><div class="wz-step-circle">1</div><div class="wz-step-label">WiFi</div></div>
<div class="wz-step" id="wz-step-1"><div class="wz-step-circle">2</div><div class="wz-step-label">网络</div></div>
<div class="wz-step" id="wz-step-2"><div class="wz-step-circle">3</div><div class="wz-step-label">电池</div></div>
<div class="wz-step" id="wz-step-3"><div class="wz-step-circle">4</div><div class="wz-step-label">硬件</div></div>
<div class="wz-step" id="wz-step-4"><div class="wz-step-circle">5</div><div class="wz-step-label">MQTT</div></div>
<div class="wz-step" id="wz-step-5"><div class="wz-step-circle">6</div><div class="wz-step-label">完成</div></div>
</div>

<!-- 步骤 1: WiFi 设置 -->
<div class="wz-card" id="wz-card-0">
<div class="wz-card-title">📶 WiFi 设置</div>
<div class="wz-info">连接到您的家庭或办公室 WiFi 网络</div>
<div class="wz-field">
<label>WiFi 名称 (SSID)</label>
<input type="text" id="wz-wifi-ssid" placeholder="输入 WiFi 名称" autocomplete="off">
</div>
<div class="wz-field">
<label>WiFi 密码</label>
<input type="password" id="wz-wifi-pass" placeholder="输入 WiFi 密码" autocomplete="off">
</div>
</div>

<!-- 步骤 2: 网络配置 -->
<div class="wz-card" id="wz-card-1" style="display:none">
<div class="wz-card-title">🌐 网络配置</div>
<div class="wz-field">
<label>IP 地址获取方式</label>
<select id="wz-ip-mode" onchange="wzToggleIP()">
<option value="dhcp">动态获取 (DHCP)</option>
<option value="static">固定 IP 地址</option>
</select>
</div>
<div id="wz-static-ip" style="display:none">
<div class="wz-field">
<label>IP 地址</label>
<input type="text" id="wz-static-ip-addr" placeholder="192.168.1.100">
</div>
<div class="wz-field">
<label>网关</label>
<input type="text" id="wz-static-gateway" placeholder="192.168.1.1">
</div>
<div class="wz-field">
<label>子网掩码</label>
<input type="text" id="wz-static-subnet" placeholder="255.255.255.0">
</div>
<div class="wz-field">
<label>DNS 服务器</label>
<input type="text" id="wz-static-dns" placeholder="8.8.8.8">
</div>
</div>
<div class="wz-field" style="margin-top:16px">
<label>NTP 时间服务器</label>
<input type="text" id="wz-ntp-server" placeholder="ntp.aliyun.com" value="ntp.aliyun.com">
</div>
</div>

<!-- 步骤 3: 电池配置 -->
<div class="wz-card" id="wz-card-2" style="display:none">
<div class="wz-card-title">🔋 电池配置</div>
<div class="wz-info">请根据您的电池规格进行配置</div>
<div class="wz-field">
<label>电池串数 (Cells)</label>
<select id="wz-cell-count">
<option value="3">3 串 (11.1V)</option>
<option value="4">4 串 (14.8V)</option>
<option value="5">5 串 (18.5V)</option>
</select>
</div>
<div class="wz-field">
<label>电池容量</label>
<input type="number" id="wz-capacity" placeholder="2000" min="100" max="50000">
</div>
<div class="wz-field">
<label>最大充电电流 (mA)</label>
<input type="number" id="wz-charge-current" placeholder="1000" min="100" max="10000">
</div>
<div class="wz-field">
<label>最大放电电流 (mA)</label>
<input type="number" id="wz-discharge-current" placeholder="2000" min="100" max="20000">
</div>
<div class="wz-field">
<label>过压保护阈值 (mV)</label>
<input type="number" id="wz-ov-threshold" placeholder="4200" min="4000" max="4500">
</div>
<div class="wz-field">
<label>欠压保护阈值 (mV)</label>
<input type="number" id="wz-uv-threshold" placeholder="3000" min="2500" max="3500">
</div>
<div class="wz-field">
<div class="wz-checkbox">
<label>启用电池均衡</label>
<input type="checkbox" id="wz-balancing" checked>
</div>
</div>
</div>

<!-- 步骤 4: 硬件设置 -->
<div class="wz-card" id="wz-card-3" style="display:none">
<div class="wz-card-title">⚙️ 硬件设置</div>
<div class="wz-field">
<div class="wz-checkbox">
<label>启用蜂鸣器</label>
<input type="checkbox" id="wz-buzzer" checked>
</div>
</div>
<div class="wz-field">
<label>蜂鸣器音量</label>
<input type="range" id="wz-volume" min="0" max="100" value="70" oninput="document.getElementById('wz-vol-val').textContent=this.value+'%'">
<div style="text-align:right;font-size:12px;color:#888;margin-top:4px"><span id="wz-vol-val">70%</span></div>
</div>
<div class="wz-field">
<label>LED 亮度</label>
<input type="range" id="wz-brightness" min="0" max="100" value="80" oninput="document.getElementById('wz-br-val').textContent=this.value+'%'">
<div style="text-align:right;font-size:12px;color:#888;margin-top:4px"><span id="wz-br-val">80%</span></div>
</div>
<div class="wz-field">
<div class="wz-checkbox">
<label>启用 HID 报告</label>
<input type="checkbox" id="wz-hid" checked>
</div>
</div>
<div class="wz-field">
<label>HID 电量模式</label>
<select id="wz-hid-mode">
<option value="0">毫安时 (mAh)</option>
<option value="1">毫瓦时 (mWh)</option>
<option value="2">百分比 (%)</option>
</select>
</div>
</div>

<!-- 步骤 4: MQTT 设置 (可选) -->
<div class="wz-card" id="wz-card-4" style="display:none">
<div class="wz-card-title">🔌 MQTT 配置（可选）</div>
<div class="wz-info">配置 Home Assistant 或其他 MQTT 客户端连接</div>
<div class="wz-field">
<div class="wz-checkbox">
<label>启用 MQTT</label>
<input type="checkbox" id="wz-mqtt-en">
</div>
</div>
<div id="wz-mqtt-config" style="display:none">
<div class="wz-field">
<label>Broker 地址</label>
<input type="text" id="wz-mqtt-broker" placeholder="192.168.1.100">
</div>
<div class="wz-field">
<label>端口</label>
<input type="number" id="wz-mqtt-port" placeholder="1883" min="1" max="65535">
</div>
<div class="wz-field">
<label>用户名（可选）</label>
<input type="text" id="wz-mqtt-user" placeholder="可选">
</div>
<div class="wz-field">
<label>密码（可选）</label>
<input type="password" id="wz-mqtt-pass" placeholder="可选">
</div>
</div>
<script>document.getElementById('wz-mqtt-en').addEventListener('change',function(){var d=document.getElementById('wz-mqtt-config');d.style.display=this.checked?'block':'none';});</script>
</div>

<!-- 步骤 5: 完成 -->
<div class="wz-card" id="wz-card-5" style="display:none">
<div class="wz-success">
<div class="wz-success-icon">✅</div>
<h3>配置完成！</h3>
<p>配置已保存，设备即将重启<br>重启后请访问管理页面查看实时监控</p>
<div id="wz-reboot-count" style="margin-top:16px;font-size:18px;color:#1677ff;font-weight:600">5</div>
</div>
</div>

<div class="wz-navigate" id="wz-nav">
<button class="wz-btn wz-btn-prev" id="wz-prev" onclick="wzPrev()" disabled>上一步</button>
<button class="wz-btn wz-btn-next" id="wz-next" onclick="wzNext()">下一步</button>
</div>
</div>

<div class="foot"><span id="updT">--</span></div>
</div>
</div>
</div>
<script id="dynamic-js"></script>
</body>
</html>
)rawliteral";

#endif // PAGE_TEMPLATES_H