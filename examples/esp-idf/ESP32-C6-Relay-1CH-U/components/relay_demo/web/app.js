'use strict';
const $ = id => document.getElementById(id);
const text = (id, value) => { $(id).textContent = value; };
const pad = value => String(value).padStart(2, '0');
const state = {online:false, busy:false, status:null, events:[], rtcEpoch:null, polling:null, tasksPolling:null, tasksKey:null};
const weekdays = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
let toastTimer;
const UI_VERSION = '20261028-c6-r2';
const scheduleView = location.pathname === '/RTC_Event';
const wifiView = location.pathname === '/WiFi_Test';
let currentBoot = null;
document.body.classList.toggle('schedule-view', scheduleView);
document.body.classList.toggle('wifi-view', wifiView);
$(wifiView ? 'nav-wifi' : scheduleView ? 'nav-schedules' : 'nav-overview').classList.add('active');
if (wifiView) {
  text('page-title', 'Wi-Fi Diagnostics');
  text('breadcrumb', 'Wi-Fi Diagnostics');
  text('page-subtitle', 'View network status, scan for nearby networks, and run ping tests.');
}
if (scheduleView) {
  text('page-title', 'Schedules'); text('breadcrumb', 'Schedules');
  text('page-subtitle', 'Schedule when the relay turns on or off.');
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
      throw new Error(detail.includes('ESP_ERR') ? 'Device operation failed. ' + detail : 'Request failed. Check the device connection and your input.');
    }
    return (response.headers.get('content-type') || '').includes('application/json') ? await response.json() : await response.text();
  } catch (error) {
    if (error.name === 'AbortError' || error instanceof TypeError) throw new Error('Unable to reach the device. Check your Wi-Fi connection.');
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
  return days ? `${days}d ${pad(hours)}h` : `${pad(hours)}h ${pad(minutes)}m`;
}
function renderStatus(status) {
  state.status = status;
  $('relay-card').classList.toggle('on', status.relay);
  $('toggle').setAttribute('aria-checked', String(status.relay));
  text('relay-label', status.relay ? 'On' : 'Off');
  text('relay-description', status.relay ? 'CH1 output is on' : 'CH1 output is off');
  text('network-mode', status.mode === 'AP' ? 'AP Mode' : 'STA Mode');
  const detail = status.connected ? (status.mode === 'AP' ? `${status.ssid} · ${status.clients} connected` : `${status.ssid} · ${status.rssi ?? '--'} dBm`) : 'Waiting for Wi-Fi connection';
  text('network-detail', detail);
  $('network-detail').title = `${detail} · IP ${status.ip}`;
  $('network-dot').classList.toggle('on', status.connected);
  text('uptime', uptime(status.uptime));
  text('board-name', status.board);
  text('clock-source', status.rtcReadOk ? 'Onboard RTC' : 'RTC read failed');
  text('clock-detail', !status.rtcReadOk ? (status.rtcReadError || 'Check the RTC connection') :
    !status.rtcCalendarValid ? 'Date is uninitialized. Showing RTC values.' :
    status.rtcOscillatorStopped ? 'The oscillator stopped. The RTC value was kept.' :
    status.rtcStopped ? 'The RTC is stopped' : 'Time comes from the onboard RTC');
  const startup = {'initialized-1970':'Initialized to 1970 at boot after an oscillator stop or invalid date','retained':'RTC time kept at boot','initialization-failed':'Failed to initialize the RTC time','read-failed':'RTC read failed at boot'}[status.rtcStartupAction] || '';
  text('rtc-register-state', status.rtcReadOk ? `OS=${status.rtcOscillatorStopped ? 1 : 0} · STOP=${status.rtcStopped ? 1 : 0} · ${startup}` : 'RTC register read failed');
  $('rtc-register-state').title = 'RTC registers 00h–0Ah · ' + (status.rtcRegisters || '--');
  const offset = status.utcOffset || '+0800';
  text('timezone', `UTC${offset.slice(0,3)}:${offset.slice(3)}`);
  text('sync-hint', !status.rtcReadOk ? 'RTC read failed. Retrying…' :
    !status.rtcCalendarValid ? 'The RTC date is uninitialized. Sync the clock to set it.' :
    status.rtcStopped ? 'The RTC is stopped. Sync the clock to start it.' :
    status.rtcOscillatorStopped ? 'The oscillator stop flag is set. Sync the clock to update the displayed RTC time.' :
    'Sync copies this phone or computer’s time to the RTC.');
  const fields = status.rtcReadOk ? rtcFields(status.rtc) : null;
  if (fields) {
    state.rtcEpoch = status.rtcCalendarValid ? Date.UTC(fields.year, fields.month - 1, fields.day, fields.hour, fields.minute, fields.second) : null;
    text('rtc-time', `${pad(fields.hour)}:${pad(fields.minute)}:${pad(fields.second)}`);
    const weekday = Number.isInteger(status.rtcWeekday) ? weekdays[status.rtcWeekday] : '';
    text('rtc-date', `${fields.year}-${pad(fields.month)}-${pad(fields.day)}${weekday ? ' · ' + weekday : ''}`);
  } else {
    state.rtcEpoch = null;
    text('rtc-time', '--:--:--'); text('rtc-date', 'RTC read failed. Retrying…');
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
    const name = document.createElement('strong'); name.textContent = `Schedule ${pad(event.id)}`;
    cell(row, name);
    const date = document.createElement('div');
    const primary = document.createElement('strong'); primary.textContent = event.datetime.split('T')[1];
    const secondary = document.createElement('small'); secondary.textContent = event.datetime.split('T')[0];
    date.append(primary, secondary); cell(row, date);
    const action = document.createElement('span');
    action.className = 'action-pill' + (event.action === 0 ? ' off' : event.action === 2 ? ' retain' : '');
    action.textContent = ['Turn CH1 Off','Turn CH1 On','Keep Current State'][event.action] || '--'; cell(row, action);
    cell(row, ['Once','Daily','Weekly on ' + weekdays[event.weekday],'Monthly'][event.repeat] || '--');
    const remove = document.createElement('button'); remove.className = 'delete-event'; remove.textContent = 'Delete';
    remove.setAttribute('aria-label', `Delete schedule ${event.id}`);
    remove.addEventListener('click', () => actionRequest(() => request(`/DeleteEvent?id=${event.id}`), 'Schedule deleted'));
    cell(row, remove, 'align-right'); $('event-list').append(row);
  }
}
async function refresh() {
  if (state.polling) return state.polling;
  state.polling = (async () => {
    try {
      const status = await request('/api/status');
      if (status.uiVersion !== UI_VERSION || !status.bootId) throw new Error('The page and firmware versions do not match. Reflash the device and reload the page.');
      if (currentBoot && currentBoot !== status.bootId) {
        clearRtc('Device restarted. Reading the RTC…');
      }
      currentBoot = status.bootId;
      text('firmware-version', status.uiVersion);
      text('boot-id', ' · Boot ' + status.bootId);
      state.online = true;
      renderStatus(status);
      $('device-connection').classList.remove('offline');
      $('device-connection').querySelector('span').textContent = 'Device online · ' + status.ip;
      text('last-update', status.rtcReadOk ? 'Last RTC read ' + status.rtc.replace('T',' ') : 'RTC read failed');
    } catch (error) {
      state.online = false;
      $('device-connection').classList.add('offline');
      $('device-connection').querySelector('span').textContent = 'Device disconnected';
      clearRtc('Device disconnected. Waiting to read the RTC again.');
      text('last-update', error.message);
      text('sync-hint', 'The RTC will be read again when the connection returns.');
    } finally { updateControls(); }
  })();
  try { await state.polling; } finally { state.polling = null; }
}
async function refreshTasks() {
  if (state.tasksPolling) return state.tasksPolling;
  state.tasksPolling = (async () => {
    try {
      const tasks = await request('/getTimeAndEvent');
      if (!Array.isArray(tasks.events)) throw new Error('The device page version does not match.');
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
$('toggle').addEventListener('click', () => actionRequest(() => request('/Switch1'), 'Relay status updated'));
$('relay-on').addEventListener('click', () => actionRequest(() => request('/RelayOn'), 'Relay turned on'));
$('relay-off').addEventListener('click', () => actionRequest(() => request('/RelayOff'), 'Relay turned off'));
$('refresh').addEventListener('click', async () => { await refresh(); if (wifiView) await refreshWifi(); toast(state.online ? 'Device status updated' : 'Unable to reach the device', !state.online); });
$('sync-time').addEventListener('click', () => actionRequest(() => request('/api/time', {method:'POST', headers:{'Content-Type':'text/plain'}, body:String(Math.floor(Date.now()/1000))}), 'RTC synchronized'));
function openDialog() {
  text('form-error', '');
  if (state.rtcEpoch === null) {
    $('event-date').value = ''; $('event-time').value = '';
    text('form-error', 'The RTC is not set. Sync the clock or choose a time manually.');
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
  if (!Number.isFinite(parsed.getTime())) { text('form-error','Enter a valid date and time.'); return; }
  if (repeat === 0 && state.rtcEpoch !== null && parsed.getTime() <= state.rtcEpoch) {
    text('form-error', 'Choose a time after the current device time.'); return;
  }
  const data = `Date: ${date.replaceAll('-','/')} Week: ${parsed.getUTCDay()} Time: ${time} Relay CH1: ${$('event-action').value} Cycle: ${repeat}`;
  state.busy = true; updateControls(); $('save-event').disabled = true;
  try {
    await request('/NewEvent?' + new URLSearchParams({data}));
    $('event-dialog').close();
    if (state.polling) await state.polling;
    await Promise.all([refresh(), refreshTasks()]); toast('Schedule added');
  } catch (error) { text('form-error', error.message); }
  finally { state.busy = false; $('save-event').disabled = false; updateControls(); }
});
queueMicrotask(() => { refresh(); if (!wifiView) refreshTasks(); });
setInterval(() => { if (!document.hidden) refresh(); }, 1000);
setInterval(() => { if (!document.hidden && !wifiView) refreshTasks(); }, 3000);
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) { clearRtc('Reading the RTC again…'); refresh(); if (!wifiView) refreshTasks(); }
});

function clearRtc(message) {
  state.rtcEpoch = null;
  text('rtc-time', '--:--:--'); text('rtc-date', message);
  text('clock-source', 'Waiting for RTC'); text('clock-detail', message);
  text('rtc-register-state', 'Waiting for a valid RTC response');
}
window.addEventListener('pageshow', event => {
  if (event.persisted) { clearRtc('Reading the RTC again…'); refresh(); }
});
window.addEventListener('offline', () => {
  state.online = false; clearRtc('Network disconnected. Waiting to read the RTC again.'); updateControls();
});
window.addEventListener('online', () => refresh());
/* Read-only scan results and manual Ping on the existing STA connection. */
let wifiPolling = null, wifiStatus = null, wifiKey = null, testBusy = false;
const wifiStates = {idle:'Not connected',connecting:'Connecting',connected:'IP assigned',disconnected:'Disconnected',failed:'Connection failed'};
function testControls() {
  const disabled = !state.online || testBusy || !wifiStatus || wifiStatus.scanning || wifiStatus.ping?.busy;
  $('wifi-scan').disabled = disabled || wifiStatus?.state === 'connecting';
  $('wifi-ping').disabled = disabled || !wifiStatus?.connected;
}
function wifiFailure(reason) {
  const reasons = {2:'Authentication expired',15:'Handshake timed out. Check the password.',201:'Network not found',202:'Authentication failed. Check the password.',203:'Association failed',204:'Handshake timed out',205:'Connection failed',210:'Security settings do not match'};
  return reasons[reason] || 'Check the signal, password, and router settings.';
}
function renderPing(ping) {
  if (!ping) return;
  const labels = {idle:'Not started',resolving:'Resolving hostname',running:'Pinging',complete:'Complete',failed:'Failed',cancelled:'Cancelled'};
  text('ping-state', labels[ping.state] || ping.state);
  text('ping-target', `${ping.host}${ping.ip ? ' · ' + ping.ip : ''}`);
  text('ping-sent', `${ping.sent} / ${ping.total}`); text('ping-received', ping.received);
  text('ping-loss', ping.sent ? `${Number(ping.loss).toFixed(0)}%` : '--');
  text('ping-average', ping.averageMs == null ? '--' : `${Number(ping.averageMs).toFixed(1)} ms`);
  let message = 'Select Ping Baidu to send 10 requests over the current STA connection.';
  if (ping.state === 'resolving') message = 'IP assigned. Resolving the Baidu hostname over the STA connection…';
  if (ping.state === 'running') message = `Sending ICMP requests · ${ping.sent}/${ping.total} sent · ${ping.received} replies.`;
  if (ping.state === 'complete') message = ping.sent === ping.total && ping.received === ping.total ? `Passed · ${ping.received}/${ping.total} replies · 0% packet loss.` : `Completed · ${ping.received}/${ping.sent} replies · ${Number(ping.loss).toFixed(0)}% packet loss. The target may block ICMP replies.`;
  if (ping.state === 'failed') message = ping.error?.startsWith('DNS') ? 'Could not resolve the Baidu hostname. Check router DNS and internet access.' : `Could not start the test. ${ping.error || 'Check the STA connection.'}`;
  if (ping.state === 'cancelled') message = 'Ping cancelled after the network disconnected.';
  text('ping-result',message); $('ping-result').classList.toggle('error', ['failed','cancelled'].includes(ping.state) || ping.state === 'complete' && ping.received < ping.total);
  $('ping-log').replaceChildren();
  if (!ping.rows?.length) {
    const empty = document.createElement('p'); empty.className = 'ping-empty'; empty.textContent = ping.state === 'resolving' ? 'Preparing the ping test…' : 'Waiting for ping results'; $('ping-log').append(empty);
  }
  for (const reply of ping.rows || []) {
    const row = document.createElement('div'); row.className = 'ping-row' + (reply.success ? '' : ' timeout');
    const seq = document.createElement('span'), content = document.createElement('span'); seq.textContent = `seq=${reply.seq}`;
    content.textContent = reply.success ? `From ${ping.ip} · ${reply.bytes} bytes · ${reply.ms} ms · TTL=${reply.ttl}` : 'Request timed out without an ICMP reply';
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
      text('wifi-duration', data.connected ? `${data.connectedSeconds} s` : '--'); text('wifi-disconnects', data.disconnects || 0);
      text('wifi-mode-note', data.apEnabled ? 'The device is in AP mode. Scanning lists nearby networks. Ping requires a STA connection.' : 'The device is in STA mode. Scanning and pinging keep the existing Wi-Fi connection.');
      text('wifi-ap-info', data.apEnabled ? `AP ${data.apSsid} · IP ${data.apIp} · ${data.clients} connected` : 'The STA connection stays active during scans.');
      const message = data.state === 'failed' ? `Connection failed · reason=${data.reason} · ${data.error && data.error !== 'ESP_OK' ? data.error : wifiFailure(data.reason)}` : data.state === 'connecting' ? `Connecting to the configured Wi-Fi network · ${data.retries || 0} attempts · Retries every second${data.reason ? ' · reason=' + data.reason + ' · ' + wifiFailure(data.reason) : ''}` : data.connected ? 'STA connected. See the Baidu ping results below.' : 'STA is disconnected. Check the Wi-Fi settings in menuconfig.';
      text('wifi-result',message); $('wifi-result').classList.toggle('error',data.state === 'failed');
      text('wifi-scan-hint', data.scanning ? `Scanning 2.4 GHz · CH ${data.scanChannel || '--'} · ${data.scanProgress || 0}/${data.scanTotal || '--'} channels` : data.scanError !== 'ESP_OK' ? `Scan failed. ${data.scanError}` : `2.4 GHz · ${data.networks.length} networks found · Up to 24 shown`);
      const key = JSON.stringify(data.networks);
      if (key !== wifiKey) {
        wifiKey = key; $('wifi-networks').replaceChildren();
        if (!data.networks.length) { const empty = document.createElement('div'); empty.className = 'test-empty'; empty.textContent = data.scanning ? 'Scanning…' : 'Select Scan Wi-Fi to find nearby networks.'; $('wifi-networks').append(empty); }
        for (const network of data.networks) {
          const item = document.createElement('div'); item.className = 'network-item';
          const title = document.createElement('span'), name = document.createElement('strong'), detail = document.createElement('small'), signal = document.createElement('span');
          name.textContent = network.ssid; detail.textContent = `2.4 GHz · CH ${network.channel} · ${network.open ? 'Open network' : 'Secured network'}`; signal.textContent = `${network.rssi} dBm`;
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
$('wifi-scan').addEventListener('click', () => testOperation(() => request('/api/wifi/scan',{method:'POST'}),'Wi-Fi scan started','wifi-result'));
$('wifi-ping').addEventListener('click', () => testOperation(() => request('/api/wifi/ping',{method:'POST'}),'Baidu ping test started','ping-result'));
if (wifiView) { refreshWifi(); setInterval(() => { if (!document.hidden) refreshWifi(); },1500); }
setInterval(testControls, 500);
