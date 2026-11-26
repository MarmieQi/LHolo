import { useCallback, useEffect, useMemo, useState } from 'react';
import type { MaterialItem, MaterialsBridge, MaterialsState, MenuLabels } from '../platform/bridge';

export type MaterialSortColumn = 'total' | 'missing' | 'available';

const initialState: MaterialsState = { ready: false, progressReady: false, items: [] };

export function formatMaterialDisplayName(item: MaterialItem, labels: MenuLabels): string {
  if (item.nameKey && labels[item.nameKey]) {
    return labels[item.nameKey];
  }
  return item.displayName || item.typeName;
}

export function formatMaterialSummary(
  materials: readonly MaterialItem[],
  progressReady: boolean,
  labels: MenuLabels
): string {
  let total = 0;
  let missing = 0;
  for (const item of materials) {
    total += item.count;
    missing += item.missing;
  }
  const base = labels['label.materialSummary']
    .replace('%llu', String(total))
    .replace('%zu', String(materials.length));
  const detail = progressReady
    ? labels['material.summaryMissing'].replace('%llu', String(missing))
    : labels['material.scanningProgress'];
  return `${base} · ${detail}`;
}

export function visibleMaterials(
  materials: readonly MaterialItem[],
  progressReady: boolean,
  sortColumn: MaterialSortColumn,
  sortDesc: boolean,
  hideCompleted: boolean
): MaterialItem[] {
  const filtered = hideCompleted && progressReady
    ? materials.filter(item => item.missing > 0)
    : materials.slice();
  const effectiveColumn: MaterialSortColumn = !progressReady && sortColumn === 'missing' ? 'total' : sortColumn;
  const metric = (item: MaterialItem, col: MaterialSortColumn) => {
    if (col === 'missing') return item.missing;
    if (col === 'available') return item.available ?? -1;
    return item.count;
  };
  const direction = sortDesc ? -1 : 1;
  return filtered.sort((left, right) => {
    const primary = metric(left, effectiveColumn) - metric(right, effectiveColumn);
    if (primary !== 0) return primary * direction;
    if (left.count !== right.count) return right.count - left.count;
    return left.typeName.localeCompare(right.typeName);
  });
}

// Native material list modal state backed by StructureUiState / MaterialTracker.
export function useMaterials(bridge: MaterialsBridge) {
  const [isOpen, setIsOpen] = useState(false);
  const [state, setState] = useState<MaterialsState>(initialState);
  const [sortColumn, setSortColumn] = useState<MaterialSortColumn>('missing');
  const [sortDesc, setSortDesc] = useState(true);
  const [hideCompleted, setHideCompleted] = useState(false);
  useEffect(() => bridge.subscribeMaterials(setState), [bridge]);

  const openModal = useCallback(() => {
    setIsOpen(true);
    try { bridge.requestMaterials(); } catch {}
  }, [bridge]);
  const closeModal = useCallback(() => setIsOpen(false), []);
  const toggleSort = useCallback((column: MaterialSortColumn) => {
    if (sortColumn === column) {
      setSortDesc(current => !current);
    } else {
      setSortColumn(column);
      setSortDesc(true);
    }
  }, [sortColumn]);

  const displayedMaterials = useMemo(
    () => visibleMaterials(state.items, state.progressReady, sortColumn, sortDesc, hideCompleted),
    [state.items, state.progressReady, sortColumn, sortDesc, hideCompleted]
  );

  return {
    isOpen,
    ready: state.ready,
    progressReady: state.progressReady,
    openModal,
    closeModal,
    materials: state.items,
    displayedMaterials,
    sortColumn,
    sortDesc,
    toggleSort,
    hideCompleted,
    setHideCompleted,
  };
}

export type MaterialsControls = ReturnType<typeof useMaterials>;
