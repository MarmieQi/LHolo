import { useEffect, useState } from 'react';
import type { SettingsControls } from './useSettings';

const MIN_UI_SCALE_PERCENT = 50;
const MAX_UI_SCALE_PERCENT = 200;
const UI_SCALE_STEP_PERCENT = 10;

function formatScale(percent: number): string {
  return (percent / 100).toFixed(1);
}

// Language codes and choices come from native state; uiScalePercent is stored as 50..200.
export function useInterfaceSettings(settings: SettingsControls) {
  const languages = settings.state?.languages || [];
  const language = Math.max(0, languages.findIndex(item => item.code === settings.values.language));
  const setLanguage = (index: number) => {
    if (languages[index]) settings.change('language', languages[index].code);
  };

  const uiScalePercent = settings.values.uiScalePercent;
  const [scaleDraft, setScaleDraft] = useState<string | null>(null);
  useEffect(() => { setScaleDraft(null); }, [uiScalePercent]);

  const stepUiScale = (delta: number) => {
    setScaleDraft(null);
    const next = Math.max(
      MIN_UI_SCALE_PERCENT,
      Math.min(MAX_UI_SCALE_PERCENT, Math.round((uiScalePercent + delta * UI_SCALE_STEP_PERCENT) / 10) * 10)
    );
    settings.change('uiScalePercent', next);
  };
  const editUiScale = (text: string) => setScaleDraft(text);
  const commitUiScale = () => {
    if (scaleDraft === null) return;
    const trimmed = scaleDraft.trim();
    setScaleDraft(null);
    const parsed = Number(trimmed);
    if (!trimmed || !Number.isFinite(parsed)) return;
    const percent = Math.max(MIN_UI_SCALE_PERCENT, Math.min(MAX_UI_SCALE_PERCENT, Math.round(parsed * 100)));
    settings.change('uiScalePercent', percent);
  };

  return {
    ready: settings.ready,
    languages,
    language,
    setLanguage,
    uiScalePercent,
    uiScaleText: scaleDraft ?? formatScale(uiScalePercent),
    stepUiScale,
    editUiScale,
    commitUiScale,
  };
}

export type InterfaceSettingsControls = ReturnType<typeof useInterfaceSettings>;
