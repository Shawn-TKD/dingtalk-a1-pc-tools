import { useEffect, useMemo, useState } from "react";
import AudioPlayer from "./AudioPlayer";
import { ArchiveIcon, DownloadIcon, FileIcon, MicIcon, RefreshIcon, SettingsIcon, SparklesIcon, StopIcon, TrashIcon } from "./icons";

function loadAccessToken() {
  const query = new URLSearchParams(window.location.search).get("token") || "";
  try {
    if (query) sessionStorage.setItem("a1-console-token", query);
    const stored = sessionStorage.getItem("a1-console-token") || "";
    if (query) window.history.replaceState(null, "", window.location.pathname);
    return query || stored;
  } catch { return query; }
}

const ACCESS_TOKEN = loadAccessToken();

function apiFetch(path, options = {}) {
  const headers = new Headers(options.headers || {});
  if (ACCESS_TOKEN) headers.set("X-A1-Access-Token", ACCESS_TOKEN);
  return fetch(path, { ...options, headers });
}

function protectedUrl(path) {
  if (!path || !ACCESS_TOKEN) return path;
  const url = new URL(path, window.location.origin);
  url.searchParams.set("token", ACCESS_TOKEN);
  return `${url.pathname}${url.search}`;
}

async function jsonRequest(path, payload) {
  const response = await apiFetch(path, {
    method: "POST",
    headers: payload === undefined ? {} : { "Content-Type": "application/json" },
    body: payload === undefined ? undefined : JSON.stringify(payload),
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(body.error || "操作失败");
  return body;
}

function formatDate(epoch, full = false) {
  const value = new Date(Number(epoch) * 1000);
  if (!Number.isFinite(value.getTime())) return "未知时间";
  return value.toLocaleString("zh-CN", full
    ? { year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false }
    : { month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false });
}

function formatDuration(seconds) {
  const value = Math.max(0, Math.floor(Number(seconds) || 0));
  return `${String(Math.floor(value / 60)).padStart(2, "0")}:${String(value % 60).padStart(2, "0")}`;
}

function DeviceMark() {
  return <span className="device-mark" aria-hidden="true"><i /><b>•••<br />••••<br />•••</b></span>;
}

function Sidebar() {
  return <aside className="sidebar">
    <a className="brand" href="#top"><DeviceMark /><span>A1<br />声音工作台</span></a>
    <nav><a className="active" href="#recordings"><FileIcon /><span>全部录音</span></a><a href="#memos"><MicIcon /><span>语音备忘录</span></a><a href="#local"><ArchiveIcon /><span>本地文件</span></a></nav>
    <p className="local-first"><i />本地服务运行中</p>
  </aside>;
}

function DeviceStrip({ state }) {
  const device = state.device;
  const remain = Number(device?.storage_remain);
  const total = Number(device?.storage_total_size);
  return <section className="device-strip">
    <div><DeviceMark /><span><strong>DingTalk A1</strong><small>{device?.serial_number || "等待读取设备"}</small></span></div>
    <dl><div><dt>电量</dt><dd className="green">{device ? `${device.battery_percent}%` : "—"}</dd></div><div><dt>固件</dt><dd>{device?.version?.replace(/-.*$/, "") || "—"}</dd></div><div><dt>可用空间</dt><dd>{Number.isFinite(remain) ? `${(remain / 1024).toFixed(2)} GB` : "—"}<small>{Number.isFinite(total) ? ` / ${(total / 1024).toFixed(2)} GB` : ""}</small></dd></div><div><dt>设备状态</dt><dd>{device?.audio_status === "idle" ? "空闲" : device?.audio_status || "—"}</dd></div></dl>
  </section>;
}

function RecordingList({ recordings, selectedFid, busy, onSelect }) {
  return <section className="recording-list" id="recordings">
    <div className="section-heading"><div><h2>全部录音</h2><p>{recordings.length} 条录音，选择一条继续处理</p></div></div>
    <div className="recording-table"><div className="table-head"><span>录制时间</span><span>类型</span><span>时长</span><span>文件状态</span><span>AI 状态</span></div>
      {recordings.length ? recordings.map((item) => { const deleting = busy?.kind === "delete" && Number(busy.fid) === Number(item.fid); return <button className={`table-row${Number(selectedFid) === Number(item.fid) ? " selected" : ""}${deleting ? " deleting" : ""}`} type="button" key={`${item.kind || "file"}-${item.fid}`} disabled={deleting} aria-busy={deleting} onClick={() => onSelect(item.fid)}>
        <span className="row-title"><b>{formatDate(item.fid)}</b><small>#{item.fid}</small></span><span><em className={item.kind === "voice_memo" ? "type-pill memo" : "type-pill"}>{item.kind === "voice_memo" ? "语音备忘录" : "设备录音"}</em></span><span>{formatDuration(item.local_duration || item.duration_seconds)}</span><span className={deleting ? "deleting-status" : item.local_url ? "ready" : "muted"}>{deleting ? <><i />正在删除…</> : item.local_url ? "已在本机" : "等待下载"}</span><span className={item.summary ? "ready" : item.transcription ? "ai-progress" : "muted"}>{item.summary ? "已总结" : item.transcription ? "已转写" : "未处理"}</span>
      </button>; }) : <div className="empty"><FileIcon /><strong>还没有录音</strong><span>点击右上角“读取设备”获取 A1 中的文件</span></div>}
    </div>
  </section>;
}

function MarkdownSummary({ text }) {
  if (!text) return <div className="summary-empty"><SparklesIcon /><p>完成转写后，可以让模型提取摘要、要点、待办和标签。</p></div>;
  return <div className="markdown-summary">{text.split("\n").map((line, index) => {
    if (line.startsWith("## ")) return <h4 key={index}>{line.slice(3)}</h4>;
    if (/^[-*] /.test(line)) return <p className="bullet" key={index}>{line.slice(2)}</p>;
    return line.trim() ? <p key={index}>{line}</p> : null;
  })}</div>;
}

function RecordingWorkspace({ recording, busy, onAction, onSaveTranscript, onDelete }) {
  const [draft, setDraft] = useState(recording?.transcription || "");
  useEffect(() => setDraft(recording?.transcription || ""), [recording?.fid, recording?.transcription]);
  if (!recording) return null;
  const mediaUrl = protectedUrl(recording.local_url);
  const working = Number(busy?.fid) === Number(recording.fid);
  const deleting = working && busy?.kind === "delete";
  return <section className="workspace" id="local">
    <header><div><span className="eyebrow">当前录音</span><h2>{formatDate(recording.fid, true)}</h2><p>{recording.kind === "voice_memo" ? "语音备忘录" : "DingTalk A1 设备录音"} · {formatDuration(recording.local_duration || recording.duration_seconds)}</p></div><div className="workspace-actions">{!recording.local_url && <button className="secondary" disabled={working} onClick={() => onAction("download", recording.fid)}><DownloadIcon />{working ? "下载中" : "下载到本机"}</button>}{recording.local_url && <a className="secondary" href={mediaUrl} download><DownloadIcon />保存 OGG</a>}{recording.local_url && <button className="secondary local-delete" disabled={working} onClick={() => onDelete(recording, "local")}><TrashIcon />删除本机</button>}{recording.on_device !== false && recording.kind !== "voice_memo" && <button className={`secondary device-delete${deleting ? " deleting-button" : ""}`} disabled={working} onClick={() => onDelete(recording, "device")}>{deleting ? <i className="button-spinner" /> : <TrashIcon />}{deleting ? "删除中…" : "删除设备"}</button>}</div></header>
    {recording.local_url ? <AudioPlayer src={mediaUrl} fallbackDuration={recording.local_duration || recording.duration_seconds} markers={recording.markers || []} large /> : <div className="download-prompt"><DownloadIcon /><span><strong>音频还在 A1 中</strong>下载后即可播放、转写和总结。</span><button onClick={() => onAction("download", recording.fid)}>开始下载</button></div>}
    <div className="insight-columns"><article className="transcript-pane"><div className="pane-head"><div><span className="eyebrow">TRANSCRIPT</span><h3>转写文本</h3></div><button className="text-button" disabled={working} onClick={() => onAction("transcribe", recording.fid)}><SparklesIcon />{working && busy.kind === "transcribe" ? "转写中…" : recording.transcription ? "重新转写" : "开始转写"}</button></div><textarea value={draft} onChange={(event) => setDraft(event.target.value)} placeholder="转写结果会出现在这里，也可以手动编辑。" /><div className="pane-foot"><span>{recording.transcription_model || "可编辑后再生成总结"}</span><button disabled={draft === (recording.transcription || "")} onClick={() => onSaveTranscript(recording.fid, draft)}>保存修改</button></div></article>
      <article className="summary-pane"><div className="pane-head"><div><span className="eyebrow">AI NOTES</span><h3>智能整理</h3></div><button className="text-button" disabled={working || !recording.transcription} onClick={() => onAction("summarize", recording.fid)}><SparklesIcon />{working && busy.kind === "summarize" ? "整理中…" : recording.summary ? "重新整理" : "生成总结"}</button></div><MarkdownSummary text={recording.summary} /></article></div>
  </section>;
}

const MEMO_LABELS = { stopped: "监听已关闭", connecting: "正在连接 A1", waiting: "等待语音备忘录", recording: "正在接收语音", saving: "正在保存", transcribing: "正在转写", error: "监听遇到问题" };

function MemoPanel({ state, busy, onToggle, onSelect }) {
  const memo = state.memo || { state: "stopped", enabled: false };
  const items = state.recordings.filter((item) => item.kind === "voice_memo").sort((a, b) => b.fid - a.fid).slice(0, 4);
  const live = ["connecting", "waiting", "recording", "saving", "transcribing"].includes(memo.state);
  return <aside className="memo-panel" id="memos"><div className="memo-panel-head"><div><span className="eyebrow">VOICE MEMO</span><h2>语音备忘录</h2></div><button className={`switch${memo.enabled ? " on" : ""}`} type="button" aria-label={memo.enabled ? "停止监听" : "开始监听"} onClick={onToggle}><i /></button></div>
    <div className={`memo-connection${memo.stream_enabled ? " ready" : ""}${memo.state === "recording" || memo.packets_received ? " receiving" : ""}`}><span><i />BLE 鉴权</span><span><i />实时推流</span><span><i />音频接收</span></div>
    <div className={`listener${live ? " live" : ""}${memo.state === "recording" ? " recording" : ""}`}><div className="sound-orb"><span /><span /><span /><span /><span /></div><strong>{MEMO_LABELS[memo.state] || memo.state}</strong>{memo.state === "recording" ? <div className="memo-live-count"><b>{Number(memo.current_duration_seconds || 0).toFixed(2)} 秒</b><span>{memo.packets_received || 0} 个 Opus 包</span></div> : <p>{memo.state === "waiting" ? "监听已经就绪。现在短按 A1 开始录音，再短按一次结束。" : memo.error || "打开监听后，电脑会鉴权并开启 A1 的实时推流通道。"}</p>}<small className="memo-last-event">{memo.last_event || "尚未建立监听连接"}</small><button className={memo.enabled ? "stop-listening" : "start-listening"} disabled={busy?.kind === "memo"} onClick={onToggle}>{memo.enabled ? <><StopIcon />停止监听</> : <><MicIcon />连接并监听</>}</button></div>
    <div className="recent-memos"><div className="recent-title"><h3>最近收到</h3><span>{items.length ? `${items.length} 条` : "暂无"}</span></div>{items.map((item) => <button type="button" key={item.fid} onClick={() => onSelect(item.fid)}><span className="memo-play"><MicIcon /></span><span><strong>{formatDate(item.fid)}</strong><small>{item.transcription || `${formatDuration(item.duration_seconds)} · 点击查看`}</small></span></button>)}{!items.length && <p className="memo-empty">收到的短语音会保存在电脑，并显示在这里。</p>}</div>
    <div className="privacy-note"><span>仅在本机保存</span><p>监听会独占 A1 的蓝牙连接；读取或下载设备文件时会短暂停止并自动恢复。</p></div>
  </aside>;
}

function SettingsDialog({ configured, onClose, onSave }) {
  const [key, setKey] = useState("");
  return <div className="dialog-backdrop" onMouseDown={onClose}><section className="settings-dialog" role="dialog" aria-modal="true" onMouseDown={(event) => event.stopPropagation()}><span className="eyebrow">AI SETTINGS</span><h2>硅基流动 API Key</h2><p>Key 只保存在这个本地服务的运行内存中，重启后会清除。音频和文字只会在你主动调用时发给硅基流动。</p><label>API Key<input type="password" value={key} autoFocus placeholder={configured ? "已配置；输入新 Key 可替换" : "sk-..."} onChange={(event) => setKey(event.target.value)} /></label><div className="dialog-actions"><button className="secondary" onClick={onClose}>取消</button><button className="primary" disabled={!key.trim()} onClick={() => onSave(key)}>保存到本次运行</button></div></section></div>;
}

function DeleteDialog({ request, onClose, onConfirm }) {
  if (!request) return null;
  const { recording, target } = request;
  const local = target === "local";
  return <div className="dialog-backdrop" onMouseDown={onClose}><section className="settings-dialog delete-dialog" role="dialog" aria-modal="true" onMouseDown={(event) => event.stopPropagation()}><span className="eyebrow danger-text">DELETE</span><h2>{local ? "删除本地副本？" : "删除设备原件？"}</h2><p>{local ? recording.kind === "voice_memo" ? "将删除电脑上的 OGG、转写和总结；这条语音备忘录不会保留其他副本。" : recording.on_device !== false ? "将删除电脑上的 DTYJ、OGG、转写、总结和标记；A1 设备中的原件会保留。" : "将删除电脑上的全部本地文件。这条录音已不在当前设备索引中，删除后无法恢复。" : recording.local_url ? "只删除 A1 设备中的原件，电脑上的 DTYJ、OGG、转写和总结会保留。" : "这条录音尚未保存到电脑。删除 A1 原件后无法恢复。"}</p><div className="dialog-actions"><button className="secondary" onClick={onClose}>取消</button><button className="delete-confirm" onClick={onConfirm}>确认删除{local ? "本地副本" : "设备原件"}</button></div></section></div>;
}

export default function App() {
  const [state, setState] = useState({ connected: false, device: null, recordings: [], memo: { state: "stopped", enabled: false } });
  const [selectedFid, setSelectedFid] = useState(null);
  const [busy, setBusy] = useState(null);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [pendingDelete, setPendingDelete] = useState(null);

  async function loadState() {
    const response = await apiFetch("/api/state");
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(body.error || "无法读取本机服务状态");
    setState(body);
  }

  useEffect(() => { loadState().catch((reason) => setError(reason.message)); const timer = window.setInterval(() => loadState().catch(() => {}), 1500); return () => window.clearInterval(timer); }, []);
  const recordings = useMemo(() => [...state.recordings].sort((a, b) => Number(b.fid) - Number(a.fid)), [state.recordings]);
  useEffect(() => { if (recordings.length && !recordings.some((item) => Number(item.fid) === Number(selectedFid))) setSelectedFid(recordings[0].fid); }, [recordings, selectedFid]);
  const selected = recordings.find((item) => Number(item.fid) === Number(selectedFid)) || null;

  async function run(kind, fid, extra = {}) {
    setBusy({ kind, fid }); setError(""); setNotice("");
    const endpoints = { refresh: "/api/refresh", download: "/api/download", transcribe: "/api/transcribe", summarize: "/api/summarize" };
    try { const body = await jsonRequest(endpoints[kind], fid === undefined ? undefined : { fid, ...extra }); setState(body.state || body); setNotice({ refresh: "已读取 A1 中的录音", download: "录音已保存为 OGG", transcribe: "转写完成", summarize: "总结完成" }[kind]); }
    catch (reason) { setError(reason.message); } finally { setBusy(null); }
  }

  async function saveTranscript(fid, transcription) {
    setBusy({ kind: "save", fid }); setError("");
    try { const body = await jsonRequest("/api/transcript", { fid, transcription }); setState(body.state); setNotice("转写修改已保存"); } catch (reason) { setError(reason.message); } finally { setBusy(null); }
  }

  async function toggleMemo() {
    setBusy({ kind: "memo" }); setError("");
    try { await jsonRequest(state.memo?.enabled ? "/api/memo/stop" : "/api/memo/start"); await loadState(); } catch (reason) { setError(reason.message); } finally { setBusy(null); }
  }

  async function saveKey(apiKey) {
    try { const body = await jsonRequest("/api/settings/api-key", { api_key: apiKey }); setState((current) => ({ ...current, ai_key_configured: body.ai_key_configured })); setSettingsOpen(false); setNotice("API Key 已保存到本次运行内存"); } catch (reason) { setError(reason.message); }
  }

  function selectMemo(fid) {
    setSelectedFid(fid);
    window.requestAnimationFrame(() => {
      document.getElementById("local")?.scrollIntoView({ behavior: "smooth", block: "start" });
    });
  }

  async function confirmDelete() {
    const request = pendingDelete; if (!request) return;
    const { recording, target } = request;
    setPendingDelete(null);
    setBusy({ kind: "delete", fid: recording.fid });
    setError("");
    setNotice(target === "local" ? "正在删除本地副本…" : "正在停止实时监听，并等待 A1 确认删除…");
    try { const body = await jsonRequest(target === "local" ? "/api/delete-local" : "/api/delete", { fid: recording.fid, kind: recording.kind || "recording", confirmed: true }); setState(body.state); setNotice(target === "local" ? "本地副本已删除，设备原件未改变" : "A1 已确认删除，设备原件已从列表移除"); } catch (reason) { setNotice(""); setError(`删除失败：${reason.message}`); } finally { setBusy(null); }
  }

  const deleting = busy?.kind === "delete";
  return <div className="app" id="top"><Sidebar /><div className="app-body"><header className="topbar"><div><h1>我的声音</h1><p>读取、聆听，再把录音变成可用的信息。</p></div><div className="top-actions"><span className={`connection-badge${state.connected ? " online" : ""}`}><i />{state.connected ? "A1 已连接" : "尚未读取"}</span><button className="secondary" disabled={busy?.kind === "refresh"} onClick={() => run("refresh")}><RefreshIcon />{busy?.kind === "refresh" ? "读取中" : "读取设备"}</button><button className="icon-button" title="AI 设置" onClick={() => setSettingsOpen(true)}><SettingsIcon /><i className={state.ai_key_configured ? "key-dot on" : "key-dot"} /></button></div></header><DeviceStrip state={state} />{(error || notice) && <div className={error ? "toast error" : deleting ? "toast pending" : "toast success"} role="status" aria-live="polite">{deleting && <i className="toast-spinner" />}{error || notice}<button onClick={() => { setError(""); setNotice(""); }}>×</button></div>}<div className="content-grid"><main className="content-main"><RecordingList recordings={recordings} selectedFid={selectedFid} busy={busy} onSelect={setSelectedFid} /><RecordingWorkspace recording={selected} busy={busy} onAction={run} onSaveTranscript={saveTranscript} onDelete={(recording, target) => setPendingDelete({ recording, target })} /></main><MemoPanel state={state} busy={busy} onToggle={toggleMemo} onSelect={selectMemo} /></div></div>{settingsOpen && <SettingsDialog configured={state.ai_key_configured} onClose={() => setSettingsOpen(false)} onSave={saveKey} />}<DeleteDialog request={pendingDelete} onClose={() => setPendingDelete(null)} onConfirm={confirmDelete} /></div>;
}
