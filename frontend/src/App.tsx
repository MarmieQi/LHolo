import React, { useEffect, useState } from 'react';
import { LHoloMenu } from './LHoloMenu';
import { useTransform } from './application/useTransform';
import { useRenderSettings } from './application/useRenderSettings';
import { useSettings } from './application/useSettings';
import { useHudSettings } from './application/useHudSettings';
import { useHotkeySettings } from './application/useHotkeySettings';
import { useInterfaceSettings } from './application/useInterfaceSettings';
import { useExperimentalSettings } from './application/useExperimentalSettings';
import { useMaterials } from './application/useMaterials';
import { useCapture } from './application/useCapture';
import { useProjectionState } from './application/useProjectionState';
import { useTasks } from './application/useTasks';
import { useStructurePreview } from './application/useStructurePreview';
import { menuBridge, type MenuLabels } from './platform/bridge';
import { FrameCompletion, GameShell } from './platform/native';
import { LHoloButton } from './components/LHoloControls';

function MenuPage() {
  const [entryPage, setEntryPage] = useState<'tasks' | 'hud'>('tasks');
  const [labels, setLabels] = useState<MenuLabels | null>(null);
  const projection = useProjectionState(menuBridge);
  const capture = useCapture(menuBridge);
  const transform = useTransform(menuBridge);
  const render = useRenderSettings(menuBridge);
  const settings = useSettings(menuBridge);
  const hud = useHudSettings(settings, menuBridge);
  const hotkeys = useHotkeySettings(settings);
  const interfaceSettings = useInterfaceSettings(settings);
  const experimental = useExperimentalSettings(settings);
  const materialsState = useMaterials(menuBridge);
  const tasks = useTasks();
  const preview = useStructurePreview();
  useEffect(() => menuBridge.subscribeEntry(setEntryPage), []);
  useEffect(() => menuBridge.subscribe(setLabels), []);
  return <>
    {labels ? <LHoloMenu entryPage={entryPage} labels={labels} projection={projection} capture={capture}
      transform={transform} render={render} hud={hud} hotkeys={hotkeys}
      interfaceSettings={interfaceSettings} experimental={experimental} materialsState={materialsState} tasks={tasks} preview={preview}
      onClose={menuBridge.close} />
      : <div className="lholo-loading"><div>LHolo</div>
        <LHoloButton label="×" onClick={menuBridge.close} /></div>}
    <FrameCompletion />
  </>;
}

export function App() {
  return <GameShell component={MenuPage} />;
}
