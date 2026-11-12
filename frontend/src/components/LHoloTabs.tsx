import React, { useEffect, useRef, useState } from 'react';
import { LHoloButton } from './LHoloControls';

export function LHoloTabs<T extends string>({ tabs, selected, onSelect }: {
  tabs: readonly { id: T; label: string }[];
  selected: T;
  onSelect(tab: T): void;
}) {
  const labelLayout = tabs.map(({ label }) => label).join('\n');
  const rootRef = useRef<HTMLDivElement>(null);
  const [indicator, setIndicator] = useState<{ left: number; width: number } | null>(null);
  useEffect(() => {
    const root = rootRef.current;
    if (!root) return;
    const update = () => {
      const selected = root.querySelector('.lholo-tab.is-selected');
      if (!selected) return;
      const bounds = root.getBoundingClientRect(), target = selected.getBoundingClientRect();
      if (target.width <= 0) return;
      const next = { left: target.left - bounds.left, width: target.width };
      setIndicator(previous => previous?.left === next.left && previous.width === next.width ? previous : next);
    };
    update();
    // Gameface can complete layout after the React commit. Keep this a single
    // layout refresh, with size notifications rather than a permanent frame loop.
    const frame = window.requestAnimationFrame(update);
    window.addEventListener('resize', update);
    const observer = typeof ResizeObserver === 'undefined' ? null : new ResizeObserver(update);
    observer?.observe(root);
    root.querySelectorAll('.lholo-tab').forEach(item => observer?.observe(item));
    return () => {
      window.cancelAnimationFrame(frame);
      window.removeEventListener('resize', update);
      observer?.disconnect();
    };
  }, [selected, labelLayout]);
  return <div className="lholo-tabs" ref={rootRef}>
    {tabs.map(({ id, label }) => <LHoloButton key={id} label={label} onClick={() => onSelect(id)}
      className={'lholo-tab' + (selected === id ? ' is-selected' : '')} />)}
    {indicator && <div className="lholo-tab-indicator" aria-hidden="true"
      style={{ transform: 'translateX(' + indicator.left + 'px)', width: indicator.width + 'px' }} />}
  </div>;
}
