'use strict';
const $ = id => document.getElementById(id);
const text = (id, value) => { $(id).textContent = value; };
const pad = value => String(value).padStart(2, '0');
const state = {online:false, busy:false, status:null, events:[], rtcEpoch:null, polling:null, tasksPolling:null, tasksKey:null};
const weekdays = ['星期日','星期一','星期二','星期三','星期四','星期五','星期六'];
let toastTimer;
const UI_VERSION = '20261028-c6-r2';
const scheduleView = location.pathname === '/RTC_Event';
const wifiView = location.pathname === '/WiFi_Test';
let currentBoot = null;
document.body.classList.toggle('schedule-view', scheduleView);
document.body.classList.toggle('wifi-view', wifiView);
$(wifiView ? 'nav-wifi' : scheduleView ? 'nav-schedules' : 'nav-overview').classList.add('active');
if (wifiView) {
  text('page-title', 'Wi-Fi 测试');
  text('breadcrumb', 'Wi-Fi 测试');
  text('page-subtitle', '扫描、连接，实时查看网络状态。');
}
if (scheduleView) {
  text('page-title', '定时任务'); text('breadcrumb', '定时任务');
  text('page-subtitle', '让每一次开启与关闭，都恰逢其时。');
}
function toast(message, error=false) {
  clearTimeout(toastTimer);
  text('toast', message);
  $('toast').className = 'toast visible' + (error ? ' error' : '');
  toastTimer = setTimeout(() => $('toast').classList.remove('visible'), 3400);
}
async function request(url, options={}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 6000);
  try {
    const response = await fetch(url, {...options, cache:'no-store', signal:controller.signal});
    if (!response.ok) {
      const detail = (await response.text()).replace(/<[^>]*>/g, '').slice(0, 160);
      throw new Error(detail.includes('ESP_ERR') ? '设备操作失败：' + detail : '请求失败，请确认设备连接和输入内容');
    }
    return (response.headers.get('content-type') || '').includes('application/json') ? await response.json() : await response.text();
  } catch (error) {
    if (error.name === 'AbortError' || error instanceof TypeError) throw new Error('暂时无法连接设备，请检查 Wi-Fi 连接');
    throw error;
  } finally { clearTimeout(timeout); }
}
function updateControls() {
  const disabled = !state.online || state.busy;
  $('toggle').disabled = disabled;
  $('relay-on').disabled = disabled || state.status?.relay === true;
  $('relay-off').disabled = disabled || state.status?.relay === false;
  $('sync-time').disabled = disabled;
  document.querySelectorAll('.add-event').forEach(button => button.disabled = disabled || state.events.length >= 10);
}
function uptime(seconds) {
  seconds = Math.max(0, Math.floor(seconds || 0));
  const days = Math.floor(seconds / 86400), hours = Math.floor(seconds / 3600) % 24, minutes = Math.floor(seconds / 60) % 60;
  return days ? `${days} 天 ${pad(hours)} 小时` : `${pad(hours)} 小时 ${pad(minutes)} 分钟`;
}
function renderStatus(status) {
  state.status = status;
  $('relay-card').classList.toggle('on', status.relay);
  $('toggle').setAttribute('aria-checked', String(status.relay));
  text('relay-label', status.relay ? '已开启' : '已关闭');
  text('relay-description', status.relay ? 'CH1 输出已接通' : 'CH1 输出已断开');
  text('network-mode', status.mode === 'AP' ? 'AP 热点模式' : 'STA 路由器模式');
  const detail = status.connected ? (status.mode === 'AP' ? `${status.ssid} · ${status.clients} 台设备` : `${status.ssid} · ${status.rssi ?? '--'} dBm`) : '等待连接路由器';
  text('network-detail', detail);
  $('network-detail').title = `${detail} · IP ${status.ip}`;
  $('network-dot').classList.toggle('on', status.connected);
  text('uptime', uptime(status.uptime));
  text('board-name', status.board);
  text('clock-source', status.rtcReadOk ? '板载 RTC 直读' : 'RTC 读取失败');
  text('clock-detail', !status.rtcReadOk ? (status.rtcReadError || '请检查 RTC 连接') :
    !status.rtcCalendarValid ? '日期未初始化，仍显示芯片读数' :
    status.rtcOscillatorStopped ? '曾停振，当前读数未自动修改' :
    status.rtcStopped ? 'RTC 当前处于停止状态' : '当前时间由 RTC 自身走时');
  const startup = {'initialized-1970':'本次启动因停振或无效日期初始化为 1970','retained':'本次启动保留芯片时间','initialization-failed':'RTC 初始时间写入失败','read-failed':'启动读取失败'}[status.rtcStartupAction] || '';
  text('rtc-register-state', status.rtcReadOk ? `OS=${status.rtcOscillatorStopped ? 1 : 0} · STOP=${status.rtcStopped ? 1 : 0} · ${startup}` : 'RTC 寄存器读取失败');
  $('rtc-register-state').title = 'RTC 寄存器 00h–0Ah：' + (status.rtcRegisters || '--');
  const offset = status.utcOffset || '+0800';
  text('timezone', `UTC${offset.slice(0,3)}:${offset.slice(3)}`);
  text('sync-hint', !status.rtcReadOk ? 'RTC 读取失败，正在重试' :
    !status.rtcCalendarValid ? 'RTC 日期未初始化；当前显示寄存器读数，可手动校准' :
    status.rtcStopped ? 'RTC 处于停止状态，点击校准可启动计时' :
    status.rtcOscillatorStopped ? 'RTC 有停振标志，当前显示其内部时间；仅点击校准才修改' :
    '打开网页即读取 RTC，点击校准才修改时间');
  const fields = status.rtcReadOk ? rtcFields(status.rtc) : null;
  if (fields) {
    state.rtcEpoch = status.rtcCalendarValid ? Date.UTC(fields.year, fields.month - 1, fields.day, fields.hour, fields.minute, fields.second) : null;
    text('rtc-time', `${pad(fields.hour)}:${pad(fields.minute)}:${pad(fields.second)}`);
    const weekday = Number.isInteger(status.rtcWeekday) ? weekdays[status.rtcWeekday] : '';
    text('rtc-date', `${fields.year} 年 ${pad(fields.month)} 月 ${pad(fields.day)} 日　${weekday || ''}`);
  } else {
    state.rtcEpoch = null;
    text('rtc-time', '--:--:--'); text('rtc-date', 'RTC 读取失败，正在重试');
  }
}
function rtcFields(value) {
  const match = /^(\d{4})[-/](\d{2})[-/](\d{2})[T ](\d{2}):(\d{2}):(\d{2})/.exec(value || '');
  if (!match) return null;
  return {year:Number(match[1]), month:Number(match[2]), day:Number(match[3]), hour:Number(match[4]), minute:Number(match[5]), second:Number(match[6])};
}
function cell(row, value, className='') {
  const element = document.createElement('td');
  if (className) element.className = className;
  if (typeof value === 'string') element.textContent = value; else element.append(value);
  row.append(element); return element;
}
function renderTasks(events) {
  state.events = events;
  text('nav-count', events.length); text('event-count', `${events.length} / 10`);
  $('empty-state').hidden = events.length > 0;
  $('event-table-wrap').hidden = events.length === 0;
  const key = JSON.stringify(events);
  if (key === state.tasksKey) return;
  state.tasksKey = key;
  $('event-list').replaceChildren();
  for (const event of events) {
    const row = document.createElement('tr');
    const name = document.createElement('strong'); name.textContent = `计划 ${pad(event.id)}`;
    cell(row, name);
    const date = document.createElement('div');
    const primary = document.createElement('strong'); primary.textContent = event.datetime.split('T')[1];
    const secondary = document.createElement('small'); secondary.textContent = event.datetime.split('T')[0];
    date.append(primary, secondary); cell(row, date);
    const action = document.createElement('span');
    action.className = 'action-pill' + (event.action === 0 ? ' off' : event.action === 2 ? ' retain' : '');
    action.textContent = ['关闭 CH1','开启 CH1','保持状态'][event.action] || '--'; cell(row, action);
    cell(row, ['仅一次','每天','每周 ' + weekdays[event.weekday].slice(2),'每月'][event.repeat] || '--');
    const remove = document.createElement('button'); remove.className = 'delete-event'; remove.textContent = '删除';
    remove.setAttribute('aria-label', `删除计划 ${event.id}`);
    remove.addEventListener('click', () => actionRequest(() => request(`/DeleteEvent?id=${event.id}`), '任务已删除'));
    cell(row, remove, 'align-right'); $('event-list').append(row);
  }
}
async function refresh() {
  if (state.polling) return state.polling;
  state.polling = (async () => {
    try {
      const status = await request('/api/status');
      if (status.uiVersion !== UI_VERSION || !status.bootId) throw new Error('网页与固件版本不一致，请重新烧录并刷新页面');
      if (currentBoot && currentBoot !== status.bootId) {
        clearRtc('设备已重新启动，正在读取 RTC');
      }
      currentBoot = status.bootId;
      text('firmware-version', status.uiVersion);
      text('boot-id', ' · 启动 ' + status.bootId);
      state.online = true;
      renderStatus(status);
      $('device-connection').classList.remove('offline');
      $('device-connection').querySelector('span').textContent = '设备在线 · ' + status.ip;
      text('last-update', status.rtcReadOk ? '最近 RTC 读取 ' + status.rtc.replace('T',' ') : 'RTC 读取失败');
    } catch (error) {
      state.online = false;
      $('device-connection').classList.add('offline');
      $('device-connection').querySelector('span').textContent = '设备连接中断';
      clearRtc('设备连接中断，等待重新读取 RTC');
      text('last-update', error.message);
      text('sync-hint', '连接恢复后重新读取芯片，不显示断线前时间');
    } finally { updateControls(); }
  })();
  try { await state.polling; } finally { state.polling = null; }
}
async function refreshTasks() {
  if (state.tasksPolling) return state.tasksPolling;
  state.tasksPolling = (async () => {
    try {
      const tasks = await request('/getTimeAndEvent');
      if (!Array.isArray(tasks.events)) throw new Error('设备页面版本不一致');
      renderTasks(tasks.events);
      updateControls();
    } catch (error) { /* Keep the last task list; RTC requests are independent. */ }
  })();
  try { await state.tasksPolling; } finally { state.tasksPolling = null; }
}

async function actionRequest(operation, success) {
  if (state.busy) return;
  state.busy = true; updateControls();
  try {
    await operation();
    if (state.polling) await state.polling;
    await Promise.all([refresh(), refreshTasks()]); toast(success);
  } catch (error) { toast(error.message, true); }
  finally { state.busy = false; updateControls(); }
}
$('toggle').addEventListener('click', () => actionRequest(() => request('/Switch1'), '继电器状态已更新'));
$('relay-on').addEventListener('click', () => actionRequest(() => request('/RelayOn'), '继电器已开启'));
$('relay-off').addEventListener('click', () => actionRequest(() => request('/RelayOff'), '继电器已关闭'));
$('refresh').addEventListener('click', async () => { await refresh(); if (wifiView) await refreshWifi(); toast(state.online ? '设备状态已更新' : '暂时无法连接设备', !state.online); });
$('sync-time').addEventListener('click', () => actionRequest(() => request('/api/time', {method:'POST', headers:{'Content-Type':'text/plain'}, body:String(Math.floor(Date.now()/1000))}), '本机时间已写入 RTC'));
function openDialog() {
  text('form-error', '');
  if (state.rtcEpoch === null) {
    $('event-date').value = ''; $('event-time').value = '';
    text('form-error', 'RTC 尚未校准，请先校准或手动选择时间');
  } else {
    const date = new Date(state.rtcEpoch + 300000);
    $('event-date').value = `${date.getUTCFullYear()}-${pad(date.getUTCMonth()+1)}-${pad(date.getUTCDate())}`;
    $('event-time').value = `${pad(date.getUTCHours())}:${pad(date.getUTCMinutes())}:${pad(date.getUTCSeconds())}`;
  }
  $('event-dialog').showModal();
}
document.querySelectorAll('.add-event').forEach(button => button.addEventListener('click', openDialog));
$('close-dialog').addEventListener('click', () => $('event-dialog').close());
$('cancel-dialog').addEventListener('click', () => $('event-dialog').close());
$('event-form').addEventListener('submit', async event => {
  event.preventDefault(); text('form-error','');
  if (state.busy) return;
  const date = $('event-date').value;
  const time = $('event-time').value.length === 5 ? $('event-time').value + ':00' : $('event-time').value;
  const parsed = new Date(`${date}T${time}Z`);
  const repeat = Number($('event-repeat').value);
  if (!Number.isFinite(parsed.getTime())) { text('form-error','请输入有效的日期和时间'); return; }
  if (repeat === 0 && state.rtcEpoch !== null && parsed.getTime() <= state.rtcEpoch) {
    text('form-error', '执行时间需要晚于设备当前时间'); return;
  }
  const data = `Date: ${date.replaceAll('-','/')} Week: ${parsed.getUTCDay()} Time: ${time} Relay CH1: ${$('event-action').value} Cycle: ${repeat}`;
  state.busy = true; updateControls(); $('save-event').disabled = true;
  try {
    await request('/NewEvent?' + new URLSearchParams({data}));
    $('event-dialog').close();
    if (state.polling) await state.polling;
    await Promise.all([refresh(), refreshTasks()]); toast('定时任务已添加');
  } catch (error) { text('form-error', error.message); }
  finally { state.busy = false; $('save-event').disabled = false; updateControls(); }
});
queueMicrotask(() => { refresh(); if (!wifiView) refreshTasks(); });
setInterval(() => { if (!document.hidden) refresh(); }, 1000);
setInterval(() => { if (!document.hidden && !wifiView) refreshTasks(); }, 3000);
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) { clearRtc('正在重新读取 RTC'); refresh(); if (!wifiView) refreshTasks(); }
});

function clearRtc(message) {
  state.rtcEpoch = null;
  text('rtc-time', '--:--:--'); text('rtc-date', message);
  text('clock-source', '等待读取 RTC'); text('clock-detail', message);
  text('rtc-register-state', '尚未取得本次有效响应');
}
window.addEventListener('pageshow', event => {
  if (event.persisted) { clearRtc('正在重新读取 RTC'); refresh(); }
});
window.addEventListener('offline', () => {
  state.online = false; clearRtc('网络已断开，等待重新读取 RTC'); updateControls();
});
window.addEventListener('online', () => refresh());
/* Read-only scan results and manual Ping on the existing STA connection. */
let wifiPolling = null, wifiStatus = null, wifiKey = null, testBusy = false;
const wifiStates = {idle:'尚未连接',connecting:'连接中',connected:'已获取 IP',disconnected:'已断开',failed:'连接失败'};
function testControls() {
  const disabled = !state.online || testBusy || !wifiStatus || wifiStatus.scanning || wifiStatus.ping?.busy;
  $('wifi-scan').disabled = disabled || wifiStatus?.state === 'connecting';
  $('wifi-ping').disabled = disabled || !wifiStatus?.connected;
}
function wifiFailure(reason) {
  const reasons = {2:'认证已过期',15:'握手超时，请检查密码',201:'找不到目标网络',202:'认证失败，请检查密码',203:'关联失败',204:'握手超时',205:'连接失败',210:'安全配置不匹配'};
  return reasons[reason] || '请检查信号、密码及路由器设置';
}
function renderPing(ping) {
  if (!ping) return;
  const labels = {idle:'尚未测试',resolving:'解析域名',running:'正在 Ping',complete:'测试完成',failed:'测试失败',cancelled:'测试已取消'};
  text('ping-state', labels[ping.state] || ping.state);
  text('ping-target', `${ping.host}${ping.ip ? ' · ' + ping.ip : ''}`);
  text('ping-sent', `${ping.sent} / ${ping.total}`); text('ping-received', ping.received);
  text('ping-loss', ping.sent ? `${Number(ping.loss).toFixed(0)}%` : '--');
  text('ping-average', ping.averageMs == null ? '--' : `${Number(ping.averageMs).toFixed(1)} ms`);
  let message = '点击“Ping 百度”，通过当前已连接的 STA 网络测试 10 次。';
  if (ping.state === 'resolving') message = '已获取 IP，开发板正在通过 STA 网络解析百度域名…';
  if (ping.state === 'running') message = `开发板正在发送 ICMP 请求：${ping.sent}/${ping.total}，已收到 ${ping.received} 个回复。`;
  if (ping.state === 'complete') message = ping.sent === ping.total && ping.received === ping.total ? `测试通过：${ping.received}/${ping.total} 回复，0% 丢包。` : `测试结束：${ping.received}/${ping.sent} 回复，丢包 ${Number(ping.loss).toFixed(0)}%。未收到 ICMP 回复不等于 Wi-Fi 已断开。`;
  if (ping.state === 'failed') message = ping.error?.startsWith('DNS') ? '百度域名解析失败，请检查路由器 DNS 和外网连接。' : `测试未能开始：${ping.error || '请检查 STA 连接状态'}`;
  if (ping.state === 'cancelled') message = '当前网络连接中断，本次 Ping 已取消。';
  text('ping-result',message); $('ping-result').classList.toggle('error', ['failed','cancelled'].includes(ping.state) || ping.state === 'complete' && ping.received < ping.total);
  $('ping-log').replaceChildren();
  if (!ping.rows?.length) {
    const empty = document.createElement('p'); empty.className = 'ping-empty'; empty.textContent = ping.state === 'resolving' ? '准备网络测试…' : '等待 Ping 测试结果'; $('ping-log').append(empty);
  }
  for (const reply of ping.rows || []) {
    const row = document.createElement('div'); row.className = 'ping-row' + (reply.success ? '' : ' timeout');
    const seq = document.createElement('span'), content = document.createElement('span'); seq.textContent = `seq=${reply.seq}`;
    content.textContent = reply.success ? `来自 ${ping.ip}：${reply.bytes} 字节 · ${reply.ms} ms · TTL=${reply.ttl}` : '请求超时，未收到 ICMP 回复';
    row.append(seq,content); $('ping-log').append(row);
  }
}
async function refreshWifi() {
  if (!wifiView || wifiPolling) return wifiPolling;
  wifiPolling = (async () => {
    try {
      const data = await request('/api/wifi'); wifiStatus = data;
      text('wifi-state', wifiStates[data.state] || data.state);
      text('wifi-current-ssid', data.ssid || '--'); text('wifi-rssi', data.rssi == null ? '--' : `${data.rssi} dBm`);
      for (const field of ['ip','gateway','netmask','dns','mac']) text('wifi-' + field, data[field] || '--');
      text('wifi-channel', data.channel ? `${data.channel > 14 ? '5' : '2.4'} GHz · CH ${data.channel}` : '--');
      text('wifi-duration', data.connected ? `${data.connectedSeconds} 秒` : '--'); text('wifi-disconnects', data.disconnects || 0);
      text('wifi-mode-note', data.apEnabled ? '当前通过 AP 热点访问。扫描结果仅供查看；Ping 需要开发板已有 STA 网络连接。' : '当前通过 STA 访问网页。扫描和 Ping 使用现有网络，不切换 Wi-Fi。');
      text('wifi-ap-info', data.apEnabled ? `AP：${data.apSsid} · IP ${data.apIp} · ${data.clients} 台设备连接` : '当前为 STA 网页访问，扫描期间保留已有连接。');
      const message = data.state === 'failed' ? `连接失败 · reason=${data.reason} · ${data.error && data.error !== 'ESP_OK' ? data.error : wifiFailure(data.reason)}` : data.state === 'connecting' ? `正在连接固件中配置的 Wi-Fi · 已尝试 ${data.retries || 0} 次，失败后每秒重试${data.reason ? ' · reason=' + data.reason + ' · ' + wifiFailure(data.reason) : ''}` : data.connected ? 'STA 已连接，网络测试结果见下方百度 Ping。' : 'STA 尚未联网，请检查 menuconfig 中的 Wi-Fi 配置。';
      text('wifi-result',message); $('wifi-result').classList.toggle('error',data.state === 'failed');
      text('wifi-scan-hint', data.scanning ? `正在扫描 2.4 GHz · CH ${data.scanChannel || '--'} · ${data.scanProgress || 0}/${data.scanTotal || '--'} 个信道` : data.scanError !== 'ESP_OK' ? `扫描失败：${data.scanError}` : `2.4 GHz · 最多 24 个网络 · 当前 ${data.networks.length} 个结果，仅供查看。`);
      const key = JSON.stringify(data.networks);
      if (key !== wifiKey) {
        wifiKey = key; $('wifi-networks').replaceChildren();
        if (!data.networks.length) { const empty = document.createElement('div'); empty.className = 'test-empty'; empty.textContent = data.scanning ? '正在扫描…' : '点击“扫描 Wi-Fi”查看附近网络'; $('wifi-networks').append(empty); }
        for (const network of data.networks) {
          const item = document.createElement('div'); item.className = 'network-item';
          const title = document.createElement('span'), name = document.createElement('strong'), detail = document.createElement('small'), signal = document.createElement('span');
          name.textContent = network.ssid; detail.textContent = `2.4 GHz · CH ${network.channel} · ${network.open ? '开放网络' : '加密网络'}`; signal.textContent = `${network.rssi} dBm`;
          title.append(name,detail); item.append(title,signal);
          $('wifi-networks').append(item);
        }
      }
      renderPing(data.ping);
    } catch (error) { text('wifi-result',error.message); $('wifi-result').classList.add('error'); }
    finally { testControls(); }
  })();
  try { await wifiPolling; } finally { wifiPolling = null; }
}
async function testOperation(operation, message, output) {
  if (testBusy || !state.online) return;
  testBusy = true; testControls();
  try {
    await operation();
    if (output) { text(output,message); $(output).classList.remove('error'); }
    toast(message); if (wifiView) await refreshWifi();
  } catch (error) { if (output) { text(output,error.message); $(output).classList.add('error'); } toast(error.message,true); }
  finally { testBusy = false; testControls(); }
}
$('wifi-scan').addEventListener('click', () => testOperation(() => request('/api/wifi/scan',{method:'POST'}),'已开始扫描 Wi-Fi','wifi-result'));
$('wifi-ping').addEventListener('click', () => testOperation(() => request('/api/wifi/ping',{method:'POST'}),'已开始 Ping 百度','ping-result'));
if (wifiView) { refreshWifi(); setInterval(() => { if (!document.hidden) refreshWifi(); },1500); }
setInterval(testControls, 500);
