import React, { useEffect, useRef, useState } from 'react';
import { NativeInteractive } from '../platform/native';
import { LHoloButton } from './LHoloControls';

export function LHoloSelect({ label, value, options, onChange, disabled, disabledOptions = [], size = 'regular' }: {
  label: string; value: number; options: readonly string[]; onChange(value: number): void; disabled?: boolean;
  size?: 'regular' | 'compact';
  disabledOptions?: readonly number[];
}) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (disabled && open) setOpen(false);
  }, [disabled, open]);
  useEffect(() => {
    if (!open) return;
    const outside = (event: MouseEvent) => {
      if (root.current && !root.current.contains(event.target as Node)) setOpen(false);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === 'Escape' || event.keyCode === 27) setOpen(false);
    };
    document.addEventListener('mousedown', outside);
    document.addEventListener('keydown', escape);
    return () => {
      document.removeEventListener('mousedown', outside);
      document.removeEventListener('keydown', escape);
    };
  }, [open]);
  return <div className={'lholo-select' + (size === 'compact' ? ' lholo-select-compact' : '')} ref={root}>
    <NativeInteractive label={label + ' · ' + options[value]} disabled={disabled}
      className="lholo-button lholo-select-trigger"
      onClick={() => { if (!disabled) setOpen(previous => !previous); }}>
      <div className="lholo-button-label">{options[value]}</div>
      <div className="lholo-select-arrow">▼</div>
    </NativeInteractive>
    {open && !disabled && <div className="lholo-select-options">{options.map((option, index) =>
      <LHoloButton key={option} label={option}
        disabled={disabledOptions.includes(index)}
        className={'lholo-select-option' + (index === value ? ' is-selected' : '')}
        onClick={() => { onChange(index); setOpen(false); }} />)}</div>}
  </div>;
}
