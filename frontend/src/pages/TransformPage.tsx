import React from 'react';
import type { OffsetAxis, TransformControls } from '../application/useTransform';
import type { MenuLabels } from '../platform/bridge';
import {
  LHoloSectionGroup,
  LHoloSettingRow,
  LHoloStepButton,
  LHoloTextField,
} from '../components/LHoloControls';
import { LHoloSelect } from '../components/LHoloSelect';

export function TransformPage({ labels, transform }: { labels: MenuLabels; transform: TransformControls }) {
  return <>
    <LHoloSectionGroup>
      <LHoloSettingRow label={labels['label.rotation']}>
        <LHoloSelect disabled={!transform.ready} label={labels['label.rotation']} value={transform.rotation}
          options={['0°', '90°', '180°', '270°']} onChange={transform.setRotation} />
      </LHoloSettingRow>
      <LHoloSettingRow label={labels['label.mirror']}>
        <LHoloSelect disabled={!transform.ready} label={labels['label.mirror']} value={transform.mirror}
          options={[labels['combo.mirrorNone'], 'X', 'Z']} onChange={transform.setMirror} />
      </LHoloSettingRow>
    </LHoloSectionGroup>

    <LHoloSectionGroup>
      {(['x', 'y', 'z'] as OffsetAxis[]).map(axis => {
        const label = labels['label.offset' + axis.toUpperCase()];
        const offset = transform.offsets[axis];
        return (
          <LHoloSettingRow key={axis} label={label}>
            <div className="lholo-stepper">
              <LHoloStepButton direction="dec" disabled={!transform.ready} onClick={() => transform.stepOffset(axis, -1)} />
              <div className="lholo-stepper-value" style={{width: Math.max(3.2, Math.min(10, offset.text.length * 0.7 + 1.2)) + 'rem'}}>
                <LHoloTextField disabled={!transform.ready} label={label} value={offset.text} maxLength={11}
                  onChange={text => transform.editOffset(axis, text)} onBlur={() => transform.commitOffset(axis)} />
              </div>
              <LHoloStepButton direction="inc" disabled={!transform.ready} onClick={() => transform.stepOffset(axis, 1)} />
            </div>
          </LHoloSettingRow>
        );
      })}
    </LHoloSectionGroup>
  </>;
}
