import React from 'react';
import type { CustomStructureControls } from '../../application/useCustomStructure';
import { LHoloAutocomplete } from '../../components/LHoloAutocomplete';
import {
  LHoloButton,
  LHoloSectionGroup,
  LHoloSettingRow,
  LHoloTextField,
} from '../../components/LHoloControls';
import { LHoloItemIcon } from '../../components/LHoloItemIcon';
import type { MenuLabels } from '../../platform/bridge';

export function CustomLayerEditorPage({ labels, editor, draft, busy }: {
  labels: MenuLabels;
  editor: CustomStructureControls;
  draft: NonNullable<CustomStructureControls['draft']>;
  busy: boolean;
}) {
  const { y } = editor.dimensions;
  const title = labels[draft.originalLayer === null ? 'capture.addLayer' : 'capture.editLayerTitle'];

  return <>
    <div className="lholo-subpage-header">
      <div className="lholo-hud-title">{title}</div>
      <LHoloButton label={labels['button.back']} onClick={editor.cancelEdit} />
    </div>
    <LHoloSectionGroup>
      <LHoloSettingRow
        label={labels['capture.layer']}
        hint={<span className="lholo-settings-note lholo-custom-layer-hint">{`(${labels['capture.layerInputFormat']})`}</span>}
      >
        <div className="lholo-custom-layer-field">
          <LHoloTextField label={labels['capture.layer']} value={draft.layerText}
            disabled={busy} onChange={layerText => editor.updateDraft({layerText})} />
        </div>
      </LHoloSettingRow>
      <div className="lholo-group-row lholo-custom-block-row">
        <div className="lholo-custom-search-field">
          <LHoloAutocomplete headerLabel={labels['capture.block']} label={labels['capture.searchBlock']} value={draft.query} options={editor.results}
            onChange={query => editor.updateDraft({query, selectedId: ''})}
            onSelect={block => editor.updateDraft({query: block.name, selectedId: block.id})}
            leadingIcon={draft.selectedId ? <LHoloItemIcon iconId={draft.selectedId} /> : undefined}
            renderIcon={option => <LHoloItemIcon iconId={option.id} />}
            disabled={busy || !editor.catalog?.length} emptyLabel={labels['capture.noBlocks']} />
        </div>
      </div>
    </LHoloSectionGroup>
    {y > 0 && (!editor.validLayer || editor.layerTaken) && <div className="lholo-settings-note">
      {labels[editor.layerTaken ? 'capture.layerAlreadyConfigured' : 'capture.invalidLayer']}
    </div>}
    {editor.catalog?.length === 0 && <div className="lholo-settings-note">{labels['capture.noBlocks']}</div>}
    <div className="lholo-action-row">
      <LHoloButton
        label={labels['capture.saveLayer']}
        className="lholo-button-primary"
        disabled={busy || !editor.canSaveLayer}
        onClick={editor.saveLayer}
      />
    </div>
  </>;
}
