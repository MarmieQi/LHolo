import React from 'react';
import { LHoloButton } from './LHoloControls';

export function LHoloTabs<T extends string>({ tabs, selected, onSelect }: {
  tabs: readonly { id: T; label: string }[];
  selected: T;
  onSelect(tab: T): void;
}) {
  return (
    <div className="lholo-tabs">
      {tabs.map(({ id, label }) => (
        <LHoloButton
          key={id}
          label={label}
          onClick={() => onSelect(id)}
          className={'lholo-tab' + (selected === id ? ' is-selected' : '')}
        />
      ))}
    </div>
  );
}
