export interface PreviewStatus {ack: number; accepted: boolean; error: string}
export interface PreviewIntent {id: number; revision: number; taskId: string; fileId: string}
export function parsePreviewStatus(payload: string): PreviewStatus {
  const value = JSON.parse(payload);
  if (!value || !Number.isSafeInteger(value.ack) || value.ack < 0
    || typeof value.accepted !== 'boolean' || typeof value.error !== 'string')
    throw new Error('Invalid LHolo preview state');
  return value;
}
export const previewBridge = {
  subscribe(listener: (status: PreviewStatus) => void) {
    const receive = (payload: string) => {try {listener(parsePreviewStatus(payload));} catch (error) {console.error(error);}};
    window.engine.on('lholo:preview', receive);
    return () => window.engine.off('lholo:preview', receive);
  },
  send(intent: PreviewIntent) {
    // Measure the real frame so max-width, rem scaling and aspect ratio match.
    const menu = document.querySelector?.('.lholo-menu')?.getBoundingClientRect();
    const backdrop = document.querySelector?.('.lholo-backdrop')?.getBoundingClientRect();
    const frame = menu && backdrop && backdrop.width > 0 && backdrop.height > 0
      ? [menu.width / backdrop.width, menu.height / backdrop.height] : undefined;
    window.engine.trigger('facet:discard', 'lholo.preview.intent');
    window.engine.trigger('facet:request', 'lholo.preview.intent', 'lholo.preview.intent', {request: JSON.stringify({...intent, frame})});
  },
};
