import React from 'react';
import {
  formatMaterialDisplayName,
  formatMaterialSummary,
  type MaterialsControls,
} from '../application/useMaterials';
import type { MenuLabels } from '../platform/bridge';
import { NativeScrollView } from '../platform/native';
import { LHoloButton } from '../components/LHoloControls';
import { LHoloItemIcon } from '../components/LHoloItemIcon';
import { SelectedFileContext } from '../components/SelectedFileContext';

export function MaterialListModal({
  labels,
  hasLoadedStructure,
  sourceName,
  materialsState,
}: {
  labels: MenuLabels;
  hasLoadedStructure: boolean;
  sourceName: string;
  materialsState: MaterialsControls;
}) {
  if (!materialsState.isOpen) return null;
  const { materials, ready, closeModal } = materialsState;

  return <div className="lholo-modal-backdrop">
    <div className="lholo-material-modal">
      <div className="lholo-material-modal-header">
        <div className="lholo-material-title">{labels['material.listTitle']}</div>
        <LHoloButton label={labels['button.close']} onClick={closeModal} />
      </div>
      {sourceName ? <SelectedFileContext label={labels['tasks.currentSelection']} name={sourceName} /> : null}
      {!hasLoadedStructure ? (
        <div className="lholo-muted lholo-material-empty">{labels['status.notLoaded']}</div>
      ) : !ready ? (
        <div className="lholo-muted lholo-material-empty">{labels['material.loading']}</div>
      ) : (
        <>
          <div className="lholo-material-summary">
            {formatMaterialSummary(materials, labels)}
          </div>
          {materials.length === 0 ? (
            <div className="lholo-muted lholo-material-empty">
              {labels['hint.noPlaceableMaterials']}
            </div>
          ) : (
            <div className="lholo-material-table">
              <div className="lholo-material-table-header">
                <div className="lholo-material-col-item">{labels['column.item']}</div>
                <div className="lholo-material-col-id">{labels['column.identifier']}</div>
                <div className="lholo-material-col-total">{labels['column.total']}</div>
              </div>
              <div className="lholo-material-table-viewport">
                <NativeScrollView>
                  <div className="lholo-material-table-body">
                    {materials.map((item, index) => (
                      <div key={item.typeName + ':' + index} className="lholo-material-table-row">
                        <div className="lholo-material-col-item">
                          <LHoloItemIcon iconId={item.iconId} iconData={item.iconData} />
                          <div className="lholo-material-name">{formatMaterialDisplayName(item, labels)}</div>
                        </div>
                        <div className="lholo-material-col-id">{item.typeName}</div>
                        <div className="lholo-material-col-total">{String(item.count)}</div>
                      </div>
                    ))}
                  </div>
                </NativeScrollView>
              </div>
            </div>
          )}
        </>
      )}
    </div>
  </div>;
}
