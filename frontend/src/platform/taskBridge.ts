export interface TaskFile {id: string; name: string; path: string; hidden: boolean; dimension: number | null}
export interface TaskItem {id: string; name: string; selectedFileId: string; layers: TaskFile[]}
export interface TaskState {
  tasks: TaskItem[]; currentTaskId: string; activeTaskId: string;
  revision: number; ack: number; accepted: boolean; loading: boolean; browsing: boolean;
  worldAvailable: boolean; writable: boolean; error: string; errorFileId: string;
}
export type TaskAction = 'create' | 'select' | 'rename' | 'delete' | 'addFile' | 'path' | 'visible' | 'move' | 'removeFile' | 'selectFile' | 'activate' | 'close';
export interface TaskIntent {id: number; revision: number; action: TaskAction; taskId?: string; fileId?: string; value?: string; delta?: number}
const taskValueMaxBytes = 32768, taskIntentMaxBytes = 262144;
// Gameface does not guarantee TextEncoder. Count UTF-8 without allocating another copy.
function utf8Bytes(text: string) {
  let bytes = 0;
  for (let i = 0; i < text.length; ++i) {
    const code = text.charCodeAt(i);
    if (code < 0x80) ++bytes;
    else if (code < 0x800) bytes += 2;
    else if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length
      && text.charCodeAt(i + 1) >= 0xdc00 && text.charCodeAt(i + 1) <= 0xdfff) {bytes += 4; ++i;}
    else bytes += 3;
  }
  return bytes;
}
export function parseTasks(payload: string): TaskState {
  const s = JSON.parse(payload);
  if (!s || !Number.isSafeInteger(s.revision) || s.revision < 0 || !Number.isSafeInteger(s.ack) || s.ack < 0
    || !['accepted','loading','browsing','worldAvailable','writable'].every(k => typeof s[k] === 'boolean')
    || !['currentTaskId','activeTaskId','error','errorFileId'].every(k => typeof s[k] === 'string')
    || !Array.isArray(s.tasks) || !s.tasks.every((t: any) => t && typeof t.id === 'string' && typeof t.name === 'string'
      && typeof t.selectedFileId === 'string' && Array.isArray(t.layers) && t.layers.every((f: any) => f
        && ['id','name','path'].every(k => typeof f[k] === 'string') && typeof f.hidden === 'boolean'
        && (f.dimension === null || Number.isSafeInteger(f.dimension)))))
    throw new Error('Invalid LHolo tasks state');
  return s as TaskState;
}
export const taskBridge = {
  subscribe(listener: (state: TaskState) => void) {
    const receive = (payload: string) => {try {listener(parseTasks(payload));} catch (error) {console.error(error);}};
    window.engine.on('lholo:tasks', receive);
    return () => window.engine.off('lholo:tasks', receive);
  },
  send(intent: TaskIntent) {
    const request = JSON.stringify(intent);
    if (utf8Bytes(intent.value ?? '') > taskValueMaxBytes || utf8Bytes(request) > taskIntentMaxBytes) return false;
    window.engine.trigger('facet:discard', 'lholo.tasks.intent');
    window.engine.trigger('facet:request', 'lholo.tasks.intent', 'lholo.tasks.intent', {request});
    return true;
  },
};
