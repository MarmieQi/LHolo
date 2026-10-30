export interface StructureWriteState {
  ack: number; worldRevision: number; busy: boolean; localAvailable: boolean; result: string;
}
export interface StructureWriteIntent {
  id: number; revision: number; worldRevision: number; taskId: string; fileId: string; mode: 'local'; skipAir: boolean;
}
export function parseStructureWriteState(payload: string): StructureWriteState {
  const s = JSON.parse(payload);
  if (!s || !Number.isSafeInteger(s.ack) || s.ack < 0 || !Number.isSafeInteger(s.worldRevision) || s.worldRevision < 0
    || typeof s.busy !== 'boolean' || typeof s.localAvailable !== 'boolean' || typeof s.result !== 'string')
    throw new Error('Invalid LHolo world-write state');
  return s;
}
export const structureWriteBridge = {
  subscribe(listener: (state: StructureWriteState) => void) {
    const receive = (payload: string) => { try { listener(parseStructureWriteState(payload)); } catch (error) { console.error(error); } };
    window.engine.on('lholo:write', receive);
    return () => window.engine.off('lholo:write', receive);
  },
  send(intent: StructureWriteIntent) {
    window.engine.trigger('facet:discard', 'lholo.write.intent');
    window.engine.trigger('facet:request', 'lholo.write.intent', 'lholo.write.intent', {request: JSON.stringify(intent)});
  },
};
