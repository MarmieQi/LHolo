import type {CaptureState, ProjectionState, MaterialsState, MaterialItem, MenuLabels, TransformState, TransformEdit, RenderState, RenderEdit} from '../src/platform/bridge';
import type {TaskState} from '../src/platform/taskBridge';
import {createPreviewSettings} from './settings';
import {createPreviewTasks} from './tasks';
import type {SettingsIntent} from '../src/platform/settingsBridge';

// Development-only native-boundary simulation. Never included by src/index.tsx.
export function createPreviewEngine(labels: MenuLabels, notice: (message: string) => void) {
  const listeners = new Map<string, Set<(payload: string) => void>>();
  let generation = 0;
  let transform: TransformState = {values:{rotation:0,mirror:0,x:0,y:0,z:0},context:0,ack:0,accepted:false};
  let render: RenderState = {values:{bounds:true,axis:0,mode:0,layer:0,fill:15,outline:100,correctionSeeThrough:false,missingSeeThrough:false},maximum:[0,0,0],context:0,ack:0,accepted:false};
  const previewMaterials: readonly MaterialItem[] = [
    { displayName: '砂岩', typeName: 'minecraft:sandstone', iconId: 'sandstone', count: 141542 },
    { displayName: '白色混凝土', typeName: 'minecraft:white_concrete', iconId: 'white_concrete', count: 131839 },
    { displayName: '橡树叶', typeName: 'minecraft:oak_leaves', iconId: 'oak_leaves', count: 50624 },
    { displayName: '青色玻璃', typeName: 'minecraft:cyan_stained_glass', iconId: 'cyan_stained_glass', count: 19196 },
    { displayName: '草方块', typeName: 'minecraft:grass_block', iconId: 'grass_block', count: 13938 },
    { displayName: '石砖', typeName: 'minecraft:stone_bricks', iconId: 'stone_bricks', count: 12809 },
    { displayName: '青色玻璃板', typeName: 'minecraft:cyan_stained_glass_pane', iconId: 'cyan_stained_glass_pane', count: 10738 },
    { displayName: '', nameKey: 'material.water', typeName: 'minecraft:water', iconId: 'minecraft:water', count: 16 },
  ];

  let materials: MaterialsState = {ready:false,items:[]};
  let projection: ProjectionState = {worldAvailable:true,loading:false,hasProjection:false};
  let capture: CaptureState = {
    draft:{first:null,second:null,includeEntities:false},revision:0,ack:0,
    worldAvailable:true,busy:false,status:labels['captureStatus.needTwoPoints'],error:'',
  };
  const emit = (name: string, value: unknown) => {
    const payload = JSON.stringify(value);
    listeners.get(name)?.forEach(listener => listener(payload));
  };
  const publish = () => {
    tasks.publish();
    emit('lholo:preview',previewStatus);
    emit('lholo:write', {ack:0,worldRevision:0,busy:false,localAvailable:false,commandAvailable:false,mode:"local",targetName:"",skipAir:false,skipPlaced:false,command:{active:false,canResume:false,done:0,total:0,failedSkipped:0,state:""},dimension:null,result:''});
    emit('lholo:materials',materials);
    emit('lholo:capture',capture);
    emit('lholo:transform',transform);
    emit('lholo:render',render);
    settings.publish();
  };
  let previewStatus = {ack:0,accepted:false,error:''};
  const settings = createPreviewSettings(emit,publish);
  const tasks = createPreviewTasks((name,value) => {
    const state=value as TaskState;
    projection={worldAvailable:state.worldAvailable,loading:state.loading,hasProjection:!!state.activeTaskId};
    emit('lholo:projection',projection);
    emit(name,value);
  });
  const later = (callback: () => void) => {
    const current=generation;
    setTimeout(() => {if(generation===current){callback();publish();}},350);
  };
  const engine: typeof window.engine = {
    on(name,callback) {
      if(!listeners.has(name))listeners.set(name,new Set());
      listeners.get(name)!.add(callback);
    },
    off(name,callback) {listeners.get(name)?.delete(callback);},
    trigger(name,...args) {
      if(name==='facet:discard')return;
      if (name === 'facet:request' && args[0] === 'lholo.preview.intent') {
        const request = JSON.parse((args[2] as {request:string}).request);
        previewStatus = {ack:request.id,accepted:false,error:'结构预览使用游戏原生场景，请在游戏内验证。'};
        queueMicrotask(publish); return;
      }
      if (name === 'facet:request' && args[0] === 'lholo.tasks.intent') {
        tasks.request(JSON.parse((args[2] as {request: string}).request)); return;
      }
      if (name === 'facet:request' && args[0] === 'lholo.settings.intent') {
        settings.request(JSON.parse((args[2] as {request: string}).request) as SettingsIntent); return;
      }
      if (name === 'facet:request' && args[0] === 'lholo.render.intent') {
        const edit: RenderEdit = JSON.parse((args[2] as {request: string}).request);
        const layers = edit.changes.some(c => ['axis', 'mode', 'layer'].includes(c.field));
        const accepted = edit.context === render.context && (!layers || edit.axis === render.values.axis)
          && edit.changes.every(c => render.values[c.field] === c.expected);
        const values = {...render.values};
        if (accepted) {
          for (const c of edit.changes) Object.assign(values, {[c.field]: c.value});
          if (layers) values.layer = Math.min(values.layer, render.maximum[values.axis]);
        }
        render = {...render, values, ack: edit.id, accepted};
        queueMicrotask(publish); return;
      }
      if (name === 'facet:request' && args[0] === 'lholo.transform.intent') {
        const edit: TransformEdit = JSON.parse((args[2] as { request: string }).request);
        const accepted = edit.context === transform.context && transform.values[edit.field] === edit.expected;
        transform = { ...transform, ack: edit.id, accepted,
          values: accepted ? { ...transform.values, [edit.field]: edit.value } : transform.values };
        queueMicrotask(publish); return;
      }
      if (name === 'facet:request' && args[0] === 'lholo.capture.intent') {
        const request = JSON.parse((args[2] as { request: string }).request);
        if (capture.busy) return;
        if (!capture.worldAvailable || request.revision !== capture.revision) {
          capture = { ...capture, ack: request.id, error: labels['oreui.captureChanged'] }; publish(); return;
        }
        capture = { ...capture, draft: request.draft, ack: request.id, error: '' };
        if (request.action === 'clear') capture.draft = { first: null, second: null, includeEntities: false };
        else if (request.action === 'first') capture.draft.first = { x: -79, y: -60, z: 43 };
        else if (request.action === 'second') capture.draft.second = { x: -80, y: -60, z: 29 };
        capture = { ...capture, revision: capture.revision + 1,
          status: labels[capture.draft.first && capture.draft.second
            ? 'captureStatus.selectionReady' : 'captureStatus.needTwoPoints'] };
        if (request.action === 'export') {
          capture = { ...capture, busy: true }; publish();
          later(() => { capture = { ...capture, busy: false, status: '开发预览：已模拟导出，未生成结构文件' }; });
        } else publish();
        return;
      }

      switch(name.replace('query:subscribe/','')) {
        case 'lholo.hud.edit-layout': notice('HUD 布局编辑使用游戏原生界面，请在游戏内验证。');break;
        case 'lholo.hotkeys.cancel': settings.cancel();break;
        case 'lholo.menu.ready':
          emit('lholo:menu-entry','tasks');
          queueMicrotask(() => {emit('lholo:menu-labels',labels);publish();});break;
        case 'lholo.menu.close': notice('这是浏览器开发预览。关闭此标签页即可退出。');break;
        case 'lholo.materials.request':
          materials=projection.hasProjection?{ready:true,items:previewMaterials}:{ready:false,items:[]};publish();break;
      }
    },
  };
  return {engine,scenario(name: string) {
    ++generation;settings.contextChanged();
    render={...render,context:render.context+1};
    transform={...transform,context:transform.context+1,values:{rotation:0,mirror:0,x:0,y:0,z:0}};
    projection={...projection,loading:false,worldAvailable:name!=='no-world'};
    capture={...capture,busy:false,worldAvailable:name!=='no-world',revision:capture.revision+1,error:''};
    if(name==='loaded') {
      projection={...projection,hasProjection:true};render={...render,maximum:[11,15,4]};
      materials={ready:true,items:previewMaterials};
    } else if(name==='selection') {
      capture={...capture,status:labels['captureStatus.selectionReady'],draft:{first:{x:-79,y:-60,z:43},second:{x:-80,y:-60,z:29},includeEntities:false}};
    } else if(name==='no-world'||name==='reset') {
      projection={...projection,hasProjection:false};materials={ready:false,items:[]};
      render={...render,maximum:[0,0,0],values:{...render.values,axis:0,mode:0,layer:0}};
      capture={...capture,status:labels[name==='no-world'?'captureStatus.noWorld':'captureStatus.needTwoPoints'],draft:{first:null,second:null,includeEntities:false}};
    }
    tasks.context(projection.worldAvailable,projection.hasProjection);
    publish();notice('开发预览：已切换模拟状态。');
  }};
}
