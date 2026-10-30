import {useEffect, useRef, useState} from 'react';
import {structureWriteBridge, type StructureWriteState} from '../platform/structureWriteBridge';

export function useStructureWrite() {
  const [state, setState] = useState<StructureWriteState | null>(null);
  const [waiting, setWaiting] = useState(false);
  const latest = useRef<StructureWriteState | null>(null), next = useRef(0), pending = useRef(0);
  useEffect(() => structureWriteBridge.subscribe(s => {
    const worldChanged = latest.current && latest.current.worldRevision !== s.worldRevision;
    latest.current = s;
    next.current = Math.max(next.current, s.ack);
    if (worldChanged || (pending.current && s.ack >= pending.current)) { pending.current = 0; setWaiting(false); }
    setState(s);
  }), []);
  return {
    state, busy: waiting || !!state?.busy,
    write(taskId: string, fileId: string, revision: number, skipAir: boolean) {
      const s = latest.current;
      if (!s?.localAvailable || s.busy || pending.current) return;
      const id = ++next.current;
      pending.current = id; setWaiting(true);
      structureWriteBridge.send({id, revision, worldRevision: s.worldRevision, taskId, fileId, mode: 'local', skipAir});
      return id;
    },
  };
}
export type StructureWriteControls = ReturnType<typeof useStructureWrite>;
