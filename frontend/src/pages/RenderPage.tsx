import React from 'react';
import type { OpacityField, RenderSettingsControls } from '../application/useRenderSettings';
import type { MenuLabels } from '../platform/bridge';
import {
  LHoloButton,
  LHoloCheckbox,
  LHoloSectionGroup,
  LHoloSettingRow,
  LHoloStepButton,
  LHoloTextField,
} from '../components/LHoloControls';
import { LHoloSelect } from '../components/LHoloSelect';

export function RenderPage({ labels, render }: { labels: MenuLabels; render: RenderSettingsControls }) {
  const isMaterial = render.layerAxis === 2;
  const axisOptions = [
    labels['combo.layerAxisY'],
    labels['combo.layerAxisX'],
    labels['combo.layerAxisMaterial'],
  ];
  const rangeOptions = isMaterial
    ? [
        labels['combo.materialAll'],
        labels['combo.materialSingle'],
        labels['combo.materialUpToCurrent'],
        labels['combo.materialFromCurrent'],
      ]
    : [
        labels['combo.rangeAll'],
        labels['combo.rangeSingle'],
        labels['combo.rangeUpToCurrent'],
        labels['combo.rangeFromCurrent'],
      ];
  const currentLayerLabel = labels[isMaterial ? 'label.currentMaterial' : 'label.currentLayer'];
  const layerHint = isMaterial
    ? labels['hint.materialOrder'].replace('%d', String(render.maxLayer + 1))
    : labels['hint.layerZeroBased']
        .replace('%d', String(render.maxLayer))
        .replace('%s', render.layerAxis === 1 ? 'X' : 'Y');

  return <>
    <LHoloSectionGroup title={labels['section.projectionStyle']}>
      <LHoloCheckbox row disabled={!render.ready} label={labels['checkbox.renderBounds']}
        value={render.structureBoundsEnabled} onChange={render.setStructureBoundsEnabled} />
    </LHoloSectionGroup>

    <LHoloSectionGroup title={labels['section.layerSettings']}>
      <LHoloSettingRow label={labels['label.layerAxis']}>
        <LHoloSelect disabled={!render.ready} label={labels['label.layerAxis']} value={render.layerAxis}
          options={axisOptions} onChange={render.setLayerAxis} />
      </LHoloSettingRow>
      <LHoloSettingRow label={labels['label.displayRange']}>
        <LHoloSelect disabled={!render.ready} label={labels['label.displayRange']} value={render.layerDisplayMode}
          options={rangeOptions} onChange={render.setLayerDisplayMode} />
      </LHoloSettingRow>
      <LHoloSettingRow label={currentLayerLabel} hint={layerHint}>
        <div className="lholo-stepper">
          <LHoloStepButton direction="dec" disabled={!render.ready} onClick={() => render.stepDisplayLayer(-1)} />
          <div className="lholo-stepper-value" style={{ width: Math.max(3.2, Math.min(10, render.layerText.length * 0.7 + 1.2)) + 'rem' }}>
            <LHoloTextField key={currentLayerLabel} disabled={!render.ready} label={currentLayerLabel} value={render.layerText} maxLength={11}
              onChange={render.editDisplayLayer} onBlur={render.commitDisplayLayer} />
          </div>
          <LHoloStepButton direction="inc" disabled={!render.ready} onClick={() => render.stepDisplayLayer(1)} />
        </div>
      </LHoloSettingRow>
    </LHoloSectionGroup>

    <LHoloSectionGroup title={labels['section.correctionStyle']}>
      {([
        ['fill', 'label.correctionFill'],
        ['outline', 'label.correctionOutline'],
      ] as const satisfies readonly [OpacityField, string][]).map(([field, labelKey]) => {
        const label = labels[labelKey];
        const item = render.opacity[field];
        return (
          <LHoloSettingRow key={field} label={label}>
            <div className="lholo-opacity-field">
              <LHoloTextField disabled={!render.ready} label={label} value={item.text} maxLength={4}
                onChange={text => render.editOpacity(field, text)} onBlur={() => render.commitOpacity(field)} />
            </div>
          </LHoloSettingRow>
        );
      })}
    </LHoloSectionGroup>
    <div className="lholo-action-row">
      <LHoloButton disabled={!render.ready} label={labels['button.resetCorrectionStyle']} onClick={render.resetCorrectionStyle} />
    </div>

    <LHoloSectionGroup title={labels['section.seeThrough']}>
      <LHoloCheckbox row disabled={!render.ready} label={labels['checkbox.correctionSeeThrough']}
        value={render.correctionSeeThrough} onChange={render.setCorrectionSeeThrough} />
      <LHoloCheckbox row disabled={!render.ready} label={labels['checkbox.missingSeeThrough']}
        value={render.missingSeeThrough} onChange={render.setMissingSeeThrough} />
    </LHoloSectionGroup>

    <div className="lholo-muted lholo-vibrant-hint">{labels['hint.vibrantVisuals']}</div>
  </>;
}
