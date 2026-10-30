import React from 'react';
import type { MenuLabels } from '../platform/bridge';
import { LHoloButton } from './LHoloControls';

export const menuPages = ['tasks', 'createStructure', 'writeWorld', 'transform', 'render',
  'hud', 'hotkeys', 'interface', 'experimental'] as const;
export type MenuPage = typeof menuPages[number];

const icons: Record<MenuPage, string> = {
  tasks: 'M3 5h18v16H3V5Zm0 4h18M7 2v6m10-6v6M7 13h4m-4 4h9',
  writeWorld: 'M12 3v12m-4-4 4 4 4-4M4 15v6h16v-6',
  createStructure: 'M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5M8 8h8v8H8Z',
  transform: 'M4 8h15m-4-4 4 4-4 4M20 16H5m4-4-4 4 4 4',
  render: 'm12 3 9 5-9 5-9-5 9-5ZM3 12l9 5 9-5M3 16l9 5 9-5',
  hud: 'M3 4h18v13H3ZM8 21h8m-4-4v4M6 8h4m-4 4h7',
  hotkeys: 'M4 5h16a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1ZM7 9h.01M12 9h.01M17 9h.01M7 12h.01M12 12h.01M17 12h.01M8 16h8',
  interface: 'M3 4h18v16H3Zm6 0v16M3 8h18',
  experimental: 'M9 3h6m-5 0v6L4 19q-1 2 2 2h12q3 0 2-2L14 9V3M7 15h10',
};

export function MenuNavigation({ page, labels, onSelect }: {
  page: MenuPage; labels: MenuLabels; onSelect(page: MenuPage): void;
}) {
  return <div className="lholo-navigation-list">
    <div className="lholo-navigation-selection"
      style={{ transform: 'translateY(' + menuPages.indexOf(page) * 3.1 + 'rem)' }} />
    {menuPages.map(item => <LHoloButton key={item}
    label={item === 'tasks' ? labels['tasks.nav'] : labels['page.' + item] ?? item}
    icon={<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6"
      strokeLinecap="round" strokeLinejoin="round"><path d={icons[item]} /></svg>}
    className={'lholo-navigation-item' + (item === page ? ' is-selected' : '')}
    onClick={() => onSelect(item)} />)}
  </div>;
}
