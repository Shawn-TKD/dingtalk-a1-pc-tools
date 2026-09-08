import React, { useEffect, useRef } from 'react';
import { HardDrive, FileAudio, Usb, Folder, Download, File, Lock, X, RefreshCw } from 'lucide-react';
import { sizeLabel } from './api';

function FolderUser({size=24}) { return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d="M3 7V5a2 2 0 0 1 2-2h5l2 3h7a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7Z"/><circle cx="12" cy="11" r="2"/><path d="M8 18c0-4 8-4 8 0Z"/></svg>; }
export const NAV = [
  { path: '/emmc', name: '设备存储', icon: HardDrive },
  { path: '/emmc/audio', name: '录音目录', icon: FileAudio },
  { path: '/emmc/mindlink', name: '个人文件', icon: FolderUser },
];
export function Sidebar({ path, status, busy, navigate, connect }) {
  const selected = path.startsWith('/emmc/mindlink') ? 2 : path.startsWith('/emmc/audio') ? 1 : 0;
  const info = status?.info || {};
  return <aside className="sidebar">
    <div className="brand"><h1>A1 文件空间</h1><p>USB 本地文件管理</p></div>
    <nav aria-label="文件分类">{NAV.map((item, index) => <button key={item.path} className={'nav-item ' + (index === selected ? 'selected' : '')} onClick={() => navigate(item.path)}><item.icon size={23}/><span>{item.name}</span></button>)}</nav>
    <div className="device-status"><div className="connection-line"><i className={status?.connected ? 'dot online' : 'dot'}/><strong>{status?.connected ? 'USB 已连接' : status?.state === 'offline' ? '设备待认证' : '未连接'}</strong></div><p>{status?.connected ? 'DingTalk A1' : 'USB 数据线连接'}</p>
      {status?.connected && <div className="device-details">{info.battery_persent != null && <span>电量 {info.battery_persent}%</span>}{info.firmware_version && <span title={info.firmware_version}>{info.firmware_version}</span>}{status.capacity && <><div className="storage-track"><div style={{width: `${Math.max(.5,status.capacity.used/status.capacity.total*100)}%`}}/></div><span>剩余 {sizeLabel(status.capacity.free)} / {sizeLabel(status.capacity.total)}</span></>}</div>}
      <button className="button outlined connect-side" disabled={busy} onClick={connect}>{busy ? <RefreshCw className="spin" size={16}/> : null}{status?.connected ? '重新检查连接' : '连接设备'}</button>
    </div><div className="sidebar-footer">本机服务 · 仅此电脑</div>
  </aside>;
}
export function EmptyState({ connected, loading, missing, connect, createRoot, busy, writable }) {
  return <div className="empty-state"><Usb className="empty-usb" size={70} strokeWidth={1.2}/><h2>{loading ? '正在读取文件夹' : !connected ? '等待连接你的 A1' : missing ? '创建你的个人文件夹' : '这个文件夹还是空的'}</h2><p>{loading ? '正在通过 USB 获取设备上的真实文件。' : !connected ? <>将录音卡用数据线连接到电脑，然后点击连接设备。<br/>连接会进入 USB 文件模式，并中断蓝牙连接。</> : missing ? '创建独立目录后，就可以把文件存进 A1，不影响原有录音目录。' : writable ? '可以从上方上传文件，或拖放文件到此处。' : '当前目录没有文件；如需上传，请前往个人文件。'}</p>{!connected && !loading && <button className="button primary" disabled={busy} onClick={connect}>连接设备</button>}{connected && missing && <button className="button primary" disabled={busy} onClick={createRoot}>创建个人文件夹</button>}</div>;
}
function kind(item) {
  if (item.type === 'folder') return '文件夹';
  if (item.type !== 'file') return '特殊节点';
  if (item.path === '/emmc/audio/00000000000000') return '备忘录容器';
  if (item.path.startsWith('/emmc/audio/')) return 'A1 原始录音';
  return '文件';
}
export function FileTable({ entries, openFolder, download, busy }) {
  return <div className="file-table" role="table" aria-label="设备文件"><div className="file-row table-heading" role="row"><span role="columnheader">名称</span><span role="columnheader">类型</span><span role="columnheader">大小</span><span role="columnheader">操作</span></div>{entries.map(item => <div className="file-row" role="row" key={item.path}>
    <div className="file-name" role="cell">{item.type === 'folder' ? <Folder size={22}/> : item.path.startsWith('/emmc/audio/') ? <FileAudio size={22}/> : <File size={22}/>}<div>{item.type === 'folder' ? <button className="name-button" onClick={() => openFolder(item.path)} title={item.name}>{item.name}</button> : <span title={item.name}>{item.name}</span>}<small>{item.name === 'mindlink' && item.path === '/emmc/mindlink' ? '个人文件 · 可上传' : item.name === 'audio' && item.type === 'folder' ? '录音与语音备忘录' : null}</small></div></div><span className="file-kind" role="cell">{kind(item)}</span><span className="file-size" role="cell">{item.type === 'folder' ? '—' : sizeLabel(item.size)}</span><div role="cell">{item.type === 'folder' ? <button className="row-button" onClick={() => openFolder(item.path)}>打开</button> : item.type === 'file' ? <button className="row-button" disabled={busy} onClick={() => download(item)}><Download size={15}/><span>下载</span></button> : <Lock size={16}/>}</div>
  </div>)}</div>;
}
export function Modal({ title, children, close, disabled=false }) {
  const panel = useRef(null);
  useEffect(() => {
    const previous = document.activeElement;
    if (!panel.current.contains(document.activeElement)) panel.current.focus();
    return () => previous?.focus();
  }, []);
  function keyDown(event) {
    if (event.key === 'Escape' && !disabled) close();
    if (event.key !== 'Tab') return;
    const elements = Array.from(panel.current.querySelectorAll('button:not(:disabled), input:not(:disabled), [tabindex="0"]'));
    if (!elements.length) {event.preventDefault(); return;}
    const first = elements[0], last = elements.at(-1);
    if (event.shiftKey && (document.activeElement === first || document.activeElement === panel.current)) {event.preventDefault();last.focus();}
    else if (!event.shiftKey && (document.activeElement === last || document.activeElement === panel.current)) {event.preventDefault();first.focus();}
  }
  return <div className="modal-backdrop" onClick={event => {if (event.target === event.currentTarget && !disabled) close();}}><section ref={panel} tabIndex={-1} onKeyDown={keyDown} className="modal" role="dialog" aria-modal="true" aria-label={title}><button className="modal-close" aria-label="关闭" disabled={disabled} onClick={close}><X size={20}/></button><h2>{title}</h2>{children}</section></div>;
}
