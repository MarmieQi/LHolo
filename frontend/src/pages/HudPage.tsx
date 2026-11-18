import React, { useState } from 'react';
import type { HudSettingsControls } from '../application/useHudSettings';
import type { MenuLabels } from '../platform/bridge';
import {
  LHoloButton,
  LHoloCheckbox,
  LHoloSectionGroup,
  LHoloSettingRow,
  LHoloStepButton,
  LHoloTextField,
} from '../components/LHoloControls';

import { LHoloTabs } from '../components/LHoloTabs';

type HudTab = 'projection' | 'materials' | 'write';

export function HudPage({ labels, hud, worldAvailable }: { labels: MenuLabels; hud: HudSettingsControls; worldAvailable: boolean }) {
  const [tab, setTab] = useState<HudTab>('projection');
  const infoItems = [
    ['checkbox.hudFileName', hud.hudShowFileName, hud.setHudShowFileName],
    ['checkbox.hudLayer', hud.hudShowLayer, hud.setHudShowLayer],
    ['checkbox.hudProjectedBlockName', hud.hudShowProjectedBlockName, hud.setHudShowProjectedBlockName],
  ] as const;
  const progressItems = [
    ['checkbox.hudOverallProgress', hud.hudShowOverallProgress, hud.setHudShowOverallProgress],
    ['checkbox.hudProgress', hud.hudShowProgress, hud.setHudShowProgress],
  ] as const;
  const errorItems = [
    ['checkbox.hudWrongState', hud.hudShowWrongState, hud.setHudShowWrongState],
    ['checkbox.hudWrongType', hud.hudShowWrongType, hud.setHudShowWrongType],
    ['checkbox.hudExtraBlocks', hud.hudShowExtraBlocks, hud.setHudShowExtraBlocks],
  ] as const;
  const renderItems = (items: typeof infoItems | typeof progressItems | typeof errorItems) => items.map(([key, value, onChange]) => (
    <LHoloCheckbox row key={key} label={labels[key]} value={value}
      onChange={onChange} disabled={!hud.ready || !hud.hudEnabled} />
  ));

  return <>
    <div className="lholo-hud-toolbar">
      <LHoloTabs<HudTab> selected={tab} onSelect={setTab} tabs={[
        { id: 'projection', label: labels['hud.tabProjection'] },
        { id: 'materials', label: labels['hud.tabMaterials'] },
        { id: 'write', label: labels['hud.tabWrite'] },
      ]} />
      <LHoloButton label={labels['hud.layout.edit']} onClick={hud.editLayout} disabled={!hud.ready || !worldAvailable} />
    </div>
    {tab === 'projection' ? <div className="lholo-tab-content">
      <LHoloSectionGroup>
        <LHoloCheckbox row disabled={!hud.ready} label={labels['checkbox.hudEnabled']}
          value={hud.hudEnabled} onChange={hud.setHudEnabled} />
      </LHoloSectionGroup>
      <div className="lholo-settings-columns">
        <div className="lholo-settings-column">
          <LHoloSectionGroup title={labels['hud.groupInfo']}>
            {renderItems(infoItems)}
          </LHoloSectionGroup>
          <LHoloSectionGroup title={labels['hud.groupProgress']}>
            {renderItems(progressItems)}
          </LHoloSectionGroup>
        </div>
        <div className="lholo-settings-column">
          <LHoloSectionGroup title={labels['hud.groupErrors']}>
            {renderItems(errorItems)}
          </LHoloSectionGroup>
        </div>
      </div>
    </div> : tab === 'materials' ? <div className="lholo-tab-content">
      <LHoloSectionGroup>
        <LHoloCheckbox row disabled={!hud.ready} label={labels['checkbox.materialHudEnabled']}
          value={hud.materialHudEnabled} onChange={hud.setMaterialHudEnabled} />
        <LHoloCheckbox row disabled={!hud.ready || !hud.materialHudEnabled} label={labels['checkbox.materialHudStackConversion']}
          value={hud.materialHudStackConversion} onChange={hud.setMaterialHudStackConversion} />
        <LHoloSettingRow label={labels['label.materialHudMaxItems']}>
          <div className="lholo-stepper">
            <LHoloStepButton direction="dec" disabled={!hud.ready || !hud.materialHudEnabled}
              onClick={() => hud.stepMaterialHudMaxItems(-1)} />
            <div className="lholo-stepper-value" style={{ width: '3.2rem' }}>
              <LHoloTextField disabled={!hud.ready || !hud.materialHudEnabled}
                label={labels['label.materialHudMaxItems']} value={hud.materialHudMaxItemsText} maxLength={3}
                onChange={hud.editMaterialHudMaxItems} onBlur={hud.commitMaterialHudMaxItems} />
            </div>
            <LHoloStepButton direction="inc" disabled={!hud.ready || !hud.materialHudEnabled}
              onClick={() => hud.stepMaterialHudMaxItems(1)} />
          </div>
        </LHoloSettingRow>
      </LHoloSectionGroup>
    </div> : <div className="lholo-tab-content">
      <LHoloSectionGroup>
        <LHoloCheckbox row disabled={!hud.ready} label={labels['checkbox.writeHudEnabled']}
          value={hud.writeHudEnabled} onChange={hud.setWriteHudEnabled} />
      </LHoloSectionGroup>
    </div>}
  </>;
}
