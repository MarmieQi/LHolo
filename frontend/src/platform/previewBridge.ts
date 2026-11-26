import type { CaptureDraft, CustomCaptureDraft } from './bridge';
export interface PreviewStatus {ack: number; accepted: boolean; error: string}
export type PreviewIntent =
  | {id: number; source?: 'task'; revision: number; taskId: string; fileId: string}
  | {id: number; source: 'captureWorld'; revision: number; draft: CaptureDraft}
  | {id: number; source: 'captureCustom'; size: CustomCaptureDraft['size']; layers: CustomCaptureDraft['layers']};
export function parsePreviewStatus(payload: string): PreviewStatus {
  const value = JSON.parse(payload);
  if (!value || !Number.isSafeInteger(value.ack) || value.ack < 0
    || typeof value.accepted !== 'boolean' || typeof value.error !== 'string')
    throw new Error('Invalid LHolo preview state');
  return value;
}
export function parsePreviewCustomDraft(payload: string): CustomCaptureDraft {
  const value = JSON.parse(payload);
  const posInt = (n: unknown) => typeof n === 'number' && Number.isInteger(n) && n > 0 && n <= 2147483647;
  const nonNegInt = (n: unknown) => typeof n === 'number' && Number.isInteger(n) && n >= 0 && n <= 2147483647;
  if (!value || typeof value !== 'object' || !value.size || typeof value.size !== 'object'
    || !posInt(value.size.x) || !posInt(value.size.y) || !posInt(value.size.z)
    || !Array.isArray(value.layers)
    || !value.layers.every((layer: unknown) => !!layer && typeof layer === 'object'
      && nonNegInt((layer as Record<string, unknown>).y)
      && nonNegInt((layer as Record<string, unknown>).endY)
      && typeof (layer as Record<string, unknown>).block === 'string'
      && ((layer as Record<string, unknown>).block as string).length > 0)) {
    throw new Error('Invalid LHolo preview custom draft');
  }
  return value as CustomCaptureDraft;
}
export const previewBridge = {
  subscribe(listener: (status: PreviewStatus) => void) {
    const receive = (payload: string) => {try {listener(parsePreviewStatus(payload));} catch (error) {console.error(error);}};
    window.engine.on('lholo:preview', receive);
    return () => window.engine.off('lholo:preview', receive);
  },
  subscribeCustomDraft(listener: (draft: CustomCaptureDraft) => void) {
    const receive = (payload: string) => {try {listener(parsePreviewCustomDraft(payload));} catch (error) {console.error(error);}};
    window.engine.on('lholo:preview-custom-draft', receive);
    return () => window.engine.off('lholo:preview-custom-draft', receive);
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
