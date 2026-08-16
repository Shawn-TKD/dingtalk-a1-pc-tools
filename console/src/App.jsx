import { useEffect, useMemo, useState } from "react";
import AudioPlayer from "./AudioPlayer";
import { ChevronIcon, DownloadIcon, InfoIcon, PlayIcon, RefreshIcon } from "./icons";

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
    ["存储空间", device ? `${device.storage_remain} / ${device.storage_total_size} MB` : "—"],
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

function RecordingRow({ recording, busyFid, onDownload }) {
  const local = Boolean(recording.local_url);
  const busy = busyFid === recording.fid;
  return (
    <div className="recording-row">
      <div className="recording-time" data-label="录制时间">{formatRecordingTime(recording.fid)}</div>
      <div data-label="时长">{formatDuration(recording.duration_seconds)}</div>
      <div data-label="状态">设备内</div>
      <div className="local-file" data-label="本地文件">
        {local ? (
          <>
            <span className="file-meta">OGG · {Number(recording.local_duration || recording.duration_seconds).toFixed(2)} 秒</span>
            <AudioPlayer src={recording.local_url} fallbackDuration={recording.local_duration || recording.duration_seconds} />
          </>
        ) : (
          <span className="not-downloaded">未下载</span>
        )}
      </div>
      <div className="row-actions" data-label="操作">
        <button type="button" className="text-action" disabled={busy} onClick={() => onDownload(recording.fid)}>
          <DownloadIcon /> {busy ? "下载中" : local ? "重新下载" : "下载"}
        </button>
        <a className={local ? "text-action" : "text-action disabled"} href={local ? recording.local_url : undefined} target="_blank" rel="noreferrer" aria-disabled={!local}>
          <PlayIcon /> 播放
        </a>
      </div>
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
  const [error, setError] = useState("");
  const [events, setEvents] = useState([]);
  const [logOpen, setLogOpen] = useState(true);

  function addEvent(message) {
    setEvents((current) => [...current, { time: new Date().toLocaleTimeString("zh-CN", { hour12: false }), message }]);
  }

  async function loadState() {
    const response = await fetch("/api/state");
    if (!response.ok) throw new Error("无法读取本机服务状态");
    setState(await response.json());
  }

  async function refreshDevice() {
    setBusy(true);
    setError("");
    addEvent("正在扫描 A1");
    try {
      const response = await fetch("/api/refresh", { method: "POST" });
      const body = await response.json();
      if (!response.ok) throw new Error(body.error || "连接失败");
      setState(body);
      addEvent("已连接设备");
      addEvent("鉴权成功");
      addEvent(`发现 ${body.recordings.length} 条录音`);
    } catch (reason) {
      setError(reason.message);
      addEvent(`连接失败：${reason.message}`);
    } finally {
      setBusy(false);
    }
  }

  async function downloadRecording(fid) {
    setBusyFid(fid);
    setError("");
    addEvent(`开始下载录音 ${fid}`);
    try {
      const response = await fetch("/api/download", {
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
    }
  }

  useEffect(() => {
    loadState().catch((reason) => setError(reason.message));
  }, []);

  const recordings = useMemo(() => [...state.recordings].sort((a, b) => b.fid - a.fid), [state.recordings]);

  return (
    <main className="app-shell">
      <header className="topbar">
        <h1>A1 本地控制台</h1>
        <div className="connection-actions">
          <span className={state.connected ? "connection-state online" : "connection-state"}>
            <i /> {state.connected ? "已连接" : "未连接"}
          </span>
          <button type="button" className="refresh-button" disabled={busy} onClick={refreshDevice}>
            <RefreshIcon /> {busy ? "连接中" : "刷新设备"}
          </button>
        </div>
      </header>

      <DeviceOverview device={state.device} />

      <section className="recordings-section">
        <div className="section-title">
          <h2>录音</h2><span>{recordings.length} 条</span>
        </div>
        {error && <div className="error-banner" role="alert">{error}</div>}
        <div className="recording-table">
          <div className="recording-head">
            <span>录制时间 ↓</span><span>时长</span><span>状态</span><span>本地文件</span><span>操作</span>
          </div>
          {recordings.length ? recordings.map((recording) => (
            <RecordingRow key={recording.fid} recording={recording} busyFid={busyFid} onDownload={downloadRecording} />
          )) : (
            <div className="empty-state">
              <strong>还没有读取录音列表</strong>
              <span>开启 A1 后点击“刷新设备”。</span>
            </div>
          )}
        </div>
      </section>

      <div className="safety-note"><InfoIcon /> 仅执行读取、下载和播放；不会删除或修改设备录音。</div>
      <EventLog events={events} open={logOpen} onToggle={() => setLogOpen((value) => !value)} onClear={() => setEvents([])} />
    </main>
  );
}
