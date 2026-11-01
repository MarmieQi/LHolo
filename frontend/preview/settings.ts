import type {SettingsState, SettingsIntent} from '../src/platform/settingsBridge';
import type {MenuLabels} from '../src/platform/bridge';
import fixture from './settings-fixture.json';
import en from '../../src/i18n/lang/en_US.json';
import ru from '../../src/i18n/lang/ru_RU.json';
import ja from '../../src/i18n/lang/ja_JP.json';
import zh from '../../src/i18n/lang/zh_CN.json';
import tw from '../../src/i18n/lang/zh_TW.json';

// Development boundary only: simulate native capture and settings, never bundled in the game.
export function createPreviewSettings(emit: (name: string, value: unknown) => void, publish: () => void) {
  const locales: Record<string, typeof zh> = {en_US: en, ja_JP: ja, ru_RU: ru, zh_CN: zh, zh_TW: tw};
  let state: SettingsState = {values: {...fixture.values}, hotkeys: fixture.defaults.map((b, slot) => ({...b, slot, display: '', capturing: false})),
    languages: Object.entries(locales).map(([code, l]) => ({code, name: l._meta.displayName})),
    consent: false, context: 0, ack: 0, accepted: false, hotkeysRevision: 0};
  const labels = () => {const {_meta, ...l} = locales[state.values.language]; return l as MenuLabels;};
  const refreshDisplays = () => {
    const l = labels(), names: Record<number, string> = {4:l['key.mouseMiddle'], 5:l['key.mouseSide1'], 6:l['key.mouseSide2'],
      37:'Left', 38:'Up', 39:'Right', 40:'Down', 13:'Enter', 32:'Space', 9:'Tab'};
    state.hotkeys = state.hotkeys.map(b => ({...b, display: !b.key ? l['key.notSet']
      : ((b.modifiers & 1) ? 'Ctrl + ' : '') + ((b.modifiers & 2) ? 'Alt + ' : '') + ((b.modifiers & 4) ? 'Shift + ' : '')
        + (names[b.key] ?? (b.key >= 112 && b.key <= 135 ? 'F' + (b.key - 111) : String.fromCharCode(b.key)))}));
  };
  let listening = false;
  const finishCapture = (key: number, modifiers: number) => {
    const target = state.hotkeys.find(b => b.capturing);
    if (!target || key === 122 || [16,17,18,91,92].includes(key)) return;
    state.hotkeys = state.hotkeys.map(b => ({...b, capturing: false,
      ...(b === target && key !== 27 ? {key: [8,46].includes(key) ? 0 : key, modifiers: [8,46].includes(key) ? 0 : modifiers}
        : key !== 27 && b !== target && b.key === key && b.modifiers === modifiers ? {key: 0, modifiers: 0} : {})}));
    ++state.hotkeysRevision; syncListeners(); publish();
  };
  const keyboard = (e: KeyboardEvent) => {
    if (e.keyCode === 122) return;
    e.preventDefault(); e.stopImmediatePropagation();
    finishCapture(e.keyCode, (e.ctrlKey ? 1 : 0) | (e.altKey ? 2 : 0) | (e.shiftKey ? 4 : 0));
  };
  const mouse = (e: MouseEvent) => {
    if (![1,3,4].includes(e.button)) return;
    e.preventDefault(); finishCapture(e.button === 1 ? 4 : e.button + 2, (e.ctrlKey ? 1 : 0) | (e.altKey ? 2 : 0) | (e.shiftKey ? 4 : 0));
  };
  function syncListeners() {
    const capture = state.hotkeys.some(b => b.capturing);
    if (capture === listening) return;
    listening = capture;
    if (capture) {document.addEventListener('keydown', keyboard, true); document.addEventListener('mousedown', mouse, true);}
    else {document.removeEventListener('keydown', keyboard, true); document.removeEventListener('mousedown', mouse, true);}
  }
  const cancel = () => {state.hotkeys = state.hotkeys.map(b => ({...b, capturing:false})); ++state.hotkeysRevision; syncListeners();};
  return {
    publish() {refreshDisplays(); emit('lholo:settings', state);},
    request(i: SettingsIntent) {
      state.accepted = false; state.ack = i.id;
      if (i.context === state.context) {
        if (i.action === 'set' && i.field && state.values[i.field] === i.expected) {
          if (!(i.field === 'activeMode' && i.value !== 0 && !state.consent)) {
            Object.assign(state.values, {[i.field]: i.value}); state.accepted = true;
            if (i.field === 'language') emit('lholo:menu-labels', labels());
          }
        } else if (i.action === 'consent' && i.expected === state.values.activeMode) {
          state.consent = true; state.values.activeMode = Number(i.value); state.accepted = true;
        } else if (i.hotkeysRevision === state.hotkeysRevision) {
          if (i.action === 'resetHotkeys') {
            cancel(); state.hotkeys = fixture.defaults.map((b, slot) => ({...b, slot, display:'', capturing:false})); state.values.altWheelOffsetEnabled = true;
          } else if (i.slot !== undefined && state.hotkeys[i.slot]) {
            if (i.action === 'capture') {cancel(); state.hotkeys[i.slot].capturing = true;}
            if (i.action === 'clearHotkey') Object.assign(state.hotkeys[i.slot], {key:0,modifiers:0,capturing:false});
            if (i.action === 'resetHotkey') Object.assign(state.hotkeys[i.slot], fixture.defaults[i.slot], {capturing:false});
          }
          ++state.hotkeysRevision; state.accepted = true; syncListeners();
        }
      }
      queueMicrotask(publish);
    },
    cancel() {cancel(); queueMicrotask(publish);},
    contextChanged() {++state.context; state.values.activeMode = 0; cancel();},
  };
}
