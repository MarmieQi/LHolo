import {useEffect, useState} from 'react';
import {pruneCleanFields} from './fieldDraft';
import type {SettingsBridge, SettingsCommand, SettingsField, SettingsIntent, SettingsState, SettingsValues} from '../platform/settingsBridge';

const initial: SettingsValues = {writeHudEnabled: true, hudEnabled: true, hudPosition: 1, hudShowFileName: true, hudShowLayer: true,
  hudShowOverallProgress: false, hudShowProgress: true, hudShowProjectedBlockName: true, hudShowWrongState: true,
  hudShowWrongType: true, hudShowExtraBlocks: true, materialHudEnabled: false, materialHudStackConversion: false, materialHudMaxItems: 5, materialHudPosition: 3,
  altWheelOffsetEnabled: true, placementRadius: 4, autoPlacementCooldown: 10, activeMode: 0, language: ''};
interface Editor {native: SettingsState | null; dirty: Partial<SettingsValues>; pending: SettingsIntent | null; failed: boolean}
let nextId = 0;
// Shared acknowledgement scheduling for these settings pages; page hooks own
// only input text and capture/modal presentation.
export function useSettings(bridge: SettingsBridge) {
  const [editor, set] = useState<Editor>({native: null, dirty: {}, pending: null, failed: false});
  useEffect(() => bridge.subscribeSettings(native => {
    nextId = Math.max(nextId, native.ack);
    set(previous => {
      if (!previous.native || native.context !== previous.native.context)
        return {native, dirty: {}, pending: null, failed: false};
      let dirty = {...previous.dirty}, pending = previous.pending;
      if (pending && native.ack >= pending.id) {
        if (!native.accepted) return {native, dirty: {}, pending: null, failed: false};
        if (pending.action === 'set' && pending.field && dirty[pending.field] === pending.value) delete dirty[pending.field];
        pending = null;
      }
      dirty = pruneCleanFields(dirty, native.values, pending?.action === 'set' && pending.field ? [pending.field] : []);
      return {...previous, native, dirty, pending};
    });
  }), [bridge]);
  const send = (intent: SettingsIntent) => {
    set(previous => ({...previous, pending: intent}));
    try { bridge.settings(intent); }
    catch (error) {
      set(previous => ({...previous, pending: null, failed: true}));
      console.error(error);
    }
  };
  useEffect(() => {
    if (!editor.native || editor.pending || editor.failed) return;
    const field = (Object.keys(editor.dirty) as SettingsField[]).find(k => editor.dirty[k] !== editor.native!.values[k]);
    if (field) send({id: ++nextId, context: editor.native.context, action: 'set', field,
      expected: editor.native.values[field], value: editor.dirty[field]});
  }, [bridge, editor]);
  const change = <F extends SettingsField>(field: F, value: SettingsValues[F]) => set(previous => {
    if (!previous.native) return previous;
    const dirty = pruneCleanFields({...previous.dirty, [field]: value}, previous.native.values,
      previous.pending?.action === 'set' && previous.pending.field ? [previous.pending.field] : []);
    return {...previous, failed: false, dirty};
  });
  const canCommand = !!editor.native && !editor.pending && !Object.keys(editor.dirty).length;
  const command = (action: SettingsCommand, slot?: number, mode?: number) => {
    if (!canCommand || !editor.native) return;
    send({id: ++nextId, context: editor.native.context, action, slot, hotkeysRevision: editor.native.hotkeysRevision,
      ...(action === 'consent' ? {expected: editor.native.values.activeMode, value: mode ?? 0} : {})});
  };
  return {state: editor.native, values: {...(editor.native?.values || initial), ...editor.dirty},
    ready: !!editor.native, canCommand, pending: editor.pending, change, command, cancelCapture: bridge.cancelHotkeyCapture};
}
export type SettingsControls = ReturnType<typeof useSettings>;
