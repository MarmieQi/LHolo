import React, { useEffect, useRef, useState } from 'react';
import { LHoloTextField } from './LHoloControls';
import { NativeInteractive } from '../platform/native';

type Option = {id: string; name: string};

export function LHoloAutocomplete({ label, headerLabel, value, options, onChange, onSelect, renderIcon, emptyLabel, disabled, leadingIcon }: {
  label: string; headerLabel?: string; value: string; options: readonly Option[]; emptyLabel: string; disabled?: boolean;
  onChange(value: string): void; onSelect(option: Option): void; renderIcon?(option: Option): React.ReactNode;
  leadingIcon?: React.ReactNode;
}) {
  const [open, setOpen] = useState(false);
  const [index, setIndex] = useState(0);
  const root = useRef<HTMLDivElement>(null);
  const visible = open && !disabled && !!value.trim();
  const activeIndex = Math.min(index, options.length - 1);

  useEffect(() => {
    if (!visible) return;
    const outside = (event: MouseEvent) => {
      if (root.current && !root.current.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('mousedown', outside);
    return () => document.removeEventListener('mousedown', outside);
  }, [visible]);

  const choose = (option: Option) => { setOpen(false); onSelect(option); };
  return <div className="lholo-autocomplete" ref={root}>
    <div className="lholo-autocomplete-entry">
      {headerLabel ? <div className="lholo-custom-block-header">
        <div className="lholo-group-row-label">{headerLabel}</div>
        <div className="lholo-settings-note lholo-autocomplete-hint">{label}</div>
      </div> : null}
      <div className="lholo-autocomplete-field">
        <div aria-hidden="true" className={'lholo-autocomplete-measure' + (leadingIcon ? ' has-icon' : '')}>{value}</div>
        <LHoloTextField className="lholo-autocomplete-input" label={label}
          value={value} disabled={disabled} leadingIcon={leadingIcon}
          onChange={text => { onChange(text); setIndex(0); setOpen(true); }}
          onKeyDown={event => {
            const key = event.key || ({13: 'Enter', 27: 'Escape', 38: 'ArrowUp', 40: 'ArrowDown'} as Record<number, string>)[event.keyCode];
            if (key === 'ArrowDown' || key === 'ArrowUp') {
              if (!value.trim()) return false;
              const next = !visible ? 0 : Math.max(0, Math.min(activeIndex + (key === 'ArrowDown' ? 1 : -1), options.length - 1));
              setOpen(true);
              setIndex(next);
            } else if (visible && key === 'Enter') {
              if (activeIndex >= 0) choose(options[activeIndex]);
            } else if (visible && key === 'Escape') {
              setOpen(false);
            } else return false;
            event.preventDefault();
            event.stopPropagation();
            return true;
          }} />
      </div>
      {!headerLabel ? <div className="lholo-settings-note lholo-autocomplete-hint">{`(${label})`}</div> : null}
    </div>
    {visible && <div className="lholo-select-options lholo-autocomplete-options">
      {options.length === 0 ? <div className="lholo-settings-note">{emptyLabel}</div> : options.map((option, position) => {
        const text = `${option.name}(${option.id})`;
        return <NativeInteractive key={option.id} label={text}
          className={'lholo-button lholo-select-option lholo-autocomplete-option' + (position === activeIndex ? ' is-selected' : '')}
          onClick={() => choose(option)}>
          {renderIcon?.(option)}
          <div className="lholo-button-label lholo-autocomplete-option-text">{text}</div>
        </NativeInteractive>;
      })}
    </div>}
  </div>;
}
