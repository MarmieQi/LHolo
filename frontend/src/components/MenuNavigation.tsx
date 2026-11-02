import React from 'react';
import type { MenuLabels } from '../platform/bridge';
import { LHoloButton } from './LHoloControls';

export const menuPages = ['tasks', 'transform', 'render', 'createStructure', 'writeWorld',
  'hud', 'hotkeys', 'interface', 'experimental'] as const;
export type MenuPage = typeof menuPages[number];

export function MenuNavigation({ page, labels, onSelect }: {
  page: MenuPage; labels: MenuLabels; onSelect(page: MenuPage): void;
}) {
  return <div className="lholo-navigation-list">
    <div className="lholo-navigation-indicator" aria-hidden="true"
      style={{ transform: 'translateY(' + menuPages.indexOf(page) * 2.9 + 'rem)' }}>
      <div key={page} className="lholo-navigation-accent" />
    </div>
    {menuPages.map(item => <LHoloButton key={item}
    label={item === 'tasks' ? labels['tasks.nav'] : labels['page.' + item] ?? item}
    className={'lholo-navigation-item' + (item === page ? ' is-selected' : '')}
    onClick={() => onSelect(item)} />)}
  </div>;
}
