import React from 'react';
import type { HotkeySettingsControls } from '../application/useHotkeySettings';
import type { MenuLabels } from '../platform/bridge';
import {
  LHoloButton,
  LHoloCheckbox,
} from '../components/LHoloControls';

export function HotkeysPage({ labels, hotkeys }: { labels: MenuLabels; hotkeys: HotkeySettingsControls }) {
  // Native metadata owns action identity and grouping; pages never infer slots
  // from their position inside a visible group.
  const groups = new Map<string, typeof hotkeys.bindings>();
  for (const binding of hotkeys.bindings) {
    const group = groups.get(binding.groupKey) ?? [];
    group.push(binding);
    groups.set(binding.groupKey, group);
  }
  return <>
    <div className="lholo-hotkey-list">
      {Array.from(groups, ([groupKey, bindings]) => (
        <div className="lholo-hotkey-group" key={groupKey}>
          <div className="lholo-hotkey-group-title">{labels[groupKey]}</div>
          <div className="lholo-group">
            {bindings.map(definition => {
              const capturing = hotkeys.capturingIndex === definition.slot;
              const bindingText = capturing ? labels['hint.pressKeys'] : definition.display;
              return (
                <div className="lholo-hotkey-row" key={definition.slot}>
                  <div className="lholo-hotkey-label">{labels[definition.labelKey]}</div>
                  <div className="lholo-hotkey-actions">
                    <LHoloButton disabled={!hotkeys.canCommand} label={bindingText}
                      className={'lholo-hotkey-bind' + (capturing ? ' is-capturing' : '')}
                      onClick={() => { if (!capturing) hotkeys.beginCapture(definition.slot); }} />
                    <LHoloButton disabled={!hotkeys.canCommand} label={labels['button.resetHotkey']}
                      onClick={() => hotkeys.resetHotkey(definition.slot)} />
                    <LHoloButton disabled={!hotkeys.canCommand} label={labels['button.clearHotkey']}
                      onClick={() => hotkeys.clearHotkey(definition.slot)} />
                  </div>
                </div>
              );
            })}
            {groupKey === 'hotkeys.group.movement' ? (
              <div className="lholo-hotkey-gesture">
                <LHoloCheckbox row disabled={!hotkeys.ready} label={labels['checkbox.altWheelOffset']}
                  value={hotkeys.altWheelOffsetEnabled} onChange={hotkeys.setAltWheelOffsetEnabled} />
              </div>
            ) : null}
          </div>
        </div>
      ))}
    </div>
    <div className="lholo-action-row">
      <LHoloButton disabled={!hotkeys.canCommand} label={labels['button.resetAllHotkeys']} onClick={hotkeys.resetAllHotkeys} />
    </div>
    <div className="lholo-muted">{labels['hint.chatCommand']}</div>
  </>;
}
