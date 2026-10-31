import {useEffect, useState} from 'react';
import type {SettingsControls} from './useSettings';
import type {MenuBridge} from '../platform/bridge';
export function useHudSettings(settings: SettingsControls, bridge: Pick<MenuBridge, 'editHudLayout'>) {
  const v = settings.values;
  const [maxItemsText, setMaxItemsText] = useState<string | null>(null);
  useEffect(() => { setMaxItemsText(null); }, [settings.state?.context]);
  useEffect(() => {
    if (settings.state && !settings.state.accepted && !settings.pending) setMaxItemsText(null);
  }, [settings.state, settings.pending]);
  const editMaxItems = (value: string) => {
    setMaxItemsText(value);
    const n = /^\d+$/.test(value.trim()) ? Number(value) : NaN;
    if (Number.isSafeInteger(n)) settings.change('materialHudMaxItems', Math.max(1, Math.min(50, n)));
  };
  return {writeHudEnabled: v.writeHudEnabled, setWriteHudEnabled: (v: boolean) => settings.change('writeHudEnabled', v),
    ready: settings.ready, editLayout: bridge.editHudLayout, hudEnabled: v.hudEnabled,
    hudShowFileName: v.hudShowFileName, hudShowLayer: v.hudShowLayer,
    hudShowOverallProgress: v.hudShowOverallProgress, hudShowProgress: v.hudShowProgress,
    hudShowProjectedBlockName: v.hudShowProjectedBlockName, hudShowWrongState: v.hudShowWrongState,
    hudShowWrongType: v.hudShowWrongType, hudShowExtraBlocks: v.hudShowExtraBlocks,
    materialHudEnabled: v.materialHudEnabled, materialHudStackConversion: v.materialHudStackConversion,
    materialHudMaxItemsText: maxItemsText ?? String(v.materialHudMaxItems),
    editMaterialHudMaxItems: editMaxItems, commitMaterialHudMaxItems: () => setMaxItemsText(null),
    stepMaterialHudMaxItems: (delta: number) => {
      setMaxItemsText(null);
      settings.change('materialHudMaxItems', Math.max(1, Math.min(50, v.materialHudMaxItems + delta)));
    },
    setHudEnabled: (v: boolean) => settings.change('hudEnabled', v),
    setHudShowFileName: (v: boolean) => settings.change('hudShowFileName', v),
    setHudShowLayer: (v: boolean) => settings.change('hudShowLayer', v),
    setHudShowOverallProgress: (v: boolean) => settings.change('hudShowOverallProgress', v),
    setHudShowProgress: (v: boolean) => settings.change('hudShowProgress', v),
    setHudShowProjectedBlockName: (v: boolean) => settings.change('hudShowProjectedBlockName', v),
    setHudShowWrongState: (v: boolean) => settings.change('hudShowWrongState', v),
    setHudShowWrongType: (v: boolean) => settings.change('hudShowWrongType', v),
    setHudShowExtraBlocks: (v: boolean) => settings.change('hudShowExtraBlocks', v),
    setMaterialHudEnabled: (v: boolean) => settings.change('materialHudEnabled', v),
    setMaterialHudStackConversion: (v: boolean) => settings.change('materialHudStackConversion', v),
};
}
export type HudSettingsControls = ReturnType<typeof useHudSettings>;
