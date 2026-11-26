import {useEffect, useRef, useState} from 'react';
import type {CaptureDraft, CustomCaptureDraft} from '../platform/bridge';
import {previewBridge, type PreviewIntent} from '../platform/previewBridge';

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
  const sendIntent = (make: (id: number) => PreviewIntent) => {
    if (!ready || pending.current) return;
    const id = ++next.current;
    pending.current = id; setWaiting(true); setError('');
    previewBridge.send(make(id));
  };
  return {
    canRequest: ready && !waiting, error,
    subscribeCustomDraft: previewBridge.subscribeCustomDraft,
    open(taskId: string, fileId: string, revision: number) {
      sendIntent(id => ({id, revision, taskId, fileId}));
    },
    openCaptureWorld(draft: CaptureDraft, revision: number) {
      sendIntent(id => ({id, source: 'captureWorld', revision, draft}));
    },
    openCaptureCustom(custom: CustomCaptureDraft) {
      sendIntent(id => ({id, source: 'captureCustom', size: custom.size, layers: custom.layers}));
    },
  };
}
export type StructurePreviewControls = ReturnType<typeof useStructurePreview>;
