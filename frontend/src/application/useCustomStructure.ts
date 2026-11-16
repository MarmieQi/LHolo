import { useEffect, useMemo, useState } from 'react';
import type { CustomBlock } from '../platform/bridge';
import type { CaptureControls } from './useCapture';

export const sizeAxes = ['x', 'y', 'z'] as const;

type LayerDraft = { originalLayer: number | null; layerText: string; query: string; selectedId: string };

const positiveInteger = (text: string) => {
  const value = Number(text.trim());
  return /^\d+$/.test(text.trim()) && Number.isInteger(value) && value > 0 && value <= 2147483647 ? value : 0;
};

export function useCustomStructure(capture: CaptureControls, active: boolean) {
  const { subscribeCustomBlocks, requestCustomBlocks } = capture;
  const [catalog, setCatalog] = useState<readonly CustomBlock[] | null>(null);
  const [size, setSize] = useState({x: '', y: '', z: ''});
  const [layers, setLayers] = useState<ReadonlyMap<number, string>>(new Map());
  const [draft, setDraft] = useState<LayerDraft | null>(null);
  useEffect(() => subscribeCustomBlocks(setCatalog), [subscribeCustomBlocks]);
  useEffect(() => { if (active && catalog === null) requestCustomBlocks(); }, [active, catalog, requestCustomBlocks]);

  const names = useMemo(() => new Map(catalog?.map(block => [block.id, block.name])), [catalog]);
  const query = draft?.query ?? '';
  const results = useMemo(() => {
    const term = query.trim().toLowerCase();
    if (!term || !catalog) return [];
    const matches = catalog.flatMap(block => {
      const name = block.name.toLowerCase();
      const id = block.id.toLowerCase();
      const shortId = id.slice(id.indexOf(':') + 1);
      const rank = name === term || id === term || shortId === term ? 0
        : name.startsWith(term) || shortId.startsWith(term) ? 1
        : name.includes(term) || id.includes(term) ? 2 : -1;
      return rank < 0 ? [] : [{block, rank}];
    });
    return matches.sort((a, b) => a.rank - b.rank || a.block.id.localeCompare(b.block.id))
      .slice(0, 8).map(match => match.block);
  }, [catalog, query]);

  const [x, y, z] = sizeAxes.map(axis => positiveInteger(size[axis]));
  const invalidField = sizeAxes.some(axis => size[axis].trim() !== '' && !positiveInteger(size[axis]));
  const volume = x * y * z;
  const tooLarge = !!capture.state && volume > capture.state.maxCustomCells;
  const validSize = volume > 0 && !tooLarge;
  const layer = positiveInteger(draft?.layerText ?? '') - 1;
  const validLayer = layer >= 0 && layer < y;
  const layerTaken = !!draft && draft.originalLayer !== layer && layers.has(layer);
  const canSaveLayer = validSize && validLayer && !layerTaken && !!draft?.selectedId;
  const sortedLayers = Array.from(layers, ([y, block]) => ({y, block})).sort((a, b) => a.y - b.y);
  const invalidLayers = sortedLayers.some(entry => entry.y >= y);
  let nextLayer = 0;
  while (layers.has(nextLayer)) ++nextLayer;

  const updateDraft = (update: Partial<LayerDraft>) => setDraft(current => current && {...current, ...update});
  const addLayer = () => setDraft({originalLayer: null, layerText: String(nextLayer + 1), query: '', selectedId: ''});
  const editLayer = (target: number, id: string) => {
    setDraft({originalLayer: target, layerText: String(target + 1), query: names.get(id) ?? id, selectedId: id});
  };
  const saveLayer = () => {
    if (!draft || !canSaveLayer) return;
    setLayers(previous => {
      const next = new Map(previous);
      if (draft.originalLayer !== null) next.delete(draft.originalLayer);
      return next.set(layer, draft.selectedId);
    });
    setDraft(null);
  };
  const removeLayer = (target: number) => {
    const next = new Map(layers);
    next.delete(target);
    setLayers(next);
  };

  return { catalog, size, setSize, dimensions: {x, y, z}, volume, invalidField, tooLarge, validSize,
    validLayer, layerTaken, invalidLayers, names, sortedLayers, draft, results,
    canAddLayer: validSize && nextLayer < y, canSaveLayer, addLayer, editLayer, updateDraft, saveLayer, removeLayer,
    cancelEdit: () => setDraft(null),
    exportStructure: () => capture.exportCustom({size: {x, y, z}, layers: sortedLayers}) };
}

export type CustomStructureControls = ReturnType<typeof useCustomStructure>;
