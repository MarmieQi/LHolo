import React, {useEffect, useState} from 'react';
import type { MenuLabels } from '../platform/bridge';
import type {TaskControls} from '../application/useTasks';
import type {StructureWriteControls} from '../application/useStructureWrite';
import {LHoloButton, LHoloCheckbox} from '../components/LHoloControls';
import {LHoloSelect} from '../components/LHoloSelect';
import {SelectedFileContext} from '../components/SelectedFileContext';

export function WriteWorldPage({labels, tasks, writer}: {
  labels: MenuLabels; tasks: TaskControls; writer: StructureWriteControls;
}) {
  const [mode, setMode] = useState(0);
  const activeCommand = writer.state?.mode === 'commands' && writer.state.command.active;
  const commandMode = activeCommand || mode === 1;
  const available = commandMode ? writer.state?.commandAvailable : writer.state?.localAvailable;
  const [airChoices, setAirChoices] = useState({local: false, commands: true});
  const writeMode = commandMode ? 'commands' : 'local';
  const skipAir = airChoices[writeMode];
  const [skipPlaced, setSkipPlaced] = useState(false);
  const displayedSkipAir = activeCommand ? writer.state!.skipAir : skipAir;
  const displayedSkipPlaced = activeCommand ? writer.state!.skipPlaced : skipPlaced;
  // Adopt a running task into this page's draft so its mode/options survive
  // completion or cancellation after navigating back to the page.
  useEffect(() => {
    if (!activeCommand) return;
    setMode(1);
    setAirChoices(previous => ({...previous, commands: writer.state!.skipAir}));
    setSkipPlaced(writer.state!.skipPlaced);
  }, [activeCommand, writer.state?.skipAir, writer.state?.skipPlaced]);
  // A receipt belongs to this page instance, not to the native writer's lifetime.
  const [resultRequest, setResultRequest] = useState<number>();
  const file = tasks.selectedLayer;
  const sameDimension = !!file && file.dimension !== null && writer.state?.dimension != null
    && file.dimension === writer.state.dimension;
  const selected = !!file && !file.hidden && tasks.currentTaskId === tasks.state?.activeTaskId;
  const canWrite = selected && sameDimension && tasks.canCommand && !!available && !writer.busy;
  const blockedHint = !available ? commandMode ? 'writeWorld.commandNoWorld' : 'writeWorld.noLocalWorld'
    : tasks.state?.loading ? 'tasks.loading'
    : !tasks.currentTask ? 'writeWorld.chooseTask'
    : tasks.currentTaskId !== tasks.state?.activeTaskId ? 'writeWorld.loadTask'
    : !file ? 'writeWorld.chooseFile'
    : file.hidden ? 'writeWorld.showFile'
    : !sameDimension ? 'writeWorld.otherDimension' : '';
  const description = labels[commandMode
    ? displayedSkipAir ? 'writeWorld.commandDescriptionSkipAir' : 'writeWorld.commandDescription'
    : displayedSkipAir ? 'writeWorld.descriptionSkipAir' : 'writeWorld.description'];
  const command = writer.state?.command;
  const ownReceipt = resultRequest === writer.state?.ack;
  const showCommand = writer.state?.mode === 'commands' && (activeCommand
    || (ownReceipt && (!writer.state.result || writer.state.result === 'writeWorld.complete'))) && !!command?.total;
  return <div className="lholo-write-world-page">
    <div className="lholo-section-title">{labels['page.writeWorld']}</div>
    <div className="lholo-write-file">
      <SelectedFileContext label={labels['tasks.currentSelection']} name={file?.name ?? labels['tasks.noSelection']} />
    </div>
    <div className="lholo-write-mode-group">
      <div className="lholo-transform-row lholo-setting-row">
        <div className="lholo-transform-label">{labels['writeWorld.mode']}</div>
        <LHoloSelect label={labels['writeWorld.mode']} value={activeCommand ? 1 : mode} onChange={setMode}
          options={[labels['writeWorld.localMode'], labels['writeWorld.commandMode']]}
          disabled={writer.busy} />
      </div>
      <div className="lholo-muted lholo-write-scope">{labels[commandMode ? 'writeWorld.commandScope' : 'writeWorld.localScope']}</div>
    </div>
    <div className="lholo-write-options">
      <div className="lholo-write-option">
        <LHoloCheckbox label={labels['writeWorld.skipAir']} value={displayedSkipAir}
          onChange={value => setAirChoices(previous => ({...previous, [writeMode]: value}))} disabled={writer.busy} />
      </div>
      {commandMode && <div className="lholo-write-option">
        <LHoloCheckbox label={labels['writeWorld.skipPlaced']} value={displayedSkipPlaced}
          onChange={setSkipPlaced} disabled={writer.busy} />
      </div>}
    </div>
    <div className="lholo-muted lholo-write-range">{description.split('\n').map((line, index) => <div key={index}>{line}</div>)}</div>
    <div className="lholo-action-row"><LHoloButton label={labels['page.writeWorld']}
      disabled={!canWrite} onClick={() => setResultRequest(writer.write(tasks.currentTaskId, file!.id, tasks.state!.revision, skipAir,
        writeMode, commandMode && skipPlaced))} />
      {activeCommand && command?.canResume && <LHoloButton label={labels['writeWorld.commandContinue']} disabled={writer.waiting}
        onClick={() => setResultRequest(writer.control('resume'))} />}
      {activeCommand && <LHoloButton label={labels['writeWorld.commandCancel']} disabled={writer.waiting}
        onClick={() => setResultRequest(writer.control('cancel'))} />}
    </div>
    {showCommand && <div className="lholo-status">
      <div>{labels['writeWorld.commandTarget'].replace('%s', writer.state!.targetName)}</div>
      <div>{labels['writeWorld.commandProgress'].replace('%s', String(command!.done)).replace('%s', String(command!.total))}</div>
      {!!command!.failedSkipped && <div>{labels['writeWorld.commandFailedSkipped'].replace('%llu', String(command!.failedSkipped))}</div>}
    </div>}
    {activeCommand ? <div className="lholo-status">{labels[command!.state]}</div>
      : writer.busy ? <div className="lholo-status">{labels['writeWorld.busy']}</div>
      : ownReceipt && writer.state?.result ? <div className="lholo-status">{labels[writer.state.result]}</div>
      : ownReceipt && writer.state?.mode === 'commands' && command?.state
        ? <div className="lholo-status">{labels[command.state]}</div>
      : blockedHint ? <div className="lholo-muted">{labels[blockedHint]}</div>
      : null}
  </div>;
}
