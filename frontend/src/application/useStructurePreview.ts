import {useEffect, useRef, useState} from 'react';
import {previewBridge} from '../platform/previewBridge';

export function useStructurePreview() {
  const [ready, setReady] = useState(false), [waiting, setWaiting] = useState(false);
  const [error, setError] = useState('');
  const next = useRef(0), pending = useRef(0);
  useEffect(() => previewBridge.subscribe(status => {
    next.current = Math.max(next.current, status.ack);
    setReady(true);
    if (pending.current && status.ack >= pending.current) {
      pending.current = 0; setWaiting(false); setError(status.error);
    }
  }), []);
  return {
    canRequest: ready && !waiting, error,
    open(taskId: string, fileId: string, revision: number) {
      if (!ready || pending.current) return;
      const id = ++next.current;
      pending.current = id; setWaiting(true); setError('');
      previewBridge.send({id, revision, taskId, fileId});
    },
  };
}
export type StructurePreviewControls = ReturnType<typeof useStructurePreview>;
