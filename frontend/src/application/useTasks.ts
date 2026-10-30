import {useEffect, useRef, useState} from 'react';
import {taskBridge, type TaskAction, type TaskIntent, type TaskState} from '../platform/taskBridge';

export function useTasks() {
  const [state, setState] = useState<TaskState | null>(null);
  const [waiting, setWaiting] = useState(false);
  const [requestRejected, setRequestRejected] = useState(false);
  const latest = useRef<TaskState | null>(null), pending = useRef(0), next = useRef(0);
  useEffect(() => taskBridge.subscribe(s => {
    latest.current = s; next.current = Math.max(next.current, s.ack);
    if (pending.current && s.ack >= pending.current) {pending.current = 0; setWaiting(false);}
    setState(s);
  }), []);
  const command = (action: TaskAction, fields: Partial<TaskIntent> = {}) => {
    const s = latest.current;
    if (!s || pending.current || !s.writable || s.browsing || (s.loading && action !== 'close')) return;
    const id = ++next.current; pending.current = id; setWaiting(true);
    const sent = taskBridge.send({id, revision: s.revision, action, taskId: s.currentTaskId, ...fields});
    setRequestRejected(!sent);
    if (!sent) {pending.current = 0; setWaiting(false);}
  };
  const tasks = state?.tasks ?? [];
  const currentTask = tasks.find(t => t.id === state?.currentTaskId);
  const layers = currentTask?.layers ?? [];
  const selectedLayer = layers.find(f => f.id === currentTask?.selectedFileId);
  return {
    state, tasks, currentTask, layers, selectedLayer, requestRejected,
    currentTaskId: state?.currentTaskId ?? '', selectedLayerId: currentTask?.selectedFileId ?? '',
    canCommand: !!state?.writable && !waiting && !state.loading && !state.browsing,
    canClose: !!state?.writable && !waiting && !state.browsing && (!!state.loading || !!state.activeTaskId),
    selectTask: (taskId: string) => command('select', {taskId}),
    createTask: (value: string) => command('create', {value}),
    renameTask: (taskId: string, value: string) => command('rename', {taskId, value}),
    deleteTask: (taskId: string) => command('delete', {taskId}),
    addLayer: () => command('addFile'),
    browseLayer: (fileId: string) => command('addFile', {fileId}),
    toggleLayerHidden: (fileId: string) => command('visible', {fileId}),
    moveLayer: (fileId: string, delta: -1 | 1) => command('move', {fileId, delta}),
    removeLayer: (fileId: string, taskId: string) => command('removeFile', {fileId, taskId}),
    changeLayerPath: (fileId: string, value: string) => {if (layers.find(f => f.id === fileId)?.path !== value) command('path', {fileId, value});},
    setSelectedLayerId: (fileId: string) => command('selectFile', {fileId}),
    activate: () => command('activate'), close: () => command('close'),
  };
}
export type TaskControls = ReturnType<typeof useTasks>;
