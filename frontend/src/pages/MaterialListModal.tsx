import React from 'react';
import {
  formatMaterialDisplayName,
  formatMaterialSummary,
  type MaterialSortColumn,
  type MaterialsControls,
} from '../application/useMaterials';
import type { MenuLabels } from '../platform/bridge';
import { NativeInteractive, NativeScrollView } from '../platform/native';
import { LHoloCheckbox, LHoloCloseButton } from '../components/LHoloControls';
import { LHoloItemIcon } from '../components/LHoloItemIcon';
import { SelectedFileContext } from '../components/SelectedFileContext';

const sortDescSvg = (
  <svg viewBox="0 0 12 12" fill="currentColor" style={{ width: '0.68rem', height: '0.68rem' }}>
    <path d="M2.2 3.8h7.6a.55.55 0 0 1 .44.88L6.44 9.15a.55.55 0 0 1-.88 0L1.76 4.68a.55.55 0 0 1 .44-.88Z" />
  </svg>
);

const sortAscSvg = (
  <svg viewBox="0 0 12 12" fill="currentColor" style={{ width: '0.68rem', height: '0.68rem' }}>
    <path d="M2.2 8.2h7.6a.55.55 0 0 0 .44-.88L6.44 2.85a.55.55 0 0 0-.88 0L1.76 7.32a.55.55 0 0 0 .44.88Z" />
  </svg>
);

function SortHeader({
  label,
  column,
  activeColumn,
  sortDesc,
  className,
  onSort,
}: {
  label: string;
  column: MaterialSortColumn;
  activeColumn: MaterialSortColumn;
  sortDesc: boolean;
  className: string;
  onSort(column: MaterialSortColumn): void;
}) {
  const active = activeColumn === column;
  return (
    <NativeInteractive
      label={label}
      className={className + ' lholo-material-sort-header' + (active ? ' is-active' : '')}
      onClick={() => onSort(column)}
    >
      <div>{label}</div>
      {active ? (
        <div className={'lholo-material-sort-icon ' + (sortDesc ? 'is-desc' : 'is-asc')}>
          {sortDesc ? sortDescSvg : sortAscSvg}
        </div>
      ) : null}
    </NativeInteractive>
  );
}

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
  const {
    materials,
    displayedMaterials,
    ready,
    progressReady,
    sortColumn,
    sortDesc,
    toggleSort,
    hideCompleted,
    setHideCompleted,
    closeModal,
  } = materialsState;

  return <div className="lholo-modal-backdrop">
    <div className="lholo-material-modal">
      <div className="lholo-material-modal-header">
        <div className="lholo-material-title">{labels['material.listTitle']}</div>
        <LHoloCloseButton label={labels['button.close']} onClick={closeModal} />
      </div>
      {sourceName ? <SelectedFileContext label={labels['tasks.currentSelection']} name={sourceName} /> : null}
      {!hasLoadedStructure ? (
        <div className="lholo-muted lholo-material-empty">{labels['status.notLoaded']}</div>
      ) : !ready ? (
        <div className="lholo-muted lholo-material-empty">{labels['material.loading']}</div>
      ) : (
        <>
          <div className="lholo-material-toolbar">
            <div className="lholo-material-summary">
              {formatMaterialSummary(materials, progressReady, labels)}
            </div>
            {materials.length > 0 ? (
              <LHoloCheckbox
                label={labels['material.hideCompleted']}
                value={hideCompleted}
                disabled={!progressReady}
                onChange={setHideCompleted}
              />
            ) : null}
          </div>
          {materials.length === 0 ? (
            <div className="lholo-muted lholo-material-empty">
              {labels['hint.noPlaceableMaterials']}
            </div>
          ) : (
            <div className="lholo-material-table">
              <div className="lholo-material-table-header">
                <div className="lholo-material-col-item">{labels['column.item']}</div>
                <SortHeader
                  label={labels['column.total']}
                  column="total"
                  activeColumn={sortColumn}
                  sortDesc={sortDesc}
                  className="lholo-material-col-num"
                  onSort={toggleSort}
                />
                <SortHeader
                  label={labels['column.missing']}
                  column="missing"
                  activeColumn={sortColumn}
                  sortDesc={sortDesc}
                  className="lholo-material-col-num"
                  onSort={toggleSort}
                />
                <SortHeader
                  label={labels['column.available']}
                  column="available"
                  activeColumn={sortColumn}
                  sortDesc={sortDesc}
                  className="lholo-material-col-num"
                  onSort={toggleSort}
                />
              </div>
              <div className="lholo-material-table-viewport">
                <NativeScrollView>
                  <div className="lholo-material-table-body">
                    {displayedMaterials.map((item, index) => {
                      const completed = progressReady && item.missing === 0;
                      const enough = progressReady && item.missing > 0 && item.available !== null && item.available >= item.missing;
                      const missingClass = !progressReady
                        ? 'lholo-material-num-muted'
                        : item.missing > 0
                          ? 'lholo-material-num-missing'
                          : 'lholo-material-num-complete';
                      const availableClass = item.available === null || item.available === 0
                        ? 'lholo-material-num-muted'
                        : enough
                          ? 'lholo-material-num-complete'
                          : '';
                      return (
                        <div
                          key={item.typeName + ':' + index}
                          className={'lholo-material-table-row' + (completed ? ' is-completed' : '')}
                        >
                          <div className="lholo-material-col-item">
                            <LHoloItemIcon iconId={item.iconId} iconData={item.iconData} />
                            <div className="lholo-material-item-text">
                              <div className="lholo-material-name">{formatMaterialDisplayName(item, labels)}</div>
                              <div className="lholo-material-id">{item.typeName}</div>
                            </div>
                          </div>
                          <div className="lholo-material-col-num">{String(item.count)}</div>
                          <div className={'lholo-material-col-num ' + missingClass}>
                            {progressReady ? String(item.missing) : '-'}
                          </div>
                          <div className={'lholo-material-col-num ' + availableClass}>
                            {item.available === null ? '-' : String(item.available)}
                          </div>
                        </div>
                      );
                    })}
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
