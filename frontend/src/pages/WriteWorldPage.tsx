import React, {useEffect, useState} from 'react';
import type { MenuLabels } from '../platform/bridge';
import type {TaskControls} from '../application/useTasks';
import type {StructureWriteControls} from '../application/useStructureWrite';
import {
  LHoloButton,
  LHoloCheckbox,
  LHoloSectionGroup,
  LHoloSettingRow,
} from '../components/LHoloControls';
import {LHoloSelect} from '../components/LHoloSelect';
import {SelectedFileContext} from '../components/SelectedFileContext';

export function WriteWorldPage({labels, tasks, writer}: {
  labels: MenuLabels; tasks: TaskControls; writer: StructureWriteControls;
}) {
  const [mode, setMode] = useState(0);
  const progress = writer.state?.progress;
  const activeJob = !!progress?.active;
  const activeMode = activeJob ? (writer.state!.mode === 'commands' ? 1 : 0) : mode;
  const commandMode = activeMode === 1;
  const available = commandMode ? writer.state?.commandAvailable : writer.state?.localAvailable;
  const [airChoices, setAirChoices] = useState({local: false, commands: true});
  const writeMode = commandMode ? 'commands' : 'local';
  const skipAir = airChoices[writeMode];
  const [skipPlaced, setSkipPlaced] = useState(false);
  const displayedSkipAir = activeJob ? writer.state!.skipAir : skipAir;
  const displayedSkipPlaced = activeJob && commandMode ? writer.state!.skipPlaced : skipPlaced;
  // Adopt a running task into this page's draft so its mode/options survive
  // completion or cancellation after navigating back to the page.
  useEffect(() => {
    if (!activeJob) return;
    const runningMode = writer.state!.mode === 'commands' ? 'commands' : 'local';
    setMode(runningMode === 'commands' ? 1 : 0);
    setAirChoices(previous => ({...previous, [runningMode]: writer.state!.skipAir}));
    if (runningMode === 'commands') setSkipPlaced(writer.state!.skipPlaced);
  }, [activeJob, writer.state?.mode, writer.state?.skipAir, writer.state?.skipPlaced]);
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
  const ownReceipt = resultRequest === writer.state?.ack;
  const showProgress = (activeJob
    || (ownReceipt && (!writer.state?.result || writer.state.result === 'writeWorld.complete'))) && !!progress?.total;
  return <div className="lholo-write-world-page">
    <div className="lholo-write-file">
      <SelectedFileContext label={labels['tasks.currentSelection']} name={file?.name ?? labels['tasks.noSelection']} />
    </div>
    <LHoloSectionGroup>
      <LHoloSettingRow label={labels['writeWorld.mode']}
        hint={labels[commandMode ? 'writeWorld.commandScope' : 'writeWorld.localScope']}>
        <LHoloSelect label={labels['writeWorld.mode']} value={activeMode} onChange={setMode}
          options={[labels['writeWorld.localMode'], labels['writeWorld.commandMode']]}
          disabled={writer.busy} />
      </LHoloSettingRow>
      <LHoloCheckbox row label={labels['writeWorld.skipAir']} value={displayedSkipAir}
        onChange={value => setAirChoices(previous => ({...previous, [writeMode]: value}))} disabled={writer.busy} />
      {commandMode && (
        <LHoloCheckbox row label={labels['writeWorld.skipPlaced']} value={displayedSkipPlaced}
          onChange={setSkipPlaced} disabled={writer.busy} />
      )}
    </LHoloSectionGroup>
    <div className="lholo-muted lholo-write-range">{description.split('\n').map((line, index) => <div key={index}>{line}</div>)}</div>
    <div className="lholo-action-row"><LHoloButton label={labels['page.writeWorld']}
      disabled={!canWrite} onClick={() => setResultRequest(writer.write(tasks.currentTaskId, file!.id, tasks.state!.revision, skipAir,
        writeMode, commandMode && skipPlaced))} />
      {activeJob && progress?.canResume && <LHoloButton label={labels['writeWorld.commandContinue']} disabled={writer.waiting}
        onClick={() => setResultRequest(writer.control('resume'))} />}
      {activeJob && <LHoloButton label={labels['writeWorld.commandCancel']} disabled={writer.waiting}
        onClick={() => setResultRequest(writer.control('cancel'))} />}
    </div>
    {showProgress && <div className="lholo-status">
      <div>{labels['writeWorld.commandTarget'].replace('%s', writer.state!.targetName)}</div>
      <div>{labels['writeWorld.commandProgress'].replace('%s', String(progress!.done)).replace('%s', String(progress!.total))}</div>
      {commandMode && !!progress!.failedSkipped && <div>{labels['writeWorld.commandFailedSkipped'].replace('%llu', String(progress!.failedSkipped))}</div>}
    </div>}
    {activeJob ? <div className="lholo-status">{labels[progress!.state]}</div>
      : writer.busy ? <div className="lholo-status">{labels['writeWorld.busy']}</div>
      : ownReceipt && writer.state?.result ? <div className="lholo-status">{labels[writer.state.result]}</div>
      : ownReceipt && progress?.state ? <div className="lholo-status">{labels[progress.state]}</div>
      : blockedHint ? <div className="lholo-muted">{labels[blockedHint]}</div>
      : null}
  </div>;
}
