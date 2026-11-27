import React, { useEffect, useRef, useState } from 'react';
import * as Facet from '@react-facet/core';
import type { InteractiveProps, NativeRuntime } from '../src/platform/native';

function Interactive({ label, className = '', disabled, children, onClick }: InteractiveProps) {
  const element = useRef<HTMLDivElement>(null);
  const [hover, setHover] = useState(false);
  const [focus, setFocus] = useState(false);
  const [pressed, setPressed] = useState(false);
  useEffect(() => {
    element.current?.setAttribute('tabindex', disabled ? '-1' : '0');
    element.current?.setAttribute('role', 'button');
    element.current?.setAttribute('aria-label', label);
    element.current?.setAttribute('aria-disabled', String(!!disabled));
  }, [disabled, label]);
  return <div ref={element} className={className + (disabled ? ' lholo-control-disabled' : '')
    + (hover && !disabled ? ' lholo-control-hovered' : '') + (focus ? ' lholo-control-focused' : '')
    + (pressed && !disabled ? ' lholo-control-pressed' : '')}
    onClick={() => { if (!disabled) onClick(); }}
    onMouseEnter={() => setHover(true)} onMouseLeave={() => { setHover(false); setPressed(false); }}
    onMouseDown={() => setPressed(true)} onMouseUp={() => setPressed(false)}
    onFocus={() => setFocus(true)} onBlur={() => setFocus(false)}
    onKeyDown={event => {
      if (event.target === event.currentTarget && (event.key === 'Enter' || event.key === ' ')) {
        event.preventDefault(); if (!disabled) onClick();
      }
    }}>{children}</div>;
}

export function installPreviewRuntime() {
  const runtime: NativeRuntime = {
    React, Facet, Interactive,
    Shell: ({ component: Component }) => <Component />,
    Scroll: ({ children }) => <div className="lholo-preview-scroll">{children}</div>,
    Completion: () => null,
    // Browser preview has no inventory renderer; never issue game URLs here.
    ItemIcon: () => <div className="lholo-preview-item-icon"><svg viewBox="0 0 24 24"
      fill="none" stroke="currentColor" strokeWidth="1.3">
      <path d="m12 3 9 5v9l-9 4-9-4V8l9-5Zm0 10L3 8m9 5 9-5m-9 5v8" />
    </svg></div>,
    setUiScale: scale => {
      const clamped = Number.isFinite(scale) ? Math.max(0.5, Math.min(2.0, scale)) : 1.0;
      document.documentElement.style.fontSize = `${16 * clamped}px`;
    },
  };
  window.__LHoloOreUiRuntime = runtime;
}
