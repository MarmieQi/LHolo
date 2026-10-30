import React, {useState} from 'react';
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
  const [skipAir, setSkipAir] = useState(false);
  // A receipt belongs to this page instance, not to the native writer's lifetime.
  const [resultRequest, setResultRequest] = useState<number>();
  const file = tasks.selectedLayer;
  const selected = !!file && !file.hidden && tasks.currentTaskId === tasks.state?.activeTaskId;
  const canWrite = selected && tasks.canCommand && !!writer.state?.localAvailable && !writer.busy;
  const blockedHint = !writer.state?.localAvailable ? 'writeWorld.noLocalWorld'
    : tasks.state?.loading ? 'tasks.loading'
    : !tasks.currentTask ? 'writeWorld.chooseTask'
    : tasks.currentTaskId !== tasks.state?.activeTaskId ? 'writeWorld.loadTask'
    : !file ? 'writeWorld.chooseFile'
    : file.hidden ? 'writeWorld.showFile' : '';
  const [summary, ...details] = labels[skipAir ? 'writeWorld.descriptionSkipAir' : 'writeWorld.description'].split('\n\n');
  return <div className="lholo-write-world-page">
    <div className="lholo-section-title">{labels['page.writeWorld']}</div>
    <SelectedFileContext label={labels['tasks.currentSelection']} name={file?.name ?? labels['tasks.noSelection']} />
    <div className="lholo-muted">{summary}</div>
    <div className="lholo-interface-row lholo-setting-row">
      <div className="lholo-interface-label">{labels['writeWorld.mode']}</div>
      <LHoloSelect label={labels['writeWorld.mode']} value={mode} onChange={setMode}
        options={[labels['writeWorld.localMode'], labels['writeWorld.commandMode']]}
        disabledOptions={[1]} disabled={writer.busy} />
    </div>
    <div className="lholo-render-check"><LHoloCheckbox label={labels['writeWorld.skipAir']}
      value={skipAir} onChange={setSkipAir} disabled={writer.busy} /></div>
    {details.map((paragraph, index) =>
      <div className="lholo-muted" key={index}>{paragraph}</div>)}
    <div className="lholo-action-row"><LHoloButton label={labels['page.writeWorld']}
      disabled={!canWrite} onClick={() => setResultRequest(writer.write(tasks.currentTaskId, file!.id, tasks.state!.revision, skipAir))} /></div>
    {writer.busy ? <div className="lholo-status">{labels['writeWorld.busy']}</div>
      : blockedHint ? <div className="lholo-muted">{labels[blockedHint]}</div>
      : resultRequest === writer.state?.ack && writer.state?.result
        && <div className="lholo-status">{labels[writer.state.result]}</div>}
  </div>;
}
