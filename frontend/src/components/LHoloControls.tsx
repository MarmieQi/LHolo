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

const minusSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" style={{ width: '0.95rem', height: '0.95rem' }}>
    <path d="M5 12h14" />
  </svg>
);

const plusSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" style={{ width: '0.95rem', height: '0.95rem' }}>
    <path d="M12 5v14M5 12h14" />
  </svg>
);

export function LHoloStepButton({ direction, disabled, onClick }: {
  direction: 'dec' | 'inc'; disabled?: boolean; onClick(): void;
}) {
  return (
    <LHoloButton
      disabled={disabled}
      label={direction === 'dec' ? '-' : '+'}
      icon={direction === 'dec' ? minusSvg : plusSvg}
      className="lholo-stepper-button"
      onClick={onClick}
    />
  );
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

export function LHoloCloseButton({
  label,
  onClick,
  className = '',
}: {
  label: string;
  onClick(): void;
  className?: string;
}) {
  return (
    <NativeInteractive
      label={label}
      className={('lholo-close-button ' + className).trim()}
      onClick={onClick}
    >
      <svg viewBox="0 0 16 16" style={{ width: '0.94rem', height: '0.94rem' }}>
        <path
          d="M3 3L13 13M13 3L3 13"
          fill="none"
          stroke="currentColor"
          strokeWidth="1.6"
          strokeLinecap="round"
        />
      </svg>
      <div className="lholo-button-label">{label}</div>
    </NativeInteractive>
  );
}

export function LHoloSectionGroup({
  title,
  children,
  className = '',
}: {
  title?: string;
  children: React.ReactNode;
  className?: string;
}) {
  return (
    <div className={('lholo-group-section ' + className).trim()}>
      {title ? <div className="lholo-group-title">{title}</div> : null}
      <div className="lholo-group">{children}</div>
    </div>
  );
}

export function LHoloSettingRow({
  label,
  hint,
  children,
  className = '',
}: {
  label: React.ReactNode;
  hint?: React.ReactNode;
  children: React.ReactNode;
  className?: string;
}) {
  return (
    <div className={('lholo-group-row ' + className).trim()}>
      <div className="lholo-group-row-text">
        <div className="lholo-group-row-label">{label}</div>
        {hint ? <div className="lholo-group-row-hint">{hint}</div> : null}
      </div>
      <div className="lholo-group-row-control">{children}</div>
    </div>
  );
}

export function LHoloCheckbox({ label, value, onChange, disabled, row }: {
  label: string; value: boolean; onChange(value: boolean): void; disabled?: boolean; row?: boolean;
}) {
  const box = (
    <div className="lholo-checkbox-box">
      {value ? (
        <svg viewBox="0 0 16 16" style={{ width: '0.92rem', height: '0.92rem' }}>
          <path d="M3.2 8.4L6.4 11.6L12.8 4.6" fill="none" stroke="#ffffff"
            strokeWidth="2.1" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      ) : null}
    </div>
  );
  if (row) {
    return (
      <NativeInteractive label={label} disabled={disabled}
        className={'lholo-checkbox lholo-checkbox-row' + (value ? ' is-checked' : '')}
        onClick={() => { if (!disabled) onChange(!value); }}>
        <div className="lholo-checkbox-label">{label}</div>
        {box}
      </NativeInteractive>
    );
  }
  return <NativeInteractive label={label} disabled={disabled}
    className={'lholo-checkbox' + (value ? ' is-checked' : '')}
    onClick={() => { if (!disabled) onChange(!value); }}>
    {box}
    <div className="lholo-checkbox-label">{label}</div>
  </NativeInteractive>;
}
