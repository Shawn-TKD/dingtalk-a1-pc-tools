import { useEffect, useMemo, useState } from "react";
import AudioPlayer from "./AudioPlayer";
import { ChevronIcon, DownloadIcon, InfoIcon, PlayIcon, RefreshIcon, TrashIcon } from "./icons";

function loadAccessToken() {
  const queryToken = new URLSearchParams(window.location.search).get("token") || "";
  try {
    if (queryToken) sessionStorage.setItem("a1-console-token", queryToken);
    const storedToken = sessionStorage.getItem("a1-console-token") || "";
    if (queryToken) window.history.replaceState(null, "", `${window.location.pathname}${window.location.hash}`);
    return queryToken || storedToken;
  } catch {
    return queryToken;
  }
}

const ACCESS_TOKEN = loadAccessToken();

function protectedUrl(path) {
  if (!path || !ACCESS_TOKEN) return path;
  const url = new URL(path, window.location.origin);
  url.searchParams.set("token", ACCESS_TOKEN);
  return `${url.pathname}${url.search}`;
}

function apiFetch(path, options = {}) {
  const headers = new Headers(options.headers || {});
  if (ACCESS_TOKEN) headers.set("X-A1-Access-Token", ACCESS_TOKEN);
  return fetch(path, { ...options, headers });
}

async function responseError(response, fallback) {
  try {
    const body = await response.json();
    return body.error || fallback;
  } catch {
    return fallback;
  }
}

function formatRecordingTime(epoch) {
  const date = new Date(epoch * 1000);
  const now = new Date();
  const sameDay = date.toDateString() === now.toDateString();
  const time = date.toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit", hour12: false });
  return sameDay ? `今天 ${time}` : date.toLocaleString("zh-CN", { month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false });
}

function formatDuration(seconds) {
  const value = Math.max(0, Math.floor(seconds || 0));
  return `${String(Math.floor(value / 60)).padStart(2, "0")}:${String(value % 60).padStart(2, "0")}`;
}

function formatStorageGb(megabytes) {
  const value = Number(megabytes);
  return Number.isFinite(value) ? (value / 1024).toFixed(2) : "—";
}

function DeviceMark() {
  return (
    <div className="device-mark" aria-hidden="true">
      <span className="device-light" />
      <span className="device-mic">•••<br />••••<br />•••</span>
    </div>
  );
}

function DeviceOverview({ device }) {
  const items = [
    ["电量", device ? `${device.battery_percent}%` : "—"],
    ["固件版本", device?.version?.replace(/-.*$/, "") || "—"],
    ["存储空间", device ? `${formatStorageGb(device.storage_remain)} / ${formatStorageGb(device.storage_total_size)} GB` : "—"],
    ["状态", device?.audio_status === "idle" ? "空闲" : device?.audio_status || "—"],
  ];
  return (
    <section className="device-overview" aria-label="设备信息">
      <div className="device-identity">
        <DeviceMark />
        <div>
          <h2>DingTalk A1</h2>
          <p>{device?.serial_number || "等待连接设备"}</p>
        </div>
      </div>
      <div className="device-facts">
        {items.map(([label, value]) => (
          <div className="device-fact" key={label}>
            <span>{label}</span>
            <strong className={label === "电量" ? "battery-value" : ""}>{value}</strong>
          </div>
        ))}
      </div>
    </section>
  );
}

function RecordingRow({ recording, busyFid, busyKind, onDownload, onDelete }) {
  const local = Boolean(recording.local_url);
  const busy = busyFid === recording.fid;
  const onDevice = recording.on_device !== false;
  const isMemo = recording.kind === "voice_memo";
  const canDelete = onDevice || isMemo;
  const hasBackup = local && Boolean(recording.local_dtyj_bytes);
  const mediaUrl = protectedUrl(recording.local_url);
  return (
    <div className="recording-row">
      <div className="recording-time" data-label="录制时间">{formatRecordingTime(recording.fid)}</div>
      <div data-label="时长">{onDevice || isMemo ? formatDuration(recording.duration_seconds) : "—"}</div>
      <div data-label="状态">{isMemo ? "语音备忘录" : onDevice ? "设备内" : "仅本地"}</div>
      <div className="local-file" data-label="本地文件">
        {local ? (
          <>
            {isMemo && <span className="memo-badge">语音备忘录</span>}
            <span className="file-meta">{onDevice || isMemo ? `OGG · ${Number(recording.local_duration || recording.duration_seconds).toFixed(2)} 秒` : "OGG · 本地备份"}</span>
            <AudioPlayer src={mediaUrl} fallbackDuration={recording.local_duration || recording.duration_seconds} />
            {recording.markers?.length > 0 && (
              <div className="recording-markers">
                {recording.markers.map((marker, index) => (
                  <span key={`${marker.relative_seconds}-${index}`}>标记 {formatDuration(marker.relative_seconds)}</span>
                ))}
              </div>
            )}
            {recording.transcription && <p className="memo-transcription">{recording.transcription}</p>}
            {recording.transcription_error && <p className="memo-transcription-error">转录失败，可稍后重试</p>}
          </>
        ) : (
          <span className="not-downloaded">未下载</span>
        )}
      </div>
      <div className="row-actions" data-label="操作">
        <button type="button" className="text-action" disabled={busy || local} onClick={() => onDownload(recording.fid)}>
          <DownloadIcon /> {busy && busyKind === "download" ? "下载中" : local ? "已下载" : "下载"}
        </button>
        <a className={local ? "text-action" : "text-action disabled"} href={local ? mediaUrl : undefined} target="_blank" rel="noreferrer" aria-disabled={!local}>
          <PlayIcon /> 播放
        </a>
        <button
          type="button"
          className={hasBackup ? "text-action delete-action" : "text-action delete-action no-backup"}
          disabled={busy || !canDelete}
          title={isMemo ? "永久删除电脑上的语音备忘录" : hasBackup ? "删除设备内录音，本地备份会保留" : onDevice ? "没有本地备份，删除后无法恢复" : "该录音已不在设备内"}
          onClick={() => onDelete(recording)}
        >
          <TrashIcon /> {busy && busyKind === "delete" ? "删除中" : "删除"}
        </button>
      </div>
    </div>
  );
}

function DeleteDialog({ recording, busy, onCancel, onConfirm }) {
  if (!recording) return null;
  const isMemo = recording.kind === "voice_memo";
  const hasBackup = Boolean(recording.local_url && recording.local_dtyj_bytes);
  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="delete-dialog" role="dialog" aria-modal="true" aria-labelledby="delete-title">
        <span className="dialog-kicker">危险操作</span>
        <h2 id="delete-title">{isMemo ? "删除本地语音备忘录？" : "删除设备内录音？"}</h2>
        {isMemo ? (
          <p className="no-backup-warning"><strong>这会永久删除电脑上的 Ogg 文件和转录文本。</strong> 删除后当前工具无法恢复。</p>
        ) : hasBackup ? (
          <p>{formatRecordingTime(recording.fid)} 的录音将从 A1 永久删除，电脑上的 DTYJ 与 OGG 备份会保留。</p>
        ) : (
          <p className="no-backup-warning"><strong>这条录音尚未下载到电脑。</strong> 删除后没有本地副本，当前工具无法恢复或写回 A1。</p>
        )}
        <div className="dialog-actions">
          <button type="button" className="dialog-cancel" disabled={busy} onClick={onCancel}>取消</button>
          <button type="button" className="dialog-delete" disabled={busy} onClick={onConfirm}>
            {busy ? "正在删除" : "确认永久删除"}
          </button>
        </div>
      </section>
    </div>
  );
}

function EventLog({ events, open, onToggle, onClear }) {
  return (
    <section className={open ? "event-log open" : "event-log"}>
      <div className="event-log-head">
        <button type="button" onClick={onToggle}>事件日志 <ChevronIcon open={open} /></button>
        <button type="button" className="clear-log" onClick={onClear}>清空日志</button>
      </div>
      {open && (
        <div className="event-entries">
          {events.length ? events.map((event, index) => (
            <div className="event-entry" key={`${event.time}-${index}`}>
              <time>{event.time}</time><span>{event.message}</span>
            </div>
          )) : <p className="empty-log">暂无事件</p>}
        </div>
      )}
    </section>
  );
}

export default function App() {
  const [state, setState] = useState({ connected: false, device: null, recordings: [] });
  const [busy, setBusy] = useState(false);
  const [busyFid, setBusyFid] = useState(null);
  const [busyKind, setBusyKind] = useState(null);
  const [error, setError] = useState("");
  const [events, setEvents] = useState([]);
  const [logOpen, setLogOpen] = useState(true);
  const [pendingDelete, setPendingDelete] = useState(null);

  function addEvent(message) {
    setEvents((current) => [...current, { time: new Date().toLocaleTimeString("zh-CN", { hour12: false }), message }]);
  }

  async function loadState() {
    const response = await apiFetch("/api/state");
    if (!response.ok) throw new Error(await responseError(response, "无法读取本机服务状态"));
    setState(await response.json());
  }

  async function refreshDevice() {
    setBusy(true);
    setError("");
    addEvent("正在扫描 A1");
    try {
      const response = await apiFetch("/api/refresh", { method: "POST" });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error || "连接失败");
      setState(body);
      addEvent("已连接设备");
      addEvent("鉴权成功");
      addEvent(`发现 ${body.recordings.filter((recording) => recording.on_device !== false).length} 条设备录音`);
    } catch (reason) {
      setError(reason.message);
      addEvent(`连接失败：${reason.message}`);
    } finally {
      setBusy(false);
    }
  }

  async function downloadRecording(fid) {
    setBusyFid(fid);
    setBusyKind("download");
    setError("");
    addEvent(`开始下载录音 ${fid}`);
    try {
      const response = await apiFetch("/api/download", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ fid }),
      });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error || "下载失败");
      setState(body.state);
      addEvent(`录音 ${fid} 已下载并转换为 OGG`);
    } catch (reason) {
      setError(reason.message);
      addEvent(`下载失败：${reason.message}`);
    } finally {
      setBusyFid(null);
      setBusyKind(null);
    }
  }

  function requestDelete(recording) {
    setPendingDelete(recording);
  }

  async function deleteRecording() {
    if (!pendingDelete) return;
    const recording = pendingDelete;
    setBusyFid(recording.fid);
    setBusyKind("delete");
    setError("");
    addEvent(`请求删除${recording.kind === "voice_memo" ? "语音备忘录" : "设备内录音"} ${recording.fid}`);
    try {
      const response = await apiFetch("/api/delete", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ fid: recording.fid, confirmed: true }),
      });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error || "删除失败");
      setState(body.state);
      setPendingDelete(null);
      addEvent(recording.kind === "voice_memo" ? `语音备忘录 ${recording.fid} 已删除` : `设备内录音 ${recording.fid} 已删除；本地备份仍保留`);
    } catch (reason) {
      setError(reason.message);
      addEvent(`删除失败：${reason.message}`);
    } finally {
      setBusyFid(null);
      setBusyKind(null);
    }
  }

  useEffect(() => {
    loadState().catch((reason) => setError(reason.message));
    const timer = window.setInterval(() => {
      loadState().catch(() => {});
    }, 1500);
    return () => window.clearInterval(timer);
  }, []);

  const recordings = useMemo(() => [...state.recordings].sort((a, b) => b.fid - a.fid), [state.recordings]);
  const deviceRecordingCount = recordings.filter((recording) => recording.on_device !== false).length;
  const localBackupCount = recordings.filter((recording) => recording.local_url).length;

  return (
    <main className="app-shell">
      <header className="topbar">
        <h1>A1 本地控制台</h1>
        <div className="connection-actions">
          <span className={state.connected ? "connection-state online" : "connection-state"}>
            <i /> {state.connected ? "已连接" : "未连接"}
          </span>
          <button type="button" className="refresh-button" disabled={busy} onClick={refreshDevice}>
            <RefreshIcon /> {busy ? "连接中" : state.connected ? "重新读取" : "连接设备"}
          </button>
        </div>
      </header>

      <DeviceOverview device={state.device} />

      <section className="recordings-section">
        <div className="section-title">
          <h2>录音</h2><span>{deviceRecordingCount} 条设备录音 · {localBackupCount} 个本地备份</span>
        </div>
        {error && <div className="error-banner" role="alert">{error}</div>}
        <div className="recording-table">
          <div className="recording-head">
            <span>录制时间 ↓</span><span>时长</span><span>状态</span><span>本地文件</span><span>操作</span>
          </div>
          {recordings.length ? recordings.map((recording) => (
            <RecordingRow
              key={recording.fid}
              recording={recording}
              busyFid={busyFid}
              busyKind={busyKind}
              onDownload={downloadRecording}
              onDelete={requestDelete}
            />
          )) : (
            <div className="empty-state">
              <strong>还没有读取录音列表</strong>
              <span>开启 A1 后点击“连接设备”；若手机正在占用 A1，请先断开手机蓝牙连接。</span>
            </div>
          )}
        </div>
      </section>

      <div className="safety-note"><InfoIcon /> 设备内录音均可在一次确认后删除；没有本地备份时会明确提示无法恢复。已有本地备份不会被删除。</div>
      <EventLog events={events} open={logOpen} onToggle={() => setLogOpen((value) => !value)} onClear={() => setEvents([])} />
      <DeleteDialog
        recording={pendingDelete}
        busy={busyKind === "delete"}
        onCancel={() => setPendingDelete(null)}
        onConfirm={deleteRecording}
      />
    </main>
  );
}
