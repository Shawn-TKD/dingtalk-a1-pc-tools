import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Folder, FolderPlus, Upload, RefreshCw, Search, Info, Lock, CheckCircle2, X } from 'lucide-react';
import { request, post, upload, sizeLabel } from './api';
import { Sidebar, EmptyState, FileTable, Modal } from './components';
import './style.css';

const PERSONAL = '/emmc/mindlink';
function App() {
  const [status, setStatus] = useState(null);
  const [path, setPath] = useState('/emmc');
  const [listing, setListing] = useState(null);
  const [search, setSearch] = useState('');
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');
  const [missing, setMissing] = useState(false);
  const [dialog, setDialog] = useState(null);
  const [folderName, setFolderName] = useState('');
  const [transfer, setTransfer] = useState(null);
  const [dragging, setDragging] = useState(false);
  const generation = useRef(0);
  const pathRef = useRef('/emmc');
  const uploadInput = useRef(null);
  const busyRef = useRef(false);
  busyRef.current = busy;
  const writable = path === PERSONAL || path.startsWith(PERSONAL + '/');
  const title = writable ? '个人文件' : path.startsWith('/emmc/audio') ? '录音目录' : '设备存储';
  const entries = (listing?.entries || []).filter(item => item.name.toLocaleLowerCase().includes(search.toLocaleLowerCase()));

  async function loadDirectory(target) {
    const id = ++generation.current;
    setLoading(true); setMissing(false); setError(''); setListing(null);
    try { const result = await request('/api/list?path=' + encodeURIComponent(target)); if (id === generation.current) setListing(result); }
    catch (error) { if (id === generation.current) { if (error.status === 404 && target === PERSONAL) setMissing(true); else setError(error.message); } }
    finally { if (id === generation.current) setLoading(false); }
  }
  async function refresh() {
    setError('');
    try { const next = await request('/api/status'); setStatus(next); if (next.connected) await loadDirectory(path); else setListing(null); }
    catch (error) { setError(error.message); }
  }
  useEffect(() => {
    let alive = true;
    (async () => {
      try {
        const token = new URLSearchParams(location.hash.slice(1)).get('token');
        if (token) { await post('/api/session', {token}); history.replaceState(null, '', location.pathname); }
        const next = await request('/api/status');
        if (alive) { setStatus(next); if (next.connected) await loadDirectory(pathRef.current); }
      } catch (error) { if (alive) setError(error.message); }
    })();
    const timer = setInterval(async () => {
      if (busyRef.current) return;
      try { const next = await request('/api/status'); if (alive) {setStatus(next); if (!next.connected) setListing(null);} } catch { /* Explicit refresh exposes transport/auth errors. */ }
    }, 6000);
    return () => { alive = false; clearInterval(timer); };
  }, []);

  function navigate(target) { if (busy) return; pathRef.current=target; setPath(target); setSearch(''); setMessage(''); setTransfer(null); setListing(null); setMissing(false); if (status?.connected) loadDirectory(target); }
  async function connect() {
    setDialog(null); setBusy(true); setError(''); setMessage('');
    try { const next = await post('/api/connect', {confirm_mode_change:true}); setStatus(next); await loadDirectory(path); setMessage('USB 文件连接已就绪'); }
    catch (error) { setError(error.message); }
    finally { setBusy(false); }
  }
  async function mkdir(root=false) {
    const target = root ? PERSONAL : path + '/' + folderName.trim();
    if (!root && !folderName.trim()) return;
    setBusy(true); setError('');
    try { await post('/api/mkdir', {path:target}); setDialog(null); setFolderName(''); await loadDirectory(path); setMessage('文件夹已创建'); }
    catch (error) {setError(error.message);}
    finally {setBusy(false);}
  }
  async function uploadFiles(files) {
    if (!files.length || busy || !status?.connected || !writable || missing) return;
    setBusy(true); setError(''); setMessage('');
    let count = 0;
    try {
      for (const file of files) {
        if (file.size > status.max_upload_bytes) throw new Error(file.name + ' 超过 1 GiB 限制');
        setTransfer({name:file.name,percent:0,label:'传送到本机服务'});
        const result = await upload(path+'/'+file.name, file, percent => setTransfer({name:file.name,percent,label:percent === 100 ? '正在写入 A1 并读回校验…' : '传送到本机服务'}));
        count += 1;
        setTransfer({name:file.name,percent:100,label:`已写入并校验 · ${sizeLabel(result.bytes)} · ${result.elapsed_seconds} 秒`});
      }
      await loadDirectory(path);
      setMessage(`${count} 个文件已存入 A1，读回内容一致`);
    } catch (error) {setError((count ? `已有 ${count} 个文件完成。` : '')+error.message); await loadDirectory(path).catch(()=>{}); setError((count ? `已有 ${count} 个文件完成。` : '')+error.message);}
    finally {setBusy(false); if (uploadInput.current) uploadInput.current.value='';}
  }
  async function download(item) {
    setBusy(true); setError(''); setMessage(''); setTransfer({name:item.name,percent:null,label:'正在从 A1 读取到电脑…'});
    try {
      const response = await fetch('/api/download?path='+encodeURIComponent(item.path));
      if (!response.ok) throw new Error((await response.json()).error || '下载失败');
      const blob = await response.blob();
      const url = URL.createObjectURL(blob); const anchor = document.createElement('a');
      anchor.href=url; anchor.download=item.name; document.body.appendChild(anchor); anchor.click(); anchor.remove();
      setTimeout(() => URL.revokeObjectURL(url),60000);
      setTransfer({name:item.name,percent:100,label:`已读取 ${sizeLabel(blob.size)}，浏览器正在保存`});
      setMessage('已交给浏览器下载；A1 原文件保留');
    } catch (error) {setError(error.message); setTransfer(null);}
    finally {setBusy(false);}
  }
  const crumbs = path.split('/').filter(Boolean);
  return <div className="app"><Sidebar path={path} status={status} busy={busy} navigate={navigate} connect={() => setDialog('connect')}/>
    <main onDragOver={event => {event.preventDefault(); if(writable && !missing && status?.connected) setDragging(true);}} onDragLeave={event => {if(!event.currentTarget.contains(event.relatedTarget)) setDragging(false);}} onDrop={event => {event.preventDefault(); setDragging(false); uploadFiles(Array.from(event.dataTransfer.files));}}>
      <header className="main-header"><div><h2>{title}</h2><p>{writable ? '把文件存进 A1，只在这个独立目录中写入。' : '浏览 A1 中的文件夹，下载文件到电脑。'}</p></div><button className="button refresh" disabled={busy || loading} onClick={refresh}><RefreshCw size={18} className={loading ? 'spin' : ''}/>刷新</button></header>
      <div className={'notice '+(writable ? 'write-notice' : '')}><Info size={18}/><span>{writable ? '同名不覆盖 · 写入后读回校验 · 单文件最多 1 GiB · 为录音保留 1 GiB' : '设备目录只读；上传仅限个人文件夹。'}</span></div>
      <div className="toolbar"><div className="breadcrumbs"><Folder size={24}/><span>/</span>{crumbs.map((crumb,index) => <React.Fragment key={index}>{index > 0 && <span>/</span>}<button onClick={() => navigate('/'+crumbs.slice(0,index+1).join('/'))}>{crumb}</button></React.Fragment>)}</div><div className="tools"><label className="search"><Search size={18}/><input aria-label="搜索当前目录" placeholder="搜索当前目录" value={search} onChange={event => setSearch(event.target.value)}/></label><button className="button" disabled={!writable || !status?.connected || missing || busy} onClick={() => setDialog('folder')}><FolderPlus size={18}/><span>新建文件夹</span></button><button className={'button '+(writable ? 'primary' : '')} disabled={!writable || !status?.connected || missing || busy} onClick={() => uploadInput.current.click()}><Upload size={18}/><span>上传文件</span></button><input className="hidden" ref={uploadInput} type="file" multiple onChange={event => uploadFiles(Array.from(event.target.files))}/></div></div>
      {error && <div className="alert error" role="alert"><span>{error}</span><button aria-label="关闭错误" onClick={() => setError('')}><X size={16}/></button></div>}
      {message && <div className="alert success" role="status"><CheckCircle2 size={17}/><span>{message}</span><button aria-label="关闭提示" onClick={() => setMessage('')}><X size={16}/></button></div>}
      <FileTable entries={status?.connected ? entries : []} openFolder={navigate} download={download} busy={busy}/>
      {(!status?.connected || loading || missing || !listing?.entries.length) && <EmptyState connected={status?.connected} loading={loading} missing={missing} writable={writable} connect={() => setDialog('connect')} createRoot={() => mkdir(true)} busy={busy}/>}
      {listing?.entries.length > 0 && entries.length === 0 && <div className="no-results">没有找到匹配的文件</div>}
      {transfer && <div className="transfer" role="status"><div><strong>{transfer.name}</strong><span>{transfer.label}</span></div>{transfer.percent != null && <progress max="100" value={transfer.percent}/>}</div>}
      {path.startsWith('/emmc/audio') && <p className="format-note">这里下载的是设备原始文件；普通录音及备忘录容器需要转码后播放。本页不会删除设备录音。</p>}
      <footer className="privacy"><Lock size={17}/>所有文件在本机处理，不上传云端。</footer>
      {dragging && <div className="drop-overlay"><Upload size={40}/><h2>松开以存入个人文件夹</h2><p>不会覆盖同名文件</p></div>}
    </main>
    {dialog === 'connect' && <Modal title="连接 A1 的 USB 文件模式" close={() => setDialog(null)}><p>设备将暂停正常录音并断开蓝牙连接。请确认现在没有录音任务，且允许进入 USB 文件模式。</p><p className="muted">使用本机保存的设备身份，不需要打开钉钉 App。</p><div className="modal-actions"><button className="button" onClick={() => setDialog(null)}>取消</button><button className="button primary" onClick={connect}>确认并连接</button></div></Modal>}
    {dialog === 'folder' && <Modal title="新建文件夹" close={() => setDialog(null)} disabled={busy}><p className="muted">位置：{path}</p>{error && <p className="alert error" role="alert">{error}</p>}<form onSubmit={event => {event.preventDefault();mkdir();}}><input autoFocus className="folder-input" aria-label="文件夹名称" placeholder="例如：我的资料" value={folderName} onChange={event => setFolderName(event.target.value)}/><div className="modal-actions"><button type="button" className="button" disabled={busy} onClick={() => setDialog(null)}>取消</button><button className="button primary" disabled={busy || !folderName.trim()}>创建文件夹</button></div></form></Modal>}
  </div>;
}
createRoot(document.getElementById('root')).render(<App/>);
