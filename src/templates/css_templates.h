#ifndef CSS_TEMPLATES_H
#define CSS_TEMPLATES_H

#include <Arduino.h>

// =============================================================================
// 共享 CSS - macOS 系统风格
// =============================================================================
const char COMMON_CSS[] PROGMEM = R"rawliteral(
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'SF Pro Display','SF Pro Text','Helvetica Neue',sans-serif;background:#f5f5f7;color:#1d1d1f;min-height:100vh;display:flex;flex-direction:column;-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale}

/* macOS 风格顶栏 - 磨砂玻璃效果 */
.topbar{background:rgba(245,245,247,0.82);backdrop-filter:saturate(180%) blur(20px);-webkit-backdrop-filter:saturate(180%) blur(20px);padding:0 20px;border-bottom:1px solid rgba(0,0,0,0.1);display:flex;align-items:center;height:52px;gap:16px;position:sticky;top:0;z-index:100}

/* macOS 交通灯窗口控制按钮 */
.traffic-lights{display:flex;gap:8px;align-items:center;margin-right:8px}
.tl{width:12px;height:12px;border-radius:50%;cursor:pointer;transition:all .15s ease}
.tl-close{background:#ff5f57}
.tl-minimize{background:#febc2e}
.tl-maximize{background:#28c840}
.tl:hover{opacity:0.8;transform:scale(1.1)}

.topbar h1{font-size:15px;font-weight:600;color:#1d1d1f;white-space:nowrap;letter-spacing:-0.01em}
.topbar-info{display:flex;gap:16px;font-size:12px;color:#86868b;flex-wrap:wrap;margin-left:auto;align-items:center}
.topbar-info span{white-space:nowrap;font-weight:500}

/* 主布局 */
.main-wrap{display:flex;flex:1;overflow:hidden}

/* macOS 风格侧边栏 */
.side{width:200px;background:rgba(251,251,253,0.82);backdrop-filter:saturate(180%) blur(20px);-webkit-backdrop-filter:saturate(180%) blur(20px);border-right:1px solid rgba(0,0,0,0.08);padding:8px 0;overflow-y:auto;flex-shrink:0}
.si{padding:10px 20px;cursor:pointer;font-size:13px;color:#1d1d1f;border-left:3px solid transparent;transition:all .2s ease;font-weight:500}
.si:hover{background:rgba(0,0,0,0.04)}
.si.active{background:rgba(0,122,255,0.08);color:#007AFF;border-left-color:#007AFF;font-weight:600}

/* 内容区 */
.ct{flex:1;padding:20px;overflow-y:auto;background:#f5f5f7}
.pnl{display:none}.pnl.active{display:block}
#p-config .pnl{display:none!important}
#p-config .pnl.active{display:block!important}

/* 网格布局 */
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px}

/* macOS 风格卡片 */
.card{background:#fff;border-radius:12px;padding:18px;border:none;box-shadow:0 1px 3px rgba(0,0,0,0.06),0 1px 2px rgba(0,0,0,0.04);transition:box-shadow .2s ease}
.card:hover{box-shadow:0 4px 12px rgba(0,0,0,0.08),0 2px 4px rgba(0,0,0,0.04)}
.card-t{font-size:11px;color:#86868b;text-transform:uppercase;letter-spacing:.5px;margin-bottom:12px;font-weight:600}

/* 大数值显示 */
.bigv{font-size:36px;font-weight:700;color:#1d1d1f;line-height:1;letter-spacing:-0.02em}
.bigu{font-size:13px;color:#86868b;margin-left:4px;font-weight:500}

/* 行样式 */
.row{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid rgba(0,0,0,0.04);font-size:13px}
.row:last-child{border-bottom:none}
.lb{color:#86868b;font-weight:500}.vl{color:#1d1d1f;font-weight:600}
.vl.g{color:#34c759}.vl.w{color:#ff9500}.vl.r{color:#ff3b30}

/* 进度条 */
.pb{background:#e8e8ed;border-radius:6px;height:18px;overflow:hidden;margin:8px 0}
.pf{height:100%;border-radius:6px;transition:width .4s cubic-bezier(0.4,0,0.2,1);display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:600;color:#fff;background:linear-gradient(135deg,#007AFF 0%,#5856D6 100%)}

/* 电芯显示 */
.cells{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;margin-top:10px}
.cell{text-align:center;padding:10px 6px;background:linear-gradient(135deg,#f5f5f7 0%,#e8e8ed 100%);border-radius:10px;border:1px solid rgba(0,0,0,0.04)}
.cn{font-size:10px;color:#86868b;font-weight:600}.cv{font-size:14px;font-weight:700;color:#1d1d1f;margin-top:3px}

/* macOS 风格徽章 */
.badge{display:inline-block;padding:2px 10px;border-radius:20px;font-size:11px;font-weight:600;letter-spacing:0.02em}
.badge.g{background:rgba(52,199,89,0.12);color:#34c759}
.badge.w{background:rgba(255,149,0,0.12);color:#ff9500}
.badge.r{background:rgba(255,59,48,0.12);color:#ff3b30}
.badge.b{background:rgba(0,122,255,0.12);color:#007AFF}

/* WebSocket 状态 */
.ws-st{padding:4px 12px;border-radius:20px;font-size:11px;font-weight:600;color:#fff}
.ws-st.ok{background:#34c759}.ws-st.fail{background:#ff3b30}

/* 统计框 */
.stat-box{background:linear-gradient(135deg,#f5f5f7 0%,#fff 100%);border:1px solid rgba(0,0,0,0.04);border-radius:10px;padding:12px;text-align:center;flex:1;min-width:0}
.stat-label{font-size:11px;color:#86868b;margin-bottom:6px;font-weight:600}
.stat-value{font-size:17px;font-weight:700;color:#1d1d1f;letter-spacing:-0.01em}
.stat-value.g{color:#34c759}.stat-value.r{color:#ff3b30}.stat-value.w{color:#ff9500}.stat-value.b{color:#007AFF}

/* 页脚 */
.foot{text-align:center;padding:10px;color:#86868b;font-size:11px;border-top:1px solid rgba(0,0,0,0.06);background:rgba(245,245,247,0.82);backdrop-filter:saturate(180%) blur(20px);-webkit-backdrop-filter:saturate(180%) blur(20px);font-weight:500}

/* 响应式 */
@media(max-width:600px){.side{width:140px}.topbar h1{font-size:14px}.ct{padding:12px}.traffic-lights{display:none}}

/* 配置页标签 - macOS 分段控件风格 */
#p-config .si{border-left:none!important;border-bottom:3px solid transparent;padding:10px 16px;font-weight:500}
#p-config .si:hover{background:rgba(0,0,0,0.04)}
#p-config .si.active{background:transparent;color:#007AFF;border-bottom-color:#007AFF;font-weight:600}
)rawliteral";

// =============================================================================
// 配置页 CSS 扩展 - macOS 风格表单
// =============================================================================
const char CONFIG_CSS[] PROGMEM = R"rawliteral(
/* macOS 风格字段集 */
.fs{border:none;border-radius:14px;padding:20px;margin-bottom:16px;background:#fff;box-shadow:0 1px 3px rgba(0,0,0,0.06),0 1px 2px rgba(0,0,0,0.04)}
.lg{font-weight:700;color:#1d1d1f;padding:0 12px;font-size:14px;letter-spacing:-0.01em}

/* 表单组 */
.fg{margin-bottom:12px;display:flex;align-items:center;flex-wrap:wrap}
.fg label{min-width:130px;text-align:right;margin-right:12px;font-weight:600;font-size:12px;color:#86868b}

/* macOS 风格输入框 */
.fg input[type=text],.fg input[type=password],.fg input[type=number],.fg select{padding:8px 12px;width:180px;border:1.5px solid #d1d1d6;border-radius:8px;font-size:13px;background:#fff;color:#1d1d1f;outline:none;transition:all .2s ease;font-weight:500}
.fg input:focus,.fg select:focus{border-color:#007AFF;box-shadow:0 0 0 3px rgba(0,122,255,0.12)}

/* macOS 风格复选框 */
.fg input[type=checkbox]{-webkit-appearance:none;appearance:none;width:18px;height:18px;border:1.5px solid #d1d1d6;border-radius:4px;background:#fff;cursor:pointer;position:relative;transition:all .2s ease;vertical-align:middle}
.fg input[type=checkbox]:hover{border-color:#007AFF}
.fg input[type=checkbox]:checked{background:#007AFF;border-color:#007AFF}
.fg input[type=checkbox]:checked::after{content:'';position:absolute;left:4px;top:1px;width:5px;height:9px;border:solid #fff;border-width:0 2px 2px 0;transform:rotate(45deg)}
.fg input[type=checkbox]:focus{box-shadow:0 0 0 3px rgba(0,122,255,0.12)}

/* macOS 风格滑块 */
.fg input[type=range]{-webkit-appearance:none;height:6px;background:linear-gradient(90deg,#007AFF 0%,#d1d1d6 0%);border-radius:3px;outline:none;flex:1;margin-right:14px;transition:background .1s}
.fg input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;background:#fff;border:1.5px solid #007AFF;border-radius:50%;cursor:pointer;box-shadow:0 1px 4px rgba(0,0,0,0.15);transition:all .15s ease}
.fg input[type=range]::-webkit-slider-thumb:hover{transform:scale(1.15);box-shadow:0 2px 8px rgba(0,122,255,0.3)}
.fg input[type=range]::-moz-range-thumb{width:18px;height:18px;background:#fff;border:1.5px solid #007AFF;border-radius:50%;cursor:pointer;box-shadow:0 1px 4px rgba(0,0,0,0.15)}

.cl{display:inline-flex;align-items:center;cursor:pointer}
.u{margin-left:10px;color:#86868b;font-size:11px;font-weight:500}
.hint{margin-left:10px;color:#86868b;font-size:11px;font-style:italic;font-weight:400}

/* macOS 风格按钮 */
.btn{background:linear-gradient(135deg,#007AFF 0%,#0055CC 100%);color:#fff;padding:10px 24px;border:none;cursor:pointer;border-radius:8px;font-size:13px;font-weight:600;transition:all .2s ease;box-shadow:0 1px 2px rgba(0,122,255,0.2)}
.btn:hover{background:linear-gradient(135deg,#0066DD 0%,#0044AA 100%);box-shadow:0 4px 12px rgba(0,122,255,0.3);transform:translateY(-1px)}
.btn:active{transform:translateY(0);box-shadow:0 1px 2px rgba(0,122,255,0.2)}

.btn-d{background:#fff;color:#1d1d1f;border:1.5px solid #d1d1d6;box-shadow:none}.btn-d:hover{background:#f5f5f7;border-color:#8e8e93;box-shadow:0 2px 6px rgba(0,0,0,0.06)}
.btn-r{background:linear-gradient(135deg,#ff3b30 0%,#d70015 100%);box-shadow:0 1px 2px rgba(255,59,48,0.2)}.btn-r:hover{background:linear-gradient(135deg,#ff453a 0%,#e0115f 100%);box-shadow:0 4px 12px rgba(255,59,48,0.3)}

.fa{text-align:center;margin-top:20px;padding:16px;border-top:1px solid rgba(0,0,0,0.06)}

/* macOS 风格警告框 */
.nt{background:linear-gradient(135deg,#fff9e6 0%,#fff3cc 100%);padding:12px 16px;border-radius:10px;margin:12px 0;font-size:12px;border:1.5px solid #ffcc00;color:#b8860b;font-weight:500;box-shadow:0 1px 2px rgba(255,204,0,0.15)}

/* 分隔区域 */
.ss{margin-top:16px;padding-top:16px;border-top:1px solid rgba(0,0,0,0.06)}
.st{font-weight:700;color:#1d1d1f;margin-bottom:12px;font-size:13px;letter-spacing:-0.01em}
)rawliteral";

// =============================================================================
// 配置向导 CSS - macOS 风格 (手机适配)
// =============================================================================
const char WIZARD_CSS[] PROGMEM = R"rawliteral(
.wz-container{max-width:480px;margin:0 auto;padding:16px}

/* macOS 风格向导头部 */
.wz-header{text-align:center;padding:28px 20px;background:linear-gradient(135deg,#007AFF 0%,#5856D6 100%);border-radius:16px;margin-bottom:24px;color:#fff;box-shadow:0 4px 16px rgba(0,122,255,0.3)}
.wz-header h2{font-size:24px;margin-bottom:8px;font-weight:700;letter-spacing:-0.02em}
.wz-header p{font-size:13px;color:rgba(255,255,255,0.85);font-weight:500}

/* 进度条 */
.wz-progress{display:flex;justify-content:space-between;margin-bottom:28px;position:relative}
.wz-progress::before{content:'';position:absolute;top:50%;left:0;right:0;height:2px;background:#d1d1d6;z-index:0}
.wz-step{flex:1;text-align:center;position:relative;z-index:1}
.wz-step-circle{width:34px;height:34px;border-radius:50%;background:#fff;border:2px solid #d1d1d6;display:flex;align-items:center;justify-content:center;margin:0 auto 8px;font-size:13px;font-weight:700;color:#86868b;transition:all .3s cubic-bezier(0.4,0,0.2,1);box-shadow:0 1px 3px rgba(0,0,0,0.06)}
.wz-step.active .wz-step-circle{border-color:#007AFF;background:linear-gradient(135deg,#007AFF 0%,#5856D6 100%);color:#fff;box-shadow:0 2px 8px rgba(0,122,255,0.3)}
.wz-step.completed .wz-step-circle{border-color:#34c759;background:#34c759;color:#fff;box-shadow:0 2px 8px rgba(52,199,89,0.3)}
.wz-step-label{font-size:11px;color:#86868b;margin-top:4px;font-weight:500}
.wz-step.active .wz-step-label{color:#007AFF;font-weight:700}

/* macOS 风格向导卡片 */
.wz-card{background:#fff;border-radius:16px;padding:24px;margin-bottom:20px;box-shadow:0 2px 12px rgba(0,0,0,0.06),0 1px 3px rgba(0,0,0,0.04)}
.wz-card-title{font-size:17px;font-weight:700;color:#1d1d1f;margin-bottom:18px;padding-bottom:14px;border-bottom:1px solid rgba(0,0,0,0.06);letter-spacing:-0.01em}

/* 向导字段 */
.wz-field{margin-bottom:18px}
.wz-field label{display:block;font-size:13px;color:#86868b;margin-bottom:8px;font-weight:600}
.wz-field input,.wz-field select{width:100%;padding:12px 16px;border:1.5px solid #d1d1d6;border-radius:10px;font-size:15px;background:#fff;color:#1d1d1f;outline:none;transition:all .2s ease;font-weight:500;-webkit-appearance:none;appearance:none}
.wz-field select{background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%2386868b' d='M6 8L1 3h10z'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 14px center;padding-right:36px}
.wz-field input:focus,.wz-field select:focus{border-color:#007AFF;box-shadow:0 0 0 4px rgba(0,122,255,0.12)}
.wz-field input::placeholder{color:#8e8e93}

/* macOS 风格复选框 */
.wz-checkbox{display:flex;align-items:center;justify-content:space-between;padding:14px 16px;background:linear-gradient(135deg,#f5f5f7 0%,#fafafa 100%);border-radius:10px;border:1.5px solid #e8e8ed}
.wz-checkbox label{margin:0;color:#1d1d1f;font-weight:600;font-size:14px}
.wz-checkbox input[type=checkbox]{-webkit-appearance:none;appearance:none;width:22px;height:22px;border:1.5px solid #d1d1d6;border-radius:5px;background:#fff;cursor:pointer;position:relative;transition:all .2s ease;flex-shrink:0}
.wz-checkbox input[type=checkbox]:hover{border-color:#007AFF}
.wz-checkbox input[type=checkbox]:checked{background:#007AFF;border-color:#007AFF}
.wz-checkbox input[type=checkbox]:checked::after{content:'';position:absolute;left:5px;top:2px;width:6px;height:11px;border:solid #fff;border-width:0 2px 2px 0;transform:rotate(45deg)}
.wz-checkbox input[type=checkbox]:focus{box-shadow:0 0 0 3px rgba(0,122,255,0.12)}

/* 导航按钮 */
.wz-navigate{display:flex;justify-content:space-between;gap:14px;margin-top:28px}
.wz-btn{flex:1;padding:14px;border-radius:12px;border:none;font-size:15px;font-weight:600;cursor:pointer;transition:all .2s cubic-bezier(0.4,0,0.2,1);letter-spacing:-0.01em}
.wz-btn-prev{background:#f5f5f7;color:#86868b;box-shadow:0 1px 2px rgba(0,0,0,0.04)}
.wz-btn-prev:hover{background:#e8e8ed;color:#1d1d1f}
.wz-btn-next{background:linear-gradient(135deg,#007AFF 0%,#5856D6 100%);color:#fff;box-shadow:0 2px 8px rgba(0,122,255,0.3)}
.wz-btn-next:hover{box-shadow:0 4px 16px rgba(0,122,255,0.4);transform:translateY(-1px)}
.wz-btn-save{background:linear-gradient(135deg,#34c759 0%,#248a3d 100%);color:#fff;box-shadow:0 2px 8px rgba(52,199,89,0.3)}
.wz-btn-save:hover{box-shadow:0 4px 16px rgba(52,199,89,0.4);transform:translateY(-1px)}
.wz-btn:disabled{opacity:.4;cursor:not-allowed;transform:none!important;box-shadow:none!important}

/* 成功页面 */
.wz-success{text-align:center;padding:44px 24px}
.wz-success-icon{font-size:52px;margin-bottom:20px}
.wz-success h3{font-size:20px;color:#1d1d1f;margin-bottom:10px;font-weight:700;letter-spacing:-0.02em}
.wz-success p{font-size:14px;color:#86868b;line-height:1.7;font-weight:500}

/* 信息框 */
.wz-info{background:linear-gradient(135deg,#e6f0ff 0%,#f0f5ff 100%);border-radius:10px;padding:14px 16px;margin-bottom:18px;font-size:12px;color:#007AFF;border:1.5px solid rgba(0,122,255,0.15);font-weight:500}

/* 响应式 */
@media(max-width:480px){.wz-container{padding:10px}.wz-header{border-radius:14px;padding:24px 18px}.wz-card{padding:18px;border-radius:14px}}
)rawliteral";

// =============================================================================
// OTA CSS 扩展 - macOS 风格固件上传
// =============================================================================
const char OTA_CSS[] PROGMEM = R"rawliteral(
.up{max-width:560px;margin:30px auto;padding:0 16px}

/* macOS 风格信息卡片 */
.info{background:#fff;padding:18px;border-radius:14px;border:none;box-shadow:0 1px 3px rgba(0,0,0,0.06),0 1px 2px rgba(0,0,0,0.04);margin-bottom:16px}
.info h3{font-size:14px;color:#1d1d1f;margin-bottom:10px;font-weight:700;letter-spacing:-0.01em}
.info p{font-size:13px;color:#86868b;margin:5px 0;font-weight:500}

/* macOS 风格警告框 */
.warn{background:linear-gradient(135deg,#fff9e6 0%,#fff3cc 100%);padding:16px;border-radius:12px;border:1.5px solid #ffcc00;margin-bottom:16px;font-size:13px;color:#b8860b;box-shadow:0 1px 2px rgba(255,204,0,0.15)}
.warn ul{margin:8px 0 0 18px}
.warn li{margin:4px 0;font-weight:500}

/* macOS 风格上传区域 */
.ua{border:2px dashed #d1d1d6;border-radius:14px;padding:40px;text-align:center;margin:18px 0;transition:all .25s cubic-bezier(0.4,0,0.2,1);cursor:pointer;background:linear-gradient(135deg,#fafafa 0%,#fff 100%)}
.ua:hover{border-color:#007AFF;background:linear-gradient(135deg,#f0f5ff 0%,#f5f8ff 100%);box-shadow:0 4px 16px rgba(0,122,255,0.1)}
.ua.drag{border-color:#007AFF;background:linear-gradient(135deg,#e6f0ff 0%,#f0f5ff 100%);box-shadow:0 4px 20px rgba(0,122,255,0.15)}
.ua .ic{font-size:40px;color:#8e8e93;margin-bottom:10px}
.ua input[type=file]{display:none}
.ua label{cursor:pointer;color:#1d1d1f;font-size:14px;font-weight:600}
.ua .fn{margin-top:12px;color:#007AFF;font-size:13px;font-weight:600}

/* 进度条 */
.prg{margin:18px 0;display:none}
.prg-bar{background:#e8e8ed;border-radius:8px;height:22px;overflow:hidden;box-shadow:inset 0 1px 2px rgba(0,0,0,0.06)}
.prg-fill{height:100%;background:linear-gradient(90deg,#007AFF 0%,#5856D6 100%);border-radius:8px;transition:width .3s cubic-bezier(0.4,0,0.2,1);display:flex;align-items:center;justify-content:center;color:#fff;font-size:11px;font-weight:700;letter-spacing:0.02em}

/* 状态消息 */
.oms{margin:14px 0;padding:12px 16px;border-radius:10px;text-align:center;font-size:13px;font-weight:600;display:none}
.oms.ok{display:block;background:rgba(52,199,89,0.1);color:#34c759;border:1.5px solid rgba(52,199,89,0.2)}
.oms.er{display:block;background:rgba(255,59,48,0.1);color:#ff3b30;border:1.5px solid rgba(255,59,48,0.2)}
.oms.nf{display:block;background:rgba(0,122,255,0.1);color:#007AFF;border:1.5px solid rgba(0,122,255,0.2)}

/* macOS 风格上传按钮 */
.ota-btn{background:linear-gradient(135deg,#007AFF 0%,#5856D6 100%);color:#fff;padding:12px 0;border:none;cursor:pointer;border-radius:10px;font-size:15px;font-weight:700;width:100%;margin-top:12px;transition:all .2s cubic-bezier(0.4,0,0.2,1);box-shadow:0 2px 8px rgba(0,122,255,0.25);letter-spacing:-0.01em}
.ota-btn:hover{box-shadow:0 4px 16px rgba(0,122,255,0.35);transform:translateY(-1px)}
.ota-btn:active{transform:translateY(0);box-shadow:0 2px 8px rgba(0,122,255,0.25)}
.ota-btn:disabled{background:linear-gradient(135deg,#d1d1d6 0%,#8e8e93 100%);box-shadow:none;cursor:not-allowed;transform:none}
)rawliteral";

#endif // CSS_TEMPLATES_H