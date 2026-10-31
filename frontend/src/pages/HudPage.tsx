import React, { useState } from 'react';
import type { HudSettingsControls } from '../application/useHudSettings';
import type { MenuLabels } from '../platform/bridge';
import { LHoloButton, LHoloCheckbox, LHoloTextField } from '../components/LHoloControls';

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
    <div className="lholo-hud-option" key={key}>
      <LHoloCheckbox label={labels[key]} value={value}
        onChange={onChange} disabled={!hud.ready || !hud.hudEnabled} />
    </div>
  ));

  return <>
    <div className="lholo-hud-header">
      <div className="lholo-hud-title">{labels['section.hud']}</div>
      <LHoloButton label={labels['hud.layout.edit']} onClick={hud.editLayout} disabled={!hud.ready || !worldAvailable} />
    </div>
    <div className="lholo-hud-tabs">
      <LHoloButton label={labels['hud.tabProjection']} onClick={() => setTab('projection')}
        className={'lholo-hud-tab' + (tab === 'projection' ? ' is-selected' : '')} />
      <LHoloButton label={labels['hud.tabMaterials']} onClick={() => setTab('materials')}
        className={'lholo-hud-tab' + (tab === 'materials' ? ' is-selected' : '')} />
      <LHoloButton label={labels['hud.tabWrite']} onClick={() => setTab('write')}
        className={'lholo-hud-tab' + (tab === 'write' ? ' is-selected' : '')} />
    </div>
    {tab === 'projection' ? <div className="lholo-hud-tab-content">
      <div className="lholo-hud-master">
        <LHoloCheckbox disabled={!hud.ready} label={labels['checkbox.hudEnabled']}
          value={hud.hudEnabled} onChange={hud.setHudEnabled} />
      </div>
      <div className="lholo-hud-columns">
        <div className="lholo-hud-column">
          <div className="lholo-hud-group">
            <div className="lholo-hud-group-title">{labels['hud.groupInfo']}</div>
            {renderItems(infoItems)}
          </div>
          <div className="lholo-hud-group">
            <div className="lholo-hud-group-title">{labels['hud.groupProgress']}</div>
            {renderItems(progressItems)}
          </div>
        </div>
        <div className="lholo-hud-column">
          <div className="lholo-hud-group">
            <div className="lholo-hud-group-title">{labels['hud.groupErrors']}</div>
            {renderItems(errorItems)}
          </div>
        </div>
      </div>
    </div> : tab === 'materials' ? <div className="lholo-hud-tab-content">
      <div className="lholo-hud-master">
        <LHoloCheckbox disabled={!hud.ready} label={labels['checkbox.materialHudEnabled']}
          value={hud.materialHudEnabled} onChange={hud.setMaterialHudEnabled} />
      </div>
      <div className="lholo-hud-option">
        <LHoloCheckbox disabled={!hud.ready || !hud.materialHudEnabled} label={labels['checkbox.materialHudStackConversion']}
          value={hud.materialHudStackConversion} onChange={hud.setMaterialHudStackConversion} />
      </div>
      <div className="lholo-material-hud-row lholo-setting-row">
        <div className="lholo-material-hud-label">{labels['label.materialHudMaxItems']}</div>
        <div className="lholo-stepper">
          <LHoloButton disabled={!hud.ready || !hud.materialHudEnabled} label="-" className="lholo-stepper-button"
            onClick={() => hud.stepMaterialHudMaxItems(-1)} />
          <div className="lholo-stepper-value" style={{ width: '3.2rem' }}>
            <LHoloTextField disabled={!hud.ready || !hud.materialHudEnabled}
              label={labels['label.materialHudMaxItems']} value={hud.materialHudMaxItemsText} maxLength={3}
              onChange={hud.editMaterialHudMaxItems} onBlur={hud.commitMaterialHudMaxItems} />
          </div>
          <LHoloButton disabled={!hud.ready || !hud.materialHudEnabled} label="+" className="lholo-stepper-button"
            onClick={() => hud.stepMaterialHudMaxItems(1)} />
        </div>
      </div>
    </div> : <div className="lholo-hud-tab-content">
      <div className="lholo-hud-master">
        <LHoloCheckbox disabled={!hud.ready} label={labels['checkbox.writeHudEnabled']}
          value={hud.writeHudEnabled} onChange={hud.setWriteHudEnabled} />
      </div>
    </div>}
  </>;
}
