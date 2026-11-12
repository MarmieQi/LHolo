import React, { useState } from 'react';
import type { CaptureControls } from '../application/useCapture';
import { selectionMetrics } from '../application/selectionMetrics';
import type { MenuLabels } from '../platform/bridge';
import { LHoloButton, LHoloCheckbox, LHoloTextField } from '../components/LHoloControls';
import { LHoloTabs } from '../components/LHoloTabs';

type CreateTab = 'world' | 'custom';
const sizeAxes = ['x', 'y', 'z'] as const;
type SizeDraft = Record<typeof sizeAxes[number], string>;
type CreateStructureProps = { labels: MenuLabels; capture: CaptureControls };

export function CreateStructurePage({ labels, capture }: CreateStructureProps) {
  const [tab, setTab] = useState<CreateTab>('world');
  const [size, setSize] = useState<SizeDraft>({x: '', y: '', z: ''});
  return <>
    <div className="lholo-section-title">{labels['section.captureSource']}</div>
    <LHoloTabs<CreateTab> selected={tab} onSelect={setTab} tabs={[
      { id: 'world', label: labels['capture.tabWorld'] },
      { id: 'custom', label: labels['capture.tabCustom'] },
    ]} />
    <div className="lholo-tab-content">
      {tab === 'world' ? <WorldSelection labels={labels} capture={capture} />
        : <EmptyStructure labels={labels} capture={capture} size={size} setSize={setSize} />}
    </div>
  </>;
}

function EmptyStructure({ labels, capture, size, setSize }: CreateStructureProps & {
  size: SizeDraft; setSize(value: SizeDraft): void;
}) {
  const fields = sizeAxes.map(axis => size[axis].trim());
  const invalidField = fields.some(text => text !== '' && (!/^\d+$/.test(text) || Number(text) === 0));
  const [x, y, z] = fields.map(Number);
  const complete = !invalidField && x > 0 && y > 0 && z > 0;
  const volume = x * y * z;
  const tooLarge = capture.state !== null && complete && volume > capture.state.maxEmptyCells;
  const valid = complete && !tooLarge;
  return <>
    <div className="lholo-section-title">{labels['capture.emptyDimensions']}</div>
    <div className="lholo-size-fields">
      {sizeAxes.map((axis, index) => <div className="lholo-coordinate-row" key={axis}>
        <div className="lholo-capture-point-title">{axis.toUpperCase()}</div>
        <LHoloTextField label={`${labels['capture.emptyDimensions']} ${axis.toUpperCase()}`}
          placeholder={['32', '16', '24'][index]} value={size[axis]} disabled={capture.busy}
          onChange={value => setSize({...size, [axis]: value})} />
      </div>)}
    </div>
    {valid && <CaptureMetrics labels={labels} dimensions={[x, y, z]} volume={String(volume)} />}
    {(invalidField || tooLarge) && <div className="lholo-notice">
      {labels[tooLarge ? 'capture.invalidEmptyDimensions' : 'capture.emptyDimensionsFormat']}
    </div>}
    <div className="lholo-action-row">
      <LHoloButton label={labels['button.exportMcstructure']} disabled={!capture.state || capture.busy || !valid}
        onClick={() => capture.exportEmpty({x, y, z})} />
    </div>
    {capture.state?.customStatus && <div className="lholo-status">{capture.state.customStatus}</div>}
    {capture.editingBusy && <div className="lholo-notice">{labels['oreui.captureBusy']}</div>}
    {capture.state?.error && <div className="lholo-notice">{capture.state.error}</div>}
  </>;
}

function WorldSelection({ labels, capture }: CreateStructureProps) {
  const { state, first, second, setFirst, setSecond, includeEntities, setIncludeEntities, valid, draft, busy, editingBusy, submit, finishPoint } = capture;
  const disabled = !state?.worldAvailable || busy;
  const editorDisabled = !state?.worldAvailable || editingBusy;
  const metrics = selectionMetrics(draft);
  return <>
    <div className="lholo-section-title">{labels['section.captureSelection']}</div>
    <div className="lholo-mode-row"><div>{labels['label.captureMode']}</div>
      <div className="lholo-mode-value">{labels['combo.captureModeClient']}</div></div>
    <div className="lholo-muted">{labels['hint.captureClientOnly']}</div>
    <div className="lholo-status">{state?.status || labels['captureStatus.noWorld']}</div>
    {([['first', first, setFirst, 'label.capturePoint1'], ['second', second, setSecond, 'label.capturePoint2']] as const)
      .map(([slot, point, setPoint, label]) => <div className="lholo-coordinate-row" key={slot}>
        <div className="lholo-capture-point-title">{labels[label]}</div>
        <LHoloTextField label={labels[label] + ' XYZ'} placeholder="X Y Z"
          value={point} onChange={setPoint} onBlur={() => finishPoint(slot)} disabled={editorDisabled} />
        <LHoloButton label={labels['button.usePlayerPosition']}
          disabled={disabled} onClick={() => submit(slot)} />
      </div>)}
    {!valid && <div className="lholo-notice">{labels['oreui.invalidCoordinates']}</div>}
    {metrics && <CaptureMetrics labels={labels} {...metrics} />}
    <div className="lholo-capture-entities"><LHoloCheckbox label={labels['checkbox.includeEntities']}
      value={includeEntities} onChange={setIncludeEntities} disabled={editorDisabled} /></div>
    <div className="lholo-action-row">
      <LHoloButton label={labels['button.clearSelection']} disabled={disabled} onClick={() => submit('clear')} />
      <LHoloButton label={labels['button.exportMcstructure']}
        disabled={disabled || !valid || !draft?.first || !draft.second} onClick={() => submit('export')} />
    </div>
    {editingBusy && <div className="lholo-notice">{labels['oreui.captureBusy']}</div>}
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
