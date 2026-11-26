import React, { useEffect, useState } from 'react';
import type { CaptureControls } from '../application/useCapture';
import type { StructurePreviewControls } from '../application/useStructurePreview';
import { useCustomStructure } from '../application/useCustomStructure';
import { selectionMetrics } from '../application/selectionMetrics';
import type { MenuLabels } from '../platform/bridge';
import {
  LHoloButton,
  LHoloCheckbox,
  LHoloSectionGroup,
  LHoloSettingRow,
  LHoloTextField,
} from '../components/LHoloControls';
import { LHoloSelect } from '../components/LHoloSelect';
import { LHoloTabs } from '../components/LHoloTabs';
import { NativeScrollView } from '../platform/native';
import { CustomStructurePanel } from './create/CustomStructurePanel';
import { CustomLayerEditorPage } from './create/CustomLayerEditorPage';

type CreateTab = 'world' | 'custom';
type CreateStructureProps = {
  labels: MenuLabels;
  capture: CaptureControls;
  preview: StructurePreviewControls;
  initialTab?: CreateTab;
};

export function CreateStructurePage({ labels, capture, preview, initialTab = 'world' }: CreateStructureProps) {
  const [tab, setTab] = useState<CreateTab>(initialTab);
  useEffect(() => setTab(initialTab), [initialTab]);
  const custom = useCustomStructure(capture, preview, tab === 'custom');
  // A new scroll view starts each level at its own header. The hook above
  // retains the dimensions and saved layers while either level is visible.
  return <NativeScrollView key={custom.draft ? 'layer-editor' : 'create-structure'}>
    <div className="lholo-content">
      {custom.draft ? <CustomLayerEditorPage labels={labels} editor={custom} draft={custom.draft} busy={capture.busy} /> : <>
        <LHoloTabs<CreateTab> selected={tab} onSelect={setTab} tabs={[
          { id: 'world', label: labels['capture.tabWorld'] },
          { id: 'custom', label: labels['capture.tabCustom'] },
        ]} />
        <div className="lholo-tab-content">
          {tab === 'world' ? <WorldSelection labels={labels} capture={capture} preview={preview} />
            : <CustomStructurePanel labels={labels} capture={capture} preview={preview} editor={custom} />}
        </div>
      </>}
    </div>
  </NativeScrollView>;
}

function WorldSelection({ labels, capture, preview }: CreateStructureProps) {
  const { state, first, second, setFirst, setSecond, includeEntities, setIncludeEntities, valid, draft, busy, editingBusy, submit, finishPoint } = capture;
  const disabled = !state?.worldAvailable || busy;
  const editorDisabled = !state?.worldAvailable || editingBusy;
  const metrics = selectionMetrics(draft);
  return <>
    <LHoloSectionGroup title={labels['section.captureSelection']}>
      <LHoloSettingRow label={labels['label.captureMode']}>
        <LHoloSelect label={labels['label.captureMode']} value={0}
          options={[labels['combo.captureModeClient']]} onChange={() => {}} />
      </LHoloSettingRow>
    </LHoloSectionGroup>
    <div className="lholo-muted">{labels['hint.captureClientOnly']}</div>
    <div className="lholo-status">{state?.status || labels['captureStatus.noWorld']}</div>
    <LHoloSectionGroup>
      {([['first', first, setFirst, 'label.capturePoint1'], ['second', second, setSecond, 'label.capturePoint2']] as const)
        .map(([slot, point, setPoint, label]) => (
          <LHoloSettingRow
            key={slot}
            label={
              <div className="lholo-capture-point-left">
                <div className="lholo-capture-point-title">{labels[label]}</div>
                <LHoloTextField className="lholo-capture-coord-input" label={labels[label] + ' XYZ'} placeholder="X Y Z"
                  value={point} onChange={setPoint} onBlur={() => finishPoint(slot)} disabled={editorDisabled} />
              </div>
            }
          >
            <LHoloButton label={labels['button.usePlayerPosition']}
              disabled={disabled} onClick={() => submit(slot)} />
          </LHoloSettingRow>
        ))}
    </LHoloSectionGroup>
    {!valid && <div className="lholo-notice">{labels['oreui.invalidCoordinates']}</div>}
    {metrics && <CaptureMetrics labels={labels} {...metrics} />}
    <LHoloSectionGroup className="lholo-capture-entities">
      <LHoloCheckbox row label={labels['checkbox.includeEntities']}
        value={includeEntities} onChange={setIncludeEntities} disabled={editorDisabled} />
    </LHoloSectionGroup>
    <div className="lholo-action-row">
      <LHoloButton label={labels['button.clearSelection']} disabled={disabled} onClick={() => submit('clear')} />
      <LHoloButton label={labels['preview.open']}
        disabled={disabled || !preview.canRequest || !valid || !draft?.first || !draft.second}
        onClick={() => state && draft && preview.openCaptureWorld(draft, state.revision)} />
      <LHoloButton label={labels['button.exportMcstructure']}
        disabled={disabled || !valid || !draft?.first || !draft.second} onClick={() => submit('export')} />
    </div>
    {editingBusy && <div className="lholo-notice">{labels['oreui.captureBusy']}</div>}
    {preview.error && <div className="lholo-notice">{preview.error}</div>}
    {state?.error && <div className="lholo-notice">{state.error}</div>}
  </>;
}

function CaptureMetrics({ labels, dimensions, volume }: {
  labels: MenuLabels; dimensions: number[]; volume: string;
}) {
  let axis = 0;
  return <>
    <div className="lholo-muted">{labels['label.captureSize'].replace(/%llu/g, () => String(dimensions[axis++]))}</div>
    <div className="lholo-muted">{labels['label.captureVolume'].replace('%llu', volume)}</div>
  </>;
}
