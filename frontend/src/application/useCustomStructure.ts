import { useEffect, useMemo, useState } from 'react';
import type { CustomBlock, CustomLayerFill } from '../platform/bridge';
import type { CaptureControls } from './useCapture';

export const sizeAxes = ['x', 'y', 'z'] as const;

type LayerDraft = { originalLayer: number | null; layerText: string; query: string; selectedId: string };

const positiveInteger = (text: string) => {
  const value = Number(text.trim());
  return /^\d+$/.test(text.trim()) && Number.isInteger(value) && value > 0 && value <= 2147483647 ? value : 0;
};

function parseLayerRange(text: string, height: number) {
  const parts = text.split('-');
  if (parts.length > 2) return null;
  const first = positiveInteger(parts[0]);
  const last = parts.length === 1 ? first : positiveInteger(parts[1]);
  return first > 0 && first <= last && last <= height ? {y: first - 1, endY: last - 1} : null;
}

export function formatLayerRange({y, endY}: CustomLayerFill) {
  return y === endY ? String(y + 1) : `${y + 1}-${endY + 1}`;
}

export function useCustomStructure(capture: CaptureControls, active: boolean) {
  const { subscribeCustomBlocks, requestCustomBlocks } = capture;
  const [catalog, setCatalog] = useState<readonly CustomBlock[] | null>(null);
  const [size, setSize] = useState({x: '', y: '', z: ''});
  const [layers, setLayers] = useState<readonly CustomLayerFill[]>([]);
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
  const range = draft && parseLayerRange(draft.layerText, y);
  const layerTaken = !!(draft && range && layers.some(entry =>
    entry.y !== draft.originalLayer && entry.y <= range.endY && range.y <= entry.endY));
  const fill = validSize && range && !layerTaken && draft?.selectedId
    ? {...range, block: draft.selectedId}
    : null;
  const invalidLayers = layers.some(entry => entry.endY >= y);
  let nextLayer = 0;
  for (const entry of layers) {
    if (entry.y > nextLayer) break;
    nextLayer = entry.endY + 1;
  }

  const updateDraft = (update: Partial<LayerDraft>) => setDraft(current => current && {...current, ...update});
  const addLayer = () => setDraft({originalLayer: null, layerText: String(nextLayer + 1), query: '', selectedId: ''});
  const editLayer = (entry: CustomLayerFill) => {
    setDraft({originalLayer: entry.y, layerText: formatLayerRange(entry),
      query: names.get(entry.block) ?? entry.block, selectedId: entry.block});
  };
  const saveLayer = () => {
    if (!draft || !fill) return;
    setLayers(previous => [...previous.filter(entry => entry.y !== draft.originalLayer), fill].sort((a, b) => a.y - b.y));
    setDraft(null);
  };
  const removeLayer = (target: number) => setLayers(previous => previous.filter(entry => entry.y !== target));

  return { catalog, size, setSize, dimensions: {x, y, z}, volume, invalidField, tooLarge, validSize,
    validLayer: !!range, layerTaken, invalidLayers, names, layers, draft, results,
    canAddLayer: validSize && nextLayer < y, canSaveLayer: !!fill, addLayer, editLayer, updateDraft, saveLayer, removeLayer,
    cancelEdit: () => setDraft(null),
    exportStructure: () => capture.exportCustom({size: {x, y, z}, layers}) };
}

export type CustomStructureControls = ReturnType<typeof useCustomStructure>;
