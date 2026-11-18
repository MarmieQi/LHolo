import React, { useEffect, useState } from 'react';
import { LHoloButton, LHoloTextField } from '../components/LHoloControls';
import { LHoloSelect } from '../components/LHoloSelect';
import { SelectedFileContext } from '../components/SelectedFileContext';
import { NativeInteractive } from '../platform/native';
import type {TaskControls} from '../application/useTasks';
import type {StructurePreviewControls} from '../application/useStructurePreview';
import type { MenuLabels } from '../platform/bridge';

const eyeOpenSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" style={{ width: '1.12rem', height: '1.12rem' }}>
    <path d="M2.5 12s3.5-6.5 9.5-6.5S21.5 12 21.5 12s-3.5 6.5-9.5 6.5S2.5 12 2.5 12Z" />
    <circle cx="12" cy="12" r="3" />
  </svg>
);

const eyeClosedSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" style={{ width: '1.12rem', height: '1.12rem' }}>
    <path d="M10.73 5.58A9.8 9.8 0 0 1 12 5.5c6 0 9.5 6.5 9.5 6.5a14.5 14.5 0 0 1-2.22 3.05" />
    <path d="M6.61 7.05A14.2 14.2 0 0 0 2.5 12s3.5 6.5 9.5 6.5a9.6 9.6 0 0 0 4.65-1.18" />
    <path d="M9.88 9.88a3 3 0 1 0 4.24 4.24" />
    <line x1="3" y1="3" x2="21" y2="21" />
  </svg>
);

const arrowUpSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" style={{ width: '1.05rem', height: '1.05rem' }}>
    <path d="M12 19V5" />
    <path d="M6.5 10.5 12 5l5.5 5.5" />
  </svg>
);

const arrowDownSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" style={{ width: '1.05rem', height: '1.05rem' }}>
    <path d="M12 5v14" />
    <path d="M6.5 13.5 12 19l5.5-5.5" />
  </svg>
);

const trashSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" style={{ width: '1.05rem', height: '1.05rem' }}>
    <path d="M4 6.5h16" />
    <path d="M9.5 6.5V4.5a1 1 0 0 1 1-1h3a1 1 0 0 1 1 1v2" />
    <path d="M6.5 6.5v12.5a1.5 1.5 0 0 0 1.5 1.5h8a1.5 1.5 0 0 0 1.5-1.5V6.5" />
    <path d="M10 10.5v6" />
    <path d="M14 10.5v6" />
  </svg>
);

const plusSvg = (
  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" style={{ width: '1.05rem', height: '1.05rem' }}>
    <path d="M12 5v14M5 12h14" />
  </svg>
);

type TaskModal =
  | {mode: 'create'}
  | {mode: 'rename' | 'deleteTask'; taskId: string; name: string}
  | {mode: 'removeFile'; taskId: string; fileId: string; name: string};

export function TasksPage({ labels, tasksCtrl, preview, hasProjection, onMaterials }: {
  labels: MenuLabels; tasksCtrl: TaskControls; preview: StructurePreviewControls; hasProjection: boolean; onMaterials(): void;
}) {
  const [taskModal, setTaskModal] = useState<TaskModal | null>(null);
  const [taskNameDraft, setTaskNameDraft] = useState('');

  const currentTask = tasksCtrl.currentTask;
  const layers = tasksCtrl.layers;
  const selectedLayer = tasksCtrl.selectedLayer;
  const errorFile = tasksCtrl.tasks.flatMap(t => t.layers).find(f => f.id === tasksCtrl.state?.errorFileId);
  const disabled = !tasksCtrl.canCommand;
  const showCloseAction = !!tasksCtrl.state?.loading || (!!currentTask && currentTask.id === tasksCtrl.state?.activeTaskId);
  const [pathDraft, setPathDraft] = useState('');
  useEffect(() => setPathDraft(selectedLayer?.path ?? ''), [selectedLayer?.id, selectedLayer?.path]);
  const commitPath = () => {if (selectedLayer) tasksCtrl.changeLayerPath(selectedLayer.id, pathDraft);};

  const taskOptions = tasksCtrl.tasks.length ? tasksCtrl.tasks.map(t => t.name) : [labels['tasks.emptyTasks']];
  const currentTaskIndex = tasksCtrl.tasks.findIndex(t => t.id === tasksCtrl.currentTaskId);
  const nameModal = taskModal?.mode === 'create' || taskModal?.mode === 'rename';
  const modalTitle = taskModal ? labels[{
    create: 'tasks.createTitle', rename: 'tasks.renameTitle',
    deleteTask: 'tasks.delete', removeFile: 'tasks.removeFile',
  }[taskModal.mode]] : '';

  // 开始新建任务（弹出窗口）
  const handleStartCreate = () => {
    setTaskNameDraft('');
    setTaskModal({mode: 'create'});
  };

  // 开始重命名任务（弹出窗口）
  const handleStartRename = () => {
    if (!currentTask) return;
    setTaskNameDraft(currentTask.name);
    setTaskModal({mode: 'rename', taskId: currentTask.id, name: currentTask.name});
  };

  const handleConfirmTaskModal = () => {
    if (!taskModal || disabled) return;
    const trimmed = taskNameDraft.trim();
    switch (taskModal.mode) {
      case 'create':
        if (!trimmed) return;
        tasksCtrl.createTask(trimmed); break;
      case 'rename':
        if (!trimmed) return;
        tasksCtrl.renameTask(taskModal.taskId, trimmed); break;
      case 'deleteTask': tasksCtrl.deleteTask(taskModal.taskId); break;
      case 'removeFile': tasksCtrl.removeLayer(taskModal.fileId, taskModal.taskId); break;
    }
    setTaskModal(null);
  };

  const handleCancelTaskModal = () => {
    setTaskModal(null);
  };


  const taskStatus = tasksCtrl.state?.loading
    ? labels['tasks.loading']
    : currentTask?.id === tasksCtrl.state?.activeTaskId
      ? labels['tasks.active']
      : '';

  return (
    <div className="lholo-tasks-page">
      {taskStatus ? <div className="lholo-muted lholo-task-active-badge">{taskStatus}</div> : null}

      <div className="lholo-tasks-header-card">
        <div className="lholo-task-control-row">
          <LHoloSelect
            label={labels['tasks.name']}
            size="compact"
            value={currentTaskIndex >= 0 ? currentTaskIndex : 0}
            options={taskOptions}
            disabled={disabled || !tasksCtrl.tasks.length}
            onChange={idx => {
              const target = tasksCtrl.tasks[idx];
              if (target) tasksCtrl.selectTask(target.id);
            }}
          />
          <LHoloButton label={labels['tasks.create']} disabled={disabled} onClick={handleStartCreate} />
          <div className="lholo-task-control-secondary">
            <LHoloButton label={labels['tasks.rename']} disabled={disabled || !currentTask} onClick={handleStartRename} />
            <LHoloButton
              label={labels['tasks.delete']}
              disabled={disabled || !currentTask}
              onClick={() => currentTask && setTaskModal({mode: 'deleteTask', taskId: currentTask.id, name: currentTask.name})}
            />
          </div>
        </div>

        <div className="lholo-action-row">
          <LHoloButton label={labels[showCloseAction ? 'tasks.close' : 'tasks.activate']}
            disabled={showCloseAction ? !tasksCtrl.canClose : disabled || !currentTask || !tasksCtrl.state?.worldAvailable}
            onClick={showCloseAction ? tasksCtrl.close : tasksCtrl.activate} />
          <LHoloButton label={labels['material.listTitle']} disabled={disabled || !hasProjection || !selectedLayer || currentTask?.id !== tasksCtrl.state?.activeTaskId} onClick={onMaterials} />
        </div>
      </div>
      {tasksCtrl.state?.error ? <div className="lholo-muted lholo-task-error" role="alert">
        <span className="lholo-task-error-heading">{`${labels['tasks.error']}:${errorFile ? ' ' + errorFile.name : ''}`}</span>
        <span className="lholo-task-error-message">{tasksCtrl.state.error}</span>
      </div> : null}
      {tasksCtrl.requestRejected ? <div className="lholo-muted" role="alert">{labels['tasks.requestTooLarge']}</div> : null}
      {preview.error ? <div className="lholo-muted" role="alert">{preview.error}</div> : null}

      <div className="lholo-task-section-title">
        {labels['tasks.layers']}
      </div>

      <div className="lholo-tasks-layer-list">
        {!layers.length ? (
          <div className="lholo-muted" style={{ padding: '0.8rem', textAlign: 'center' }}>
            {labels['tasks.emptyFiles']}
          </div>
        ) : null}

        {layers.map((layer, index) => {
          const isSel = selectedLayer?.id === layer.id;
          return (
            <div
              className={'lholo-tasks-layer-row' + (isSel ? ' is-selected' : '') + (layer.hidden ? ' is-hidden' : '')}
              key={layer.id}
            >
              {isSel ? <div className="lholo-tasks-layer-indicator" /> : null}
              <LHoloButton
                label={layer.hidden ? labels['tasks.show'] : labels['tasks.hide']}
                icon={layer.hidden ? eyeClosedSvg : eyeOpenSvg}
                className={'lholo-tasks-eye-btn' + (layer.hidden ? ' is-hidden' : ' is-visible')}
                disabled={disabled}
                onClick={() => tasksCtrl.toggleLayerHidden(layer.id)}
              />
              <div className="lholo-tasks-layer-num">{index + 1}</div>
              <NativeInteractive disabled={disabled} label={layer.name + (isSel ? ' ' + labels['tasks.selected'] : '') + (layer.hidden ? ' ' + labels['tasks.hidden'] : '')} className="lholo-tasks-layer-name"
                onClick={() => tasksCtrl.setSelectedLayerId(layer.id)}>
                <span className="lholo-tasks-layer-title">{layer.name}</span>
                {isSel && labels['tasks.currentBadge'] ? (
                  <span className="lholo-tasks-current-badge">{labels['tasks.currentBadge']}</span>
                ) : null}
              </NativeInteractive>
              <div className="lholo-tasks-layer-actions">
                <LHoloButton label={labels['preview.open']} className="lholo-tasks-preview-btn"
                  disabled={disabled || !preview.canRequest || !tasksCtrl.state?.worldAvailable}
                  onClick={() => currentTask && tasksCtrl.state && preview.open(currentTask.id, layer.id, tasksCtrl.state.revision)} />
                <LHoloButton
                  label={labels['tasks.moveUp']}
                  icon={arrowUpSvg}
                  className="lholo-tasks-arrow-btn"
                  disabled={disabled || index === 0}
                  onClick={() => tasksCtrl.moveLayer(layer.id, -1)}
                />
                <LHoloButton
                  label={labels['tasks.moveDown']}
                  icon={arrowDownSvg}
                  className="lholo-tasks-arrow-btn"
                  disabled={disabled || index === layers.length - 1}
                  onClick={() => tasksCtrl.moveLayer(layer.id, 1)}
                />
                <LHoloButton
                  label={labels['tasks.removeFile']}
                  icon={trashSvg}
                  className="lholo-tasks-arrow-btn lholo-tasks-delete-btn"
                  disabled={disabled}
                  onClick={() => currentTask && setTaskModal({mode: 'removeFile', taskId: currentTask.id, fileId: layer.id, name: layer.name})}
                />
              </div>
            </div>
          );
        })}
        <LHoloButton
          label={labels['tasks.addFile']}
          icon={plusSvg}
          className="lholo-tasks-add-row"
          disabled={disabled || !currentTask}
          onClick={() => tasksCtrl.addLayer()}
        />
      </div>

      {/* 选中文件 */}
      <SelectedFileContext className="lholo-task-file-heading" label={labels['tasks.currentSelection']}
        name={selectedLayer?.name ?? labels['tasks.noSelection']} />

      {selectedLayer ? (
        <div className="lholo-tasks-file-box">
          <div className="lholo-file-row">
            <div style={{ flex: 1 }}>
              <LHoloTextField
                className="lholo-input-full"
                label={labels['tasks.path']}
                value={pathDraft}
                disabled={disabled}
                placeholder={labels['tasks.pathPlaceholder']}
                onChange={setPathDraft}
                onBlur={commitPath}
                onEnter={commitPath}
              />
            </div>
            <LHoloButton label={labels['button.browse']} disabled={disabled} onClick={() => tasksCtrl.browseLayer(selectedLayer.id)} />
          </div>
        </div>
      ) : (
        <div className="lholo-muted" style={{ marginTop: '0.6rem' }}>{labels['tasks.noSelection']}</div>
      )}

      {/* ========================================================= */}
      {/* 新建、重命名和删除确认共用同一个弹窗 */}
      {/* ========================================================= */}
      {taskModal ? (
        <div className="lholo-modal-backdrop" onClick={handleCancelTaskModal}>
          <div
            className="lholo-modal"
            style={{ width: '30rem', maxWidth: '90vw' }}
            onClick={e => e.stopPropagation?.()}
          >
            <div className="lholo-modal-title">
              {modalTitle}
            </div>
            <div className="lholo-transform-divider" />
            {nameModal ? <div className="lholo-modal-input-group">
              <div className="lholo-modal-input-label">{labels['tasks.name']}</div>
              <LHoloTextField
                className="lholo-modal-input"
                label={labels['tasks.name']}
                value={taskNameDraft}
                maxLength={32}
                autoFocus
                placeholder={labels['tasks.namePlaceholder']}
                onChange={setTaskNameDraft}
                onEnter={handleConfirmTaskModal}
                onEscape={handleCancelTaskModal}
              />
            </div> : <div className="lholo-modal-input-group">
              <div className="lholo-modal-paragraph">
                {(taskModal.mode === 'deleteTask' || taskModal.mode === 'removeFile')
                  ? labels[taskModal.mode === 'deleteTask' ? 'tasks.deleteTaskPrompt' : 'tasks.removeFilePrompt']
                    .replace('%s', () => taskModal.name)
                  : ''}
              </div>
            </div>}
            <div className="lholo-transform-divider" />
            <div className="lholo-action-row" style={{ marginTop: '0.9rem', justifyContent: 'flex-end' }}>
              <LHoloButton
                label={taskModal.mode === 'create' ? labels['tasks.create'] : labels['tasks.confirm']}
                disabled={disabled || (nameModal && !taskNameDraft.trim())}
                onClick={handleConfirmTaskModal}
              />
              <LHoloButton label={labels['tasks.cancel']} onClick={handleCancelTaskModal} />
            </div>
          </div>
        </div>
      ) : null}
    </div>
  );
}
