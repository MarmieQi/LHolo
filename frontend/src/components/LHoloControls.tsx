import React, { useEffect, useRef, useState } from 'react';
import { NativeInteractive } from '../platform/native';

interface ButtonProps {
  label: string; onClick(): void; disabled?: boolean; className?: string;
  icon?: React.ReactNode;
}

export function LHoloButton({ label, onClick, disabled, className = '', icon }: ButtonProps) {
  return <NativeInteractive label={label} disabled={disabled}
    className={'lholo-button ' + className} onClick={() => { if (!disabled) onClick(); }}>
    <div className="lholo-button-indicator" />
    {icon ? <div className="lholo-button-icon">{icon}</div> : null}
    <div className="lholo-button-label">{label}</div>
  </NativeInteractive>;
}

export function LHoloTextField({
  label,
  value,
  onChange,
  disabled,
  maxLength,
  onBlur,
  className = '',
  placeholder,
  autoFocus,
  onEnter,
  onEscape,
  onKeyDown,
  leadingIcon,
}: {
  label: string;
  value: string;
  onChange(value: string): void;
  disabled?: boolean;
  maxLength?: number;
  onBlur?(): void;
  className?: string;
  placeholder?: string;
  autoFocus?: boolean;
  onEnter?(): void;
  onEscape?(): void;
  onKeyDown?(event: React.KeyboardEvent<HTMLInputElement>): boolean;
  leadingIcon?: React.ReactNode;
}) {
  const input = useRef<HTMLInputElement>(null);
  const composing = useRef(false);
  const [editing, setEditing] = useState(false);

  useEffect(() => {
    if (autoFocus && input.current) {
      input.current.focus();
    }
  }, [autoFocus]);

  // dom-fiber does not forward the placeholder JSX prop to the input.
  useEffect(() => {
    const element = input.current;
    if (!element) return;
    if (placeholder === undefined) element.removeAttribute('placeholder');
    else element.setAttribute('placeholder', placeholder);
  }, [placeholder]);

  // dom-fiber only forwards a documented subset of JSX event props. Bind the
  // real input event explicitly so keyboard, paste and IME changes all work.
  useEffect(() => {
    const element = input.current;
    if (!element) return;
    const change = () => { if (!disabled) onChange(element.value); };
    const startComposition = () => { composing.current = true; };
    const endComposition = () => { composing.current = false; };
    element.addEventListener('input', change);
    element.addEventListener('compositionstart', startComposition);
    element.addEventListener('compositionend', endComposition);
    return () => {
      element.removeEventListener('input', change);
      element.removeEventListener('compositionstart', startComposition);
      element.removeEventListener('compositionend', endComposition);
    };
  }, [disabled, onChange]);

  const combinedClass = ('lholo-input-shell ' + className + (editing ? ' is-editing' : '')).trim();

  return (
    <NativeInteractive
      label={label}
      disabled={disabled}
      className={combinedClass}
      onClick={() => { if (!disabled) input.current?.focus(); }}
    >
      {leadingIcon && <div className="lholo-input-leading-icon">{leadingIcon}</div>}
      <input
        ref={input}
        className="lholo-text-input"
        type="text"
        value={value}
        disabled={disabled}
        maxLength={maxLength}
        data-narrate={label}
        onFocus={() => setEditing(true)}
        onBlur={() => { setEditing(false); onBlur?.(); }}
        onKeyDown={event => {
          if (composing.current || event.nativeEvent?.isComposing || event.keyCode === 229) return;
          if (onKeyDown?.(event)) return;
          if (event.key === 'Enter' || event.keyCode === 13) {
            input.current?.blur();
            onEnter?.();
          } else if (event.key === 'Escape' || event.keyCode === 27) {
            input.current?.blur();
            onEscape?.();
          }
        }}
      />
    </NativeInteractive>
  );
}

export function LHoloCheckbox({ label, value, onChange, disabled }: {
  label: string; value: boolean; onChange(value: boolean): void; disabled?: boolean;
}) {
  return <NativeInteractive label={label} disabled={disabled}
    className={'lholo-checkbox' + (value ? ' is-checked' : '')}
    onClick={() => { if (!disabled) onChange(!value); }}>
    <div className="lholo-checkbox-box">
      {value ? (
        <svg viewBox="0 0 16 16" style={{ width: '0.92rem', height: '0.92rem' }}>
          <path d="M3.2 8.4L6.4 11.6L12.8 4.6" fill="none" stroke="#ffffff"
            strokeWidth="2.1" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      ) : null}
    </div>
    <div className="lholo-checkbox-label">{label}</div>
  </NativeInteractive>;
}
