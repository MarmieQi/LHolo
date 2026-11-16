import {useEffect, useState} from 'react';
import type {SettingsControls} from './useSettings';
export type AssistedPlacementMode = 'manual' | 'easy' | 'range';
export type ExperimentalConsentTarget = 'view' | AssistedPlacementMode | null;
type RangeField = 'placementRadius' | 'localPlacementRadius' | 'autoPlacementCooldown';
const modes: AssistedPlacementMode[] = ['manual', 'easy', 'range'];
export function useExperimentalSettings(settings: SettingsControls) {
  const [pendingConsentFeature, setModal] = useState<ExperimentalConsentTarget>(null);
  const [text, setText] = useState<Partial<Record<RangeField, string>>>({});
  useEffect(() => { setModal(null); setText({}); }, [settings.state?.context]);
  useEffect(() => {
    if (settings.state?.consent && !settings.pending) setModal(null);
    if (settings.state && !settings.state.accepted && !settings.pending) setText({});
  }, [settings.state, settings.pending]);
  const activeNumber = settings.state?.values.activeMode ?? 0;
  const experimentalConsent = settings.state?.consent ?? false;
  const activeMode = activeNumber === 0 || activeNumber >= 4 ? null : modes[activeNumber - 1];
  const toggleMode = (mode: AssistedPlacementMode, checked: boolean) => {
    if (!settings.canCommand) return;
    if (checked && !experimentalConsent) { setModal(mode); return; }
    settings.change('activeMode', checked ? modes.indexOf(mode) + 1 : 0);
  };
  const confirmConsent = () => {
    if (pendingConsentFeature && settings.canCommand)
      settings.command('consent', undefined, pendingConsentFeature === 'view' ? 0 : modes.indexOf(pendingConsentFeature) + 1);
  };
  const maximum = (field: string) => field === 'placementRadius' ? 4 : field === 'localPlacementRadius' ? 64 : 60;
  const minimum = (field: string) => field === 'autoPlacementCooldown' ? 0 : 1;
  const edit = (field: RangeField, value: string) => {
    setText(previous => ({...previous, [field]: value}));
    const n = /^-?\d+$/.test(value.trim()) ? Number(value) : NaN;
    if (Number.isFinite(n)) settings.change(field, Math.max(minimum(field), Math.min(maximum(field), n)));
  };
  const commit = (field: RangeField) => setText(previous => {
    const next = {...previous}; delete next[field]; return next;
  });
  const step = (field: RangeField, delta: number) => {
    commit(field);
    settings.change(field, Math.max(minimum(field), Math.min(maximum(field), settings.values[field] + delta)));
  };
  return {ready: settings.ready, canCommand: settings.canCommand, experimentalConsent, activeMode, pendingConsentFeature,
    localRangeAvailable: settings.state?.localRangeAvailable ?? false,
    localRangeEnabled: activeNumber === 4,
    localEasyEnabled: activeNumber === 5,
    localManualEnabled: activeNumber === 6,
    toggleLocalManual: (checked: boolean) => settings.change('activeMode', checked ? 6 : 0),
    toggleLocalEasy: (checked: boolean) => settings.change('activeMode', checked ? 5 : 0),
    localShulkerRestock: settings.values.localShulkerRestock,
    setLocalShulkerRestock: (checked: boolean) => settings.change('localShulkerRestock', checked),
    localCreativeRestock: settings.values.localCreativeRestock,
    setLocalCreativeRestock: (checked: boolean) => settings.change('localCreativeRestock', checked),
    localCreativeRestockOverwrite: settings.values.localCreativeRestockOverwrite,
    setLocalCreativeRestockOverwrite: (checked: boolean) => settings.change('localCreativeRestockOverwrite', checked),
    localLiquidsDisabled: settings.values.localPlacementLiquidsDisabled,
    setLocalLiquidsDisabled: (checked: boolean) => settings.change('localPlacementLiquidsDisabled', checked),
    toggleLocalRange: (checked: boolean) => settings.change('activeMode', checked ? 4 : 0),
    openInfoModal: () => setModal('view'), closeModal: () => setModal(null), confirmConsent, toggleMode,
    radius: (field: 'placementRadius' | 'localPlacementRadius') => ({
      text: text[field] ?? String(settings.values[field]),
      edit: (value: string) => edit(field, value),
      commit: () => commit(field),
      step: (delta: number) => step(field, delta),
    }),
    autoPlacementCooldown: {text: text.autoPlacementCooldown ?? String(settings.values.autoPlacementCooldown)},
    editAutoPlacementCooldown: (v: string) => edit('autoPlacementCooldown', v), commitAutoPlacementCooldown: () => commit('autoPlacementCooldown'),
    stepAutoPlacementCooldown: (d: number) => step('autoPlacementCooldown', d)};
}
export type ExperimentalSettingsControls = ReturnType<typeof useExperimentalSettings>;
