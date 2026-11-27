import React from 'react';
import {
  type InterfaceSettingsControls,
} from '../application/useInterfaceSettings';
import type { MenuLabels } from '../platform/bridge';
import {
  LHoloSectionGroup,
  LHoloSettingRow,
  LHoloStepButton,
  LHoloTextField,
} from '../components/LHoloControls';
import { LHoloSelect } from '../components/LHoloSelect';

export function InterfacePage({ labels, interfaceSettings }: { labels: MenuLabels; interfaceSettings: InterfaceSettingsControls }) {
  const languageLabel = labels['label.language'];
  const uiScaleLabel = labels['label.uiScale'];
  return (
    <LHoloSectionGroup title={labels['section.interfaceSettings']}>
      <LHoloSettingRow label={languageLabel}>
        <LHoloSelect label={languageLabel} value={interfaceSettings.language}
          disabled={!interfaceSettings.ready} options={interfaceSettings.languages.map(item => item.name)} onChange={interfaceSettings.setLanguage} />
      </LHoloSettingRow>
      <LHoloSettingRow label={uiScaleLabel}>
        <div className="lholo-stepper">
          <LHoloStepButton direction="dec" disabled={!interfaceSettings.ready || interfaceSettings.uiScalePercent <= 50}
            onClick={() => interfaceSettings.stepUiScale(-1)} />
          <div className="lholo-stepper-value" style={{ width: '4.2rem' }}>
            <LHoloTextField disabled={!interfaceSettings.ready} label={uiScaleLabel} value={interfaceSettings.uiScaleText} maxLength={4}
              onChange={interfaceSettings.editUiScale} onBlur={interfaceSettings.commitUiScale} />
          </div>
          <LHoloStepButton direction="inc" disabled={!interfaceSettings.ready || interfaceSettings.uiScalePercent >= 200}
            onClick={() => interfaceSettings.stepUiScale(1)} />
        </div>
      </LHoloSettingRow>
    </LHoloSectionGroup>
  );
}
