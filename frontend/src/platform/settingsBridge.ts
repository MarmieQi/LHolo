export interface SettingsValues {
  writeHudEnabled: boolean;
  hudEnabled: boolean; hudPosition: number; hudShowFileName: boolean; hudShowLayer: boolean;
  hudShowOverallProgress: boolean; hudShowProgress: boolean; hudShowProjectedBlockName: boolean;
  hudShowWrongState: boolean; hudShowWrongType: boolean; hudShowExtraBlocks: boolean;
  materialHudEnabled: boolean; materialHudStackConversion: boolean; materialHudMaxItems: number; materialHudPosition: number; altWheelOffsetEnabled: boolean;
  placementRadius: number; autoPlacementCooldown: number; activeMode: number; language: string;
}
export type SettingsField = keyof SettingsValues;
export interface SettingsHotkey {slot: number; labelKey: string; groupKey: string; key: number; modifiers: number; capturing: boolean; display: string}
export interface SettingsState {
  values: SettingsValues; hotkeys: SettingsHotkey[]; languages: {code: string; name: string}[];
  consent: boolean; context: number; ack: number; accepted: boolean; hotkeysRevision: number;
}
export type SettingsCommand = 'capture' | 'clearHotkey' | 'resetHotkey' | 'resetHotkeys' | 'consent';
export interface SettingsIntent {
  id: number; context: number; action: 'set' | SettingsCommand; field?: SettingsField;
  expected?: boolean | number | string; value?: boolean | number | string; slot?: number; hotkeysRevision?: number;
}
export interface SettingsBridge {
  subscribeSettings(listener: (state: SettingsState) => void): () => void;
  settings(intent: SettingsIntent): void;
  cancelHotkeyCapture(): void;
}
export function parseSettings(payload: string): SettingsState {
  const s = JSON.parse(payload), v = s?.values;
  const integer = (n: unknown, low: number, high: number) => typeof n === 'number' && Number.isInteger(n) && n >= low && n <= high;
  const id = (n: unknown) => typeof n === 'number' && Number.isSafeInteger(n) && n >= 0;
  const boolFields = ['writeHudEnabled', 'hudEnabled', 'hudShowFileName', 'hudShowLayer', 'hudShowOverallProgress', 'hudShowProgress',
    'hudShowProjectedBlockName', 'hudShowWrongState', 'hudShowWrongType', 'hudShowExtraBlocks', 'materialHudEnabled', 'materialHudStackConversion', 'altWheelOffsetEnabled'];
  if (!v || !boolFields.every(k => typeof v[k] === 'boolean') || !integer(v.hudPosition, 0, 3)
    || !integer(v.materialHudMaxItems, 1, 50) || !integer(v.materialHudPosition, 0, 3) || !integer(v.placementRadius, 1, 4)
    || !integer(v.autoPlacementCooldown, 0, 60) || !integer(v.activeMode, 0, 3) || typeof v.language !== 'string'
    || !id(s.context) || !id(s.ack) || !id(s.hotkeysRevision) || typeof s.accepted !== 'boolean' || typeof s.consent !== 'boolean'
    || !Array.isArray(s.languages) || !s.languages.length || !s.languages.every((l: any) => l && typeof l.code === 'string' && !!l.code && typeof l.name === 'string')
    || new Set(s.languages.map((l: any) => l.code)).size !== s.languages.length || !s.languages.some((l: any) => l.code === v.language)
    || !Array.isArray(s.hotkeys) || !s.hotkeys.length || s.hotkeys.length > 256
    || !s.hotkeys.every((b: any, n: number) => b && b.slot === n && typeof b.labelKey === 'string' && typeof b.groupKey === 'string' && !!b.groupKey
      && integer(b.key, 0, 255) && integer(b.modifiers, 0, 7) && typeof b.capturing === 'boolean' && typeof b.display === 'string')
    || s.hotkeys.filter((b: any) => b.capturing).length > 1) throw new Error('Invalid LHolo settings state');
  return s as SettingsState;
}
export const settingsBridge: SettingsBridge = {
  subscribeSettings(listener) {
    const receive = (payload: string) => {
      try { listener(parseSettings(payload)); } catch (error) { console.error(error); }
    };
    window.engine.on('lholo:settings', receive);
    return () => window.engine.off('lholo:settings', receive);
  },
  settings(intent) {
    window.engine.trigger('facet:discard', 'lholo.settings.intent');
    window.engine.trigger('facet:request', 'lholo.settings.intent', 'lholo.settings.intent', {request: JSON.stringify(intent)});
  },
  cancelHotkeyCapture() {
    const name = 'lholo.hotkeys.cancel';
    let hash = 0;
    for (let n = 0; n < name.length; ++n) hash = (name.charCodeAt(n) + (hash << 5) - hash) >>> 0;
    window.engine.trigger('query:subscribe/' + name, hash);
  },
};
