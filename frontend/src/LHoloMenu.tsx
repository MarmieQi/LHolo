import React, { useEffect, useState } from 'react';
import { MenuNavigation, type MenuPage } from './components/MenuNavigation';
import { TransformPage } from './pages/TransformPage';
import type { TransformControls } from './application/useTransform';
import { RenderPage } from './pages/RenderPage';
import type { RenderSettingsControls } from './application/useRenderSettings';
import { HudPage } from './pages/HudPage';
import type { HudSettingsControls } from './application/useHudSettings';
import { HotkeysPage } from './pages/HotkeysPage';
import type { HotkeySettingsControls } from './application/useHotkeySettings';
import { InterfacePage } from './pages/InterfacePage';
import type { InterfaceSettingsControls } from './application/useInterfaceSettings';
import { ExperimentalInfoModal, ExperimentalPage } from './pages/ExperimentalPage';
import type { ExperimentalSettingsControls } from './application/useExperimentalSettings';
import { MaterialListModal } from './pages/MaterialListModal';
import type { MaterialsControls } from './application/useMaterials';
import { WriteWorldPage } from './pages/WriteWorldPage';
import type {StructureWriteControls} from './application/useStructureWrite';
import { CreateStructurePage } from './pages/CreateStructurePage';
import type { CaptureControls } from './application/useCapture';
import { TasksPage } from './pages/TasksPage';
import type {TaskControls} from './application/useTasks';
import type {StructurePreviewControls} from './application/useStructurePreview';
import type { MenuLabels, ProjectionState } from './platform/bridge';
import { NativeScrollView } from './platform/native';
import { LHoloButton } from './components/LHoloControls';
import { SelectedFileContext } from './components/SelectedFileContext';

export function LHoloMenu({
  entryPage,
  labels,
  projection,
  capture,
  transform,
  render,
  hud,
  hotkeys,
  interfaceSettings,
  experimental,
  materialsState,
  tasks,
  preview,
  writer,
  onClose,
}: {
  entryPage: 'tasks' | 'hud';
  labels: MenuLabels;
  projection: ProjectionState;
  capture: CaptureControls;
  transform: TransformControls;
  render: RenderSettingsControls;
  hud: HudSettingsControls;
  hotkeys: HotkeySettingsControls;
  interfaceSettings: InterfaceSettingsControls;
  experimental: ExperimentalSettingsControls;
  materialsState: MaterialsControls;
  tasks: TaskControls;
  preview: StructurePreviewControls;
  writer: StructureWriteControls;
  onClose(): void;
}) {
  const [page, setPage] = useState<MenuPage>(entryPage);
  const activeTask = tasks.tasks.find(t => t.id === tasks.state?.activeTaskId);
  const activeFile = activeTask?.layers.find(f => f.id === activeTask.selectedFileId);
  const taskBusy = !!tasks.state?.loading;
  useEffect(() => setPage(entryPage), [entryPage]);
  const selectPage = (next: MenuPage) => {
    materialsState.closeModal();
    experimental.closeModal();
    if (page === 'hotkeys' && next !== 'hotkeys') hotkeys.cancelCapture();
    setPage(next);
  };
  return <div className="lholo-backdrop">
    <div className="lholo-menu">
      <div className="lholo-menu-header">
        <div className="lholo-brand">LHolo</div>
        <LHoloButton label={labels['menu.close']} onClick={onClose} />
      </div>
      <div className="lholo-menu-body">
        <div className="lholo-navigation"><NativeScrollView>
          <MenuNavigation page={page} labels={labels} onSelect={selectPage} />
        </NativeScrollView></div>
        <div className="lholo-content-viewport"><NativeScrollView>
          <div className="lholo-content" key={page}>
            {(page === 'transform' || page === 'render') && activeTask ? (
              <SelectedFileContext label={labels['tasks.currentSelection']}
                name={activeFile?.name ?? labels['tasks.noSelection']} />
            ) : null}
            {page === 'tasks' ? <TasksPage labels={labels} tasksCtrl={tasks} preview={preview} hasProjection={projection.hasProjection} onMaterials={materialsState.openModal} />
              : page === 'createStructure' ? <CreateStructurePage labels={labels} capture={capture} />
              : page === 'writeWorld' ? <WriteWorldPage
                key={JSON.stringify([tasks.currentTaskId, tasks.state?.activeTaskId, tasks.selectedLayerId,
                  tasks.selectedLayer?.path, tasks.selectedLayer?.dimension, writer.state?.worldRevision, writer.state?.dimension])}
                labels={labels} tasks={tasks} writer={writer} />
              : page === 'transform' ? <TransformPage labels={labels} transform={{...transform, ready: transform.ready && !taskBusy && (!activeTask || !!activeFile)}} />
              : page === 'render' ? <RenderPage labels={labels} render={{...render, ready: render.ready && !taskBusy}} />
              : page === 'hud' ? <HudPage labels={labels} hud={hud} worldAvailable={projection.worldAvailable} />
              : page === 'hotkeys' ? <HotkeysPage labels={labels} hotkeys={hotkeys} />
              : page === 'interface' ? <InterfacePage labels={labels} interfaceSettings={interfaceSettings} />
              : <ExperimentalPage labels={labels} experimental={experimental} worldAvailable={projection.worldAvailable} />}
          </div>
        </NativeScrollView></div>
      </div>
    </div>
    <MaterialListModal
      labels={labels}
      hasLoadedStructure={projection.hasProjection}
      sourceName={activeFile?.name ?? ''}
      materialsState={materialsState}
    />
    <ExperimentalInfoModal labels={labels} experimental={experimental} />
  </div>;
}
