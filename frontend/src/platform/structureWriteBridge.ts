export type WriteMode = 'local' | 'commands';
export interface StructureWriteState {
  ack: number; worldRevision: number; busy: boolean; localAvailable: boolean; result: string;
  dimension: number | null;
  commandAvailable: boolean; mode: WriteMode; targetName: string; skipAir: boolean; skipPlaced: boolean;
  command: {active: boolean; canResume: boolean; done: number; total: number; state: string};
}
export type StructureWriteIntent = {
  id: number; revision: number; worldRevision: number; taskId: string; fileId: string; mode: WriteMode; skipAir: boolean; skipPlaced: boolean;
} | {id: number; worldRevision: number; action: 'resume' | 'cancel'};
export function parseStructureWriteState(payload: string): StructureWriteState {
  const s = JSON.parse(payload);
  if (!s || !Number.isSafeInteger(s.ack) || s.ack < 0 || !Number.isSafeInteger(s.worldRevision) || s.worldRevision < 0
    || typeof s.busy !== 'boolean' || typeof s.localAvailable !== 'boolean' || typeof s.result !== 'string'
    || typeof s.commandAvailable !== 'boolean' || !['local', 'commands'].includes(s.mode) || typeof s.targetName !== 'string'
    || typeof s.skipAir !== 'boolean' || typeof s.skipPlaced !== 'boolean'
    || !s.command || typeof s.command.active !== 'boolean' || typeof s.command.canResume !== 'boolean'
    || !Number.isSafeInteger(s.command.done) || s.command.done < 0 || !Number.isSafeInteger(s.command.total)
    || s.command.total < s.command.done || typeof s.command.state !== 'string'
    || !(s.dimension === null || Number.isSafeInteger(s.dimension)))
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
