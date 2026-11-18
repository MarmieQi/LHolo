import React from 'react';
import {
  type InterfaceSettingsControls,
} from '../application/useInterfaceSettings';
import type { MenuLabels } from '../platform/bridge';
import { LHoloSectionGroup, LHoloSettingRow } from '../components/LHoloControls';
import { LHoloSelect } from '../components/LHoloSelect';

export function InterfacePage({ labels, interfaceSettings }: { labels: MenuLabels; interfaceSettings: InterfaceSettingsControls }) {
  const languageLabel = labels['label.language'];
  return (
    <LHoloSectionGroup title={labels['section.interfaceSettings']}>
      <LHoloSettingRow label={languageLabel}>
        <LHoloSelect label={languageLabel} value={interfaceSettings.language}
          disabled={!interfaceSettings.ready} options={interfaceSettings.languages.map(item => item.name)} onChange={interfaceSettings.setLanguage} />
      </LHoloSettingRow>
    </LHoloSectionGroup>
  );
}
