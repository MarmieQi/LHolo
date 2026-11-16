import React from 'react';
import type { CaptureControls } from '../../application/useCapture';
import { sizeAxes, type CustomStructureControls } from '../../application/useCustomStructure';
import { LHoloButton, LHoloTextField } from '../../components/LHoloControls';
import { LHoloItemIcon } from '../../components/LHoloItemIcon';
import type { MenuLabels } from '../../platform/bridge';

export function CustomStructurePanel({ labels, capture, editor }: {
  labels: MenuLabels; capture: CaptureControls; editor: CustomStructureControls;
}) {
  const { x, y, z } = editor.dimensions;
  const layerLabel = (layer: number) => labels['capture.layerNumber'].replace('%d', String(layer + 1));
  let dimensionIndex = 0;
  const dimensions = labels['label.captureSize'].replace(/%llu/g, () => String([x, y, z][dimensionIndex++]));
  const volume = labels['label.captureVolume'].replace('%llu', String(editor.volume));
  return <>
    <div className="lholo-section-title">{labels['capture.customDimensions']}</div>
    <div className="lholo-size-fields">
      {sizeAxes.map((axis, index) => <div className="lholo-coordinate-row" key={axis}>
        <div className="lholo-capture-point-title">{axis.toUpperCase()}</div>
        <LHoloTextField label={`${labels['capture.customDimensions']} ${axis.toUpperCase()}`}
          placeholder={['32', '16', '24'][index]} value={editor.size[axis]} disabled={capture.busy}
          onChange={value => editor.setSize({...editor.size, [axis]: value})} />
      </div>)}
    </div>
    {editor.validSize && <div className="lholo-settings-note">{`${dimensions} · ${volume}`}</div>}
    {(editor.invalidField || editor.tooLarge) && <div className="lholo-settings-note">
      {labels[editor.tooLarge ? 'capture.invalidCustomDimensions' : 'capture.customDimensionsFormat']}
    </div>}
    <div className="lholo-custom-section">
      <div className="lholo-hud-header">
        <div className="lholo-hud-title">{labels['capture.layerFill']}</div>
        <LHoloButton label={labels['capture.addLayer']} disabled={capture.busy || !editor.canAddLayer}
          onClick={editor.addLayer} />
      </div>
      {editor.sortedLayers.map(({y: layer, block}) => <div className="lholo-file-row lholo-custom-layer-row" key={layer}>
        <div className="lholo-custom-layer-index">{layerLabel(layer)}</div>
        <div className="lholo-custom-layer-block"><LHoloItemIcon iconId={block} /><div>{editor.names.get(block) ?? block}</div></div>
        <LHoloButton label={labels['capture.editLayer']} disabled={capture.busy} onClick={() => editor.editLayer(layer, block)} />
        <LHoloButton label={labels['capture.removeLayer']} disabled={capture.busy} onClick={() => editor.removeLayer(layer)} />
      </div>)}
      {y > 0 && editor.invalidLayers && <div className="lholo-settings-note">{labels['capture.invalidLayer']}</div>}
      <div className="lholo-settings-note">{labels['capture.unfilledLayersAir']}</div>
    </div>
    <div className="lholo-action-row">
      <LHoloButton label={labels['button.exportMcstructure']}
        disabled={!capture.state || capture.busy || !editor.validSize || editor.invalidLayers} onClick={editor.exportStructure} />
    </div>
    {capture.customRequestTooLarge && <div className="lholo-settings-note">{labels['capture.tooManyLayers']}</div>}
    {capture.state?.customStatus && <div className="lholo-status">{capture.state.customStatus}</div>}
    {capture.editingBusy && <div className="lholo-settings-note">{labels['oreui.captureBusy']}</div>}
    {capture.state?.error && <div className="lholo-settings-note">{capture.state.error}</div>}
  </>;
}
