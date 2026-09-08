export async function request(path, options = {}) {
  const response = await fetch(path, { ...options, headers: { 'Content-Type': 'application/json', ...options.headers } });
  const result = await response.json();
  if (!response.ok) { const error = new Error(result.error || '操作未完成'); error.status = response.status; throw error; }
  return result;
}
export const post = (path, data) => request(path, { method: 'POST', body: JSON.stringify(data) });
export function upload(path, file, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/upload?path=' + encodeURIComponent(path));
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = event => { if (event.lengthComputable) onProgress(Math.round(event.loaded / event.total * 100)); };
    xhr.onerror = () => reject(new Error('连接中断；请刷新个人目录检查是否有完成的文件'));
    xhr.onload = () => {
      let result;
      try { result = JSON.parse(xhr.responseText); } catch { reject(new Error('服务返回了无效响应')); return; }
      if (xhr.status >= 400) reject(new Error(result.error || '上传失败')); else resolve(result);
    };
    xhr.send(file);
  });
}
export function sizeLabel(bytes) {
  if (bytes == null) return '—';
  if (bytes < 1000) return bytes + ' B';
  if (bytes < 1000000) return (bytes / 1000).toFixed(1) + ' KB';
  if (bytes < 1000000000) return (bytes / 1000000).toFixed(2) + ' MB';
  return (bytes / 1000000000).toFixed(2) + ' GB';
}
