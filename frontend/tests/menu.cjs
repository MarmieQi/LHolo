// Real React lifecycle, with only the game runtime boundary replaced.
// This does not verify Gameface layout, focus, sound or native scene navigation.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const React = require('react');
const renderer = require('react-test-renderer');
process.chdir(path.resolve(__dirname, '..'));
const listeners = new Set();
const events = [];
const errors = [];
const dropdownListeners = new Set();
const docListenersByType = new Map();
const context = {
  console: { ...console, error: (...args) => errors.push(args) },
  document: {
    querySelector(selector) {
      const rect = selector === '.lholo-menu' ? {width:800,height:600} : {width:1000,height:1000};
      return {getBoundingClientRect: () => rect};
    },
    addEventListener(name, listener) {
      dropdownListeners.add(listener);
      if (!docListenersByType.has(name)) docListenersByType.set(name, new Set());
      docListenersByType.get(name).add(listener);
    },
    removeEventListener(name, listener) {
      dropdownListeners.delete(listener);
      docListenersByType.get(name)?.delete(listener);
    },
  },
  // Gameface compatibility: no host-provided BigInt implementation.
  BigInt: undefined,
  window: {
    __LHoloOreUiRuntime: {
      React, Facet: {}, Shell: ({ component }) => React.createElement(component),
      Interactive: ({ onClick, className, disabled, children }) => React.createElement('button', { onClick, className, disabled }, children),
      Scroll: ({ children }) => React.createElement('native-scroll', null, children),
      Completion: () => null,
      ItemIcon: ({ itemId }) => React.createElement('native-item-icon', { itemId }),
    },
    engine: {
      on(name, fn) { assert(['lholo:menu-entry', 'lholo:menu-labels', 'lholo:projection', 'lholo:materials', 'lholo:capture', 'lholo:transform', 'lholo:render', 'lholo:settings', 'lholo:tasks', 'lholo:preview', 'lholo:write'].includes(name)); listeners.add({ name, fn }); },
      off(name, fn) { for (const item of listeners) if (item.name === name && item.fn === fn) listeners.delete(item); },
      trigger(...args) { events.push(args); },
    },
  },
};
const dispatchDocEvent = (name, event) => renderer.act(() => {
  for (const listener of [...(docListenersByType.get(name) || [])]) listener(event);
});
vm.createContext(context);
vm.runInContext(fs.readFileSync('dist/app.js', 'utf8'), context);
const labels = JSON.parse(fs.readFileSync('../src/i18n/lang/zh_CN.json', 'utf8'));
delete labels._meta;
let tree;
const inputMocks = new Map();
const createNodeMock = element => {
  if (element.type !== 'input') return null;
  const label = element.props['data-narrate'];
  if (!inputMocks.has(label)) inputMocks.set(label, {
    value: element.props.value, listeners: new Map(), focus() {}, blur() {},
    addEventListener(name, listener) { this.listeners.set(name, listener); },
    removeEventListener(name, listener) { if (this.listeners.get(name) === listener) this.listeners.delete(name); },
  });
  return inputMocks.get(label);
};
const mount = () => renderer.act(() => { tree = renderer.create(React.createElement(context.LHoloOreUi.App), {createNodeMock}); });
const receive = (payload, name = 'lholo:menu-labels') => renderer.act(() => { for (const item of listeners) if (item.name === name) item.fn(payload); });
const nodeText = node => typeof node === 'string' ? node : (node.children || []).map(nodeText).join('');
const click = label => renderer.act(() => {
  const button = tree.root.findAllByType('button').find(node => nodeText(node) === label);
  assert(button, `Missing button: ${label}`);
  button.props.onClick();
});
mount();
assert.equal(listeners.size, 11);
assert(events.some(args => args[0] === 'query:subscribe/lholo.menu.ready'));
receive('{"menu.close":false}');
assert(errors.length > 0);
receive(JSON.stringify(labels));
assert.equal(tree.root.findAllByType('native-scroll').length, 2);
assert.equal(tree.root.findAllByType('button').filter(node => node.props.className?.includes('lholo-navigation-item')).length, 9);
let taskState = {tasks:[{id:'castle',name:'城堡建造',selectedFileId:'roof',layers:[
  {id:'body',name:'主体.mcstructure',path:'D:\\投影\\主体.mcstructure',hidden:false,dimension:0},
  {id:'roof',name:'屋顶.litematic',path:'D:\\投影\\屋顶.litematic',hidden:false,dimension:0},
  {id:'detail',name:'装饰.mcstructure',path:'D:\\投影\\装饰.mcstructure',hidden:true,dimension:0}]}],
  currentTaskId:'castle',activeTaskId:'',revision:1,ack:0,accepted:false,loading:false,browsing:false,
  worldAvailable:true,writable:true,error:'',errorFileId:''};
const emitTasks = () => receive(JSON.stringify(taskState),'lholo:tasks');
emitTasks();
// World writing shares native task selection, but owns its request/status channel.
const navigationLabels = tree.root.findAllByType('button')
  .filter(node => node.props.className?.includes('lholo-navigation-item')).map(nodeText);
assert.equal(navigationLabels.indexOf(labels['page.createStructure']), navigationLabels.indexOf(labels['page.render']) + 1);
assert.equal(navigationLabels.indexOf(labels['page.writeWorld']), navigationLabels.indexOf(labels['page.createStructure']) + 1);
click(labels['page.writeWorld']);
const writeAction = () => tree.root.findAllByType('button').find(node => nodeText(node) === labels['page.writeWorld']
  && !node.props.className?.includes('lholo-navigation-item'));
assert(writeAction().props.disabled, 'Writing waits for the native capability snapshot');
const emitWrite = changes => receive(JSON.stringify({ack:0,worldRevision:4,busy:false,localAvailable:true,commandAvailable:true,mode:'local',targetName:'',skipAir:false,skipPlaced:false,command:{active:false,canResume:false,done:0,total:0,state:''},dimension:0,result:'',...changes}), 'lholo:write');
emitWrite({localAvailable:false});
assert(writeAction().props.disabled, 'Remote worlds cannot use local writing');
assert(nodeText(tree.root).includes(labels['writeWorld.noLocalWorld']));
assert(!nodeText(tree.root).includes(labels['writeWorld.loadTask']), 'Only the current blocking condition is displayed');
emitWrite({});
assert(nodeText(tree.root).includes(labels['writeWorld.loadTask']), 'A selected file in an inactive task needs loading, not reselection');
assert(!nodeText(tree.root).includes(labels['writeWorld.noSelection']), 'Do not show the generic combined selection warning');
taskState.activeTaskId = 'castle'; emitTasks(); emitWrite({});
assert(!writeAction().props.disabled, 'Active selected file can be written in a local world');
assert(!nodeText(tree.root).includes(labels['writeWorld.loadTask']));
renderer.act(() => tree.root.findAllByType('button').find(node => node.props.className?.includes('lholo-select-trigger')).props.onClick());
const commandMode = tree.root.findAllByType('button').find(node => nodeText(node) === labels['writeWorld.commandMode']);
assert(!commandMode.props.disabled, 'Command mode can be selected');
renderer.act(() => tree.root.findAllByType('button').find(node => node.props.className?.includes('lholo-select-option') && nodeText(node) === labels['writeWorld.localMode']).props.onClick());
const writeRequests = () => events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.write.intent');
const skipPlacedControl = () => tree.root.findAllByType('button').find(node => nodeText(node) === labels['writeWorld.skipPlaced']);
const skipAirControl = () => tree.root.findAllByType('button').find(node => nodeText(node) === labels['writeWorld.skipAir']);
assert(!skipAirControl().props.className.includes('is-checked'), 'Local air skipping defaults to off');
assert(!skipPlacedControl(), 'Placed-block skipping belongs to command mode only');
renderer.act(() => writeAction().props.onClick());
const writeRequest = JSON.parse(writeRequests().at(-1)[3].request);
assert.deepEqual(writeRequest, {id:1,revision:1,worldRevision:4,taskId:'castle',fileId:'roof',mode:'local',skipAir:false,skipPlaced:false});
assert(writeAction().props.disabled, 'Duplicate writes are blocked while awaiting acknowledgement');
emitWrite({ack:1,busy:true});
assert(writeAction().props.disabled, 'Native execution keeps the action disabled');
assert(skipAirControl().props.disabled, 'The submitted air choice cannot change while writing');
click(labels['page.createStructure']); click(labels['page.writeWorld']);
assert(writeAction().props.disabled, 'Changing pages preserves the in-flight write');
emitWrite({ack:1,result:'writeWorld.complete'});
assert(!writeAction().props.disabled && !nodeText(tree.root).includes(labels['writeWorld.complete']),
  'Late completion from a previous page instance is not displayed');
click(labels['writeWorld.skipAir']);
assert(skipAirControl().props.className.includes('is-checked'));
assert(nodeText(tree.root).includes(labels['writeWorld.descriptionSkipAir'].split('\n').at(-1)), 'Description reflects air skipping');
renderer.act(() => writeAction().props.onClick());
assert.equal(JSON.parse(writeRequests().at(-1)[3].request).skipAir, true, 'Checkbox choice is sent with this write only');
emitWrite({ack:2,result:'writeWorld.complete'});
assert(nodeText(tree.root).includes(labels['writeWorld.complete']), 'The current page displays its own completion');
taskState.tasks[0].selectedFileId = 'body'; emitTasks();
assert(!skipAirControl().props.className.includes('is-checked'), 'Changing files starts with air skipping off');
emitWrite({ack:2,result:'writeWorld.complete'});
assert(!nodeText(tree.root).includes(labels['writeWorld.complete']), 'Changing files clears the old result even when republished');
taskState.tasks[0].selectedFileId = 'roof'; emitTasks();
assert(!nodeText(tree.root).includes(labels['writeWorld.complete']), 'Returning to the old file does not restore its result');
renderer.act(() => writeAction().props.onClick());
emitWrite({ack:3,result:'writeWorld.failed'});
assert(nodeText(tree.root).includes(labels['writeWorld.failed']), 'The current operation can display a failure');
click(labels['writeWorld.skipAir']);
click(labels['page.createStructure']); click(labels['page.writeWorld']);
assert(!skipAirControl().props.className.includes('is-checked'), 'Leaving the page discards the air-skipping choice');
emitWrite({ack:3,result:'writeWorld.failed'});
assert(!nodeText(tree.root).includes(labels['writeWorld.failed']), 'Leaving the page destroys success and failure feedback');
renderer.act(() => writeAction().props.onClick());
emitWrite({ack:4,result:'writeWorld.complete'});
assert(nodeText(tree.root).includes(labels['writeWorld.complete']));
emitWrite({ack:4,worldRevision:5,result:'writeWorld.complete'});
assert(!nodeText(tree.root).includes(labels['writeWorld.complete']), 'A new world cannot inherit old feedback');
emitWrite({ack:4,dimension:1,result:'writeWorld.complete'});
assert(writeAction().props.disabled, 'Other-dimension file is disabled even before task reload publishes');
assert(nodeText(tree.root).includes(labels['writeWorld.otherDimension']), 'Dimension mismatch has a specific hint');
assert(!nodeText(tree.root).includes(labels['writeWorld.complete']), 'Changing dimensions discards the old receipt');
emitWrite({ack:4,dimension:null});
assert(writeAction().props.disabled, 'Missing player dimension cannot enable writing');
emitWrite({ack:4,dimension:0});
assert(!writeAction().props.disabled, 'Returning to the file dimension restores availability');
taskState.tasks[0].layers[1].dimension = null; emitTasks();
assert(writeAction().props.disabled, 'Unbound file cannot be written');
taskState.tasks[0].layers[1].dimension = 0; emitTasks();
assert(!writeAction().props.disabled, 'Bound file in the current dimension becomes available');

taskState.tasks[0].layers[1].hidden = true; emitTasks();
assert(writeAction().props.disabled, 'Hidden file cannot be written');
assert(nodeText(tree.root).includes(labels['writeWorld.showFile']), 'Hidden-file hint explains the specific required action');
taskState.tasks[0].layers[1].hidden = false;
emitTasks();
renderer.act(() => tree.root.findAllByType('button').find(node => node.props.className?.includes('lholo-select-trigger')).props.onClick());
click(labels['writeWorld.commandMode']);
assert(skipAirControl().props.className.includes('is-checked'), 'Command air skipping defaults to on');
assert(!skipPlacedControl().props.className.includes('is-checked'), 'Placed-block skipping defaults to off');
click(labels['writeWorld.skipPlaced']);
emitWrite({ack:4,localAvailable:false,commandAvailable:true});
assert(!writeAction().props.disabled, 'Remote worlds support command writing independently of the local executor');
renderer.act(() => writeAction().props.onClick());
const commandRequest = JSON.parse(writeRequests().at(-1)[3].request);
assert.equal(commandRequest.mode, 'commands');
assert(commandRequest.skipAir && commandRequest.skipPlaced, 'Both options are frozen in the command request');
const commandJob = {active:true,canResume:true,done:2,total:10,state:'writeWorld.commandRetryLimit'};
emitWrite({ack:5,busy:true,mode:'commands',targetName:'屋顶.litematic',skipAir:true,skipPlaced:true,command:commandJob});
assert(nodeText(tree.root).includes('已完成 2 / 10'));
click(labels['writeWorld.commandContinue']);
assert.deepEqual(JSON.parse(writeRequests().at(-1)[3].request), {id:6,worldRevision:4,action:'resume'});
emitWrite({ack:6,busy:true,mode:'commands',targetName:'屋顶.litematic',skipAir:true,skipPlaced:true,command:{...commandJob,canResume:false,state:'writeWorld.commandRunning'}});
click(labels['page.createStructure']); click(labels['page.writeWorld']);
assert(nodeText(tree.root).includes(labels['writeWorld.commandCancel']), 'An active command job remains controllable after page re-entry');
assert(skipAirControl().props.className.includes('is-checked'), 'Active jobs show their frozen air choice after page re-entry');
assert(skipPlacedControl().props.disabled && skipPlacedControl().props.className.includes('is-checked'), 'Active jobs retain the frozen placed-block option');
click(labels['writeWorld.commandCancel']);
assert.deepEqual(JSON.parse(writeRequests().at(-1)[3].request), {id:7,worldRevision:4,action:'cancel'});
emitWrite({ack:7,mode:'commands',targetName:'屋顶.litematic',command:{...commandJob,active:false,canResume:false,state:'writeWorld.commandCancelled'}});
assert(nodeText(tree.root).includes(labels['writeWorld.commandCancelled']));
assert.equal(nodeText(tree.root.findAllByType('button').find(node => node.props.className?.includes('lholo-select-trigger'))).replace('▼',''),
  labels['writeWorld.commandMode'], 'Cancellation after page re-entry preserves the adopted command mode');
assert(skipPlacedControl().props.className.includes('is-checked') && skipAirControl().props.className.includes('is-checked'),
  'Cancelling retains the current page options instead of reverting to defaults');
click(labels['page.createStructure']); click(labels['page.writeWorld']);
renderer.act(() => tree.root.findAllByType('button').find(node => node.props.className?.includes('lholo-select-trigger')).props.onClick());
click(labels['writeWorld.commandMode']);
assert(!skipPlacedControl().props.className.includes('is-checked') && skipAirControl().props.className.includes('is-checked'),
  'New page instances restore the command defaults without persistence');
taskState.activeTaskId = ''; emitTasks();
click(labels['tasks.nav']);
receive(JSON.stringify({ack:0,accepted:false,error:''}), 'lholo:preview');
const previewButtons = () => tree.root.findAllByType('button').filter(node => nodeText(node) === labels['preview.open']);
assert.equal(previewButtons().length, 3, 'Every file has its own preview entry, including hidden files');
const previewEventStart = events.length;
renderer.act(() => previewButtons()[0].props.onClick());
const previewEvent = events.slice(previewEventStart).find(e => e[0] === 'facet:request' && e[1] === 'lholo.preview.intent');
const previewRequest = JSON.parse(previewEvent[3].request);
assert.deepEqual(previewRequest, {id:1,revision:1,taskId:'castle',fileId:'body',frame:[0.8,0.6]});
assert.equal(taskState.tasks[0].selectedFileId,'roof','Preview does not change the build target');
assert(previewButtons().every(node => node.props.disabled), 'Prevent duplicate preview requests');
receive(JSON.stringify({ack:previewRequest.id,accepted:false,error:'preview rejected'}), 'lholo:preview');
assert(nodeText(tree.root).includes('preview rejected'));
assert(previewButtons().every(node => !node.props.disabled), 'Native rejection permits retry');
taskState.worldAvailable = false; emitTasks();
assert(previewButtons().every(node => node.props.disabled), 'Native preview needs an active world');
taskState.worldAvailable = true; emitTasks();
click(labels['tasks.nav']);
assert(nodeText(tree.root).includes(labels['tasks.layers']));
assert(nodeText(tree.root).includes(labels['tasks.currentSelection'] + ':'));
assert.equal(tree.root.findByProps({className:'lholo-selected-file-label'}).children.join(''),
  labels['tasks.currentSelection'] + ':', 'Task label and colon share a single text node');
assert.equal(nodeText(tree.root.findByProps({className:'lholo-selected-file-name'})), '屋顶.litematic',
  'Selected-file heading has no order number or parentheses');
assert(nodeText(tree.root).includes('主体.mcstructure'));
assert(nodeText(tree.root).includes('屋顶.litematic'));
assert(nodeText(tree.root).includes('装饰.mcstructure'));
assert(nodeText(tree.root).includes(labels['tasks.create']));
assert(nodeText(tree.root).includes(labels['tasks.rename']));
assert(nodeText(tree.root).includes(labels['tasks.delete']));
taskState.error = '不支持的文件格式，请选择 .mcstructure 或 .litematic';
taskState.errorFileId = 'roof'; emitTasks();
let taskError = tree.root.findByProps({className:'lholo-muted lholo-task-error'});
assert.deepEqual(taskError.findByProps({className:'lholo-task-error-heading'}).children,
  [labels['tasks.error'] + ': 屋顶.litematic'], 'Error heading keeps punctuation with filename');
assert.deepEqual(taskError.findByProps({className:'lholo-task-error-message'}).children,
  [taskState.error], 'Error reason starts in its own text block');
taskState.errorFileId = ''; emitTasks();
taskError = tree.root.findByProps({className:'lholo-muted lholo-task-error'});
assert.deepEqual(taskError.findByProps({className:'lholo-task-error-heading'}).children,
  [labels['tasks.error'] + ':'], 'Storage error label keeps its punctuation');
assert.deepEqual(taskError.findByProps({className:'lholo-task-error-message'}).children,
  [taskState.error], 'Storage error reason also has its own line');
taskState.error = ''; emitTasks();
click(labels['tasks.hide']);
const taskIntent = JSON.parse(events.filter(e => e[0]==='facet:request' && e[1]==='lholo.tasks.intent').at(-1)[3].request);
assert.equal(taskIntent.action,'visible');assert.equal(taskIntent.fileId,'body');assert.equal(taskIntent.revision,1);
assert(!taskState.tasks[0].layers[0].hidden,'Task UI waits for native state');
taskState.tasks[0].layers[0].hidden=true;taskState.ack=taskIntent.id;taskState.revision++;taskState.accepted=true;emitTasks();
assert.equal(tree.root.findByProps({ 'data-narrate': labels['tasks.path'] }).props.value, 'D:\\投影\\屋顶.litematic',
  'Changing visibility does not select another file');
click(labels['tasks.rename']);
assert(nodeText(tree.root).includes(labels['tasks.renameTitle']));
const beforeImeRequests = events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.tasks.intent').length;
const nameInput = () => tree.root.findByProps({'data-narrate': labels['tasks.name']});
const nameMock = inputMocks.get(labels['tasks.name']);
renderer.act(() => nameMock.listeners.get('compositionstart')());
renderer.act(() => nameInput().props.onKeyDown({key:'Enter',keyCode:13,nativeEvent:{isComposing:true}}));
assert(nodeText(tree.root).includes(labels['tasks.renameTitle']), 'IME candidate Enter keeps task dialog open');
assert.equal(events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.tasks.intent').length, beforeImeRequests);
renderer.act(() => nameMock.listeners.get('compositionend')());
renderer.act(() => nameInput().props.onKeyDown({key:'Enter',keyCode:229,nativeEvent:{}}));
assert(nodeText(tree.root).includes(labels['tasks.renameTitle']), 'IME compatibility key cannot submit');
click(labels['tasks.cancel']);
assert(!nodeText(tree.root).includes(labels['tasks.renameTitle']));
const taskRequests = () => events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.tasks.intent');
const taskActionButton = () => tree.root.findAllByType('button').filter(node =>
  [labels['tasks.activate'], labels['tasks.close']].includes(nodeText(node)));
const acknowledgeTaskAction = changes => {
  taskState.ack = JSON.parse(taskRequests().at(-1)[3].request).id;
  taskState.revision++; Object.assign(taskState, changes); emitTasks();
};
assert.equal(taskActionButton().length, 1, 'Loading and closing share one action button');
assert.equal(nodeText(taskActionButton()[0]), labels['tasks.activate']);
click(labels['tasks.activate']);
assert.equal(JSON.parse(taskRequests().at(-1)[3].request).action, 'activate');
assert(taskActionButton()[0].props.disabled, 'The combined action prevents duplicate requests');
acknowledgeTaskAction({loading:true,activeTaskId:''});
assert.equal(taskActionButton().length, 1);
assert.equal(nodeText(taskActionButton()[0]), labels['tasks.close'], 'First load can be cancelled before an active task is published');
assert(!taskActionButton()[0].props.disabled, 'Native loading does not disable cancellation');
click(labels['tasks.close']);
assert.equal(JSON.parse(taskRequests().at(-1)[3].request).action, 'close');
assert(taskActionButton()[0].props.disabled);
acknowledgeTaskAction({loading:false,activeTaskId:''});
assert.equal(nodeText(taskActionButton()[0]), labels['tasks.activate'], 'Closing restores the load action');
click(labels['tasks.activate']);
acknowledgeTaskAction({loading:false,activeTaskId:taskState.currentTaskId});
assert.equal(nodeText(taskActionButton()[0]), labels['tasks.close'], 'Loaded task displays the close action');
click(labels['tasks.close']);
acknowledgeTaskAction({loading:false,activeTaskId:''});
taskState.activeTaskId = 'another-task'; emitTasks();
assert.equal(nodeText(taskActionButton()[0]), labels['tasks.activate'], 'A different active task does not turn the selected task action into close');
taskState.activeTaskId = ''; taskState.worldAvailable = false; emitTasks();
assert(taskActionButton()[0].props.disabled, 'Loading still requires a world');
taskState.worldAvailable = true; emitTasks();
const pathInput = () => tree.root.findByProps({'data-narrate': labels['tasks.path']});
const editTaskPath = value => renderer.act(() => {
  const input = inputMocks.get(labels['tasks.path']);
  input.value = value; input.listeners.get('input')();
});
const beforeLongPath = taskRequests().length;
editTaskPath('中'.repeat(11000));
renderer.act(() => pathInput().props.onBlur());
assert.equal(taskRequests().length, beforeLongPath, 'oversized UTF-8 value never enters native transport');
assert(nodeText(tree.root).includes(labels['tasks.requestTooLarge']), 'oversized request has localized feedback');
editTaskPath('D:\\' + '中'.repeat(2000) + '.mcstructure');
renderer.act(() => pathInput().props.onBlur());
assert.equal(taskRequests().length, beforeLongPath + 1, 'rejected request does not leave the page waiting');
const longPathIntent = JSON.parse(taskRequests().at(-1)[3].request);
assert(Buffer.byteLength(taskRequests().at(-1)[3].request, 'utf8') > 4096, 'regression reaches beyond old facet limit');
taskState.ack=longPathIntent.id;taskState.revision++;emitTasks();
assert(!nodeText(tree.root).includes(labels['tasks.requestTooLarge']));
const beforeDeleteRequests = taskRequests().length;
click(labels['tasks.delete']);
assert.equal(nodeText(tree.root.findByProps({className:'lholo-modal-title'})), labels['tasks.delete']);
assert(nodeText(tree.root).includes(labels['tasks.deleteTaskPrompt'].replace('%s','城堡建造')));
assert.equal(tree.root.findByProps({className:'lholo-modal'}).findAllByType('input').length, 0);
assert.equal(taskRequests().length, beforeDeleteRequests, 'Opening delete confirmation does not delete the task');
click(labels['tasks.cancel']);
assert.equal(taskRequests().length, beforeDeleteRequests, 'Cancel preserves the task');
click(labels['tasks.removeFile']);
assert(nodeText(tree.root).includes(labels['tasks.removeFilePrompt'].replace('%s','主体.mcstructure')));
renderer.act(() => tree.root.findByProps({className:'lholo-modal-backdrop'}).props.onClick());
assert.equal(taskRequests().length, beforeDeleteRequests, 'Closing the backdrop preserves the file');
click(labels['tasks.removeFile']);
assert.equal(taskRequests().length, beforeDeleteRequests, 'File removal also waits for confirmation');
click(labels['tasks.confirm']);
const removeFileIntent = JSON.parse(taskRequests().at(-1)[3].request);
assert.equal(removeFileIntent.action,'removeFile');assert.equal(removeFileIntent.taskId,'castle');assert.equal(removeFileIntent.fileId,'body');
taskState.tasks[0].layers=taskState.tasks[0].layers.filter(f=>f.id!=='body');
taskState.ack=removeFileIntent.id;taskState.revision++;emitTasks();
assert(!nodeText(tree.root).includes('主体.mcstructure'));
click(labels['tasks.delete']);click(labels['tasks.confirm']);
const deleteTaskIntent = JSON.parse(taskRequests().at(-1)[3].request);
assert.equal(deleteTaskIntent.action,'delete');assert.equal(deleteTaskIntent.taskId,'castle');
assert.equal(taskRequests().length, beforeDeleteRequests+2, 'Only the two confirmed deletions send native requests');
taskState.tasks=[];taskState.currentTaskId='';taskState.ack=deleteTaskIntent.id;taskState.revision++;emitTasks();
assert(nodeText(tree.root).includes(labels['tasks.emptyTasks']));
click(labels['page.transform']);
assert(!nodeText(tree.root).includes('城堡建造'));
click(labels['page.render']);
assert(!nodeText(tree.root).includes('屋顶.litematic'));
click(labels['tasks.nav']);
assert.equal(tree.root.findAllByType('native-scroll').length, 2);
assert(!tree.root.findAllByType('button').some(node => nodeText(node) === labels['page.projection']), 'Legacy projection navigation removed');
const buttonByLabel = label => tree.root.findAllByType('button').find(node => nodeText(node) === label);
const projectionState = (changes = {}) => receive(JSON.stringify({
  worldAvailable:true,loading:false,hasProjection:false,...changes,
}), 'lholo:projection');
projectionState();
assert.equal(buttonByLabel(labels['material.listTitle']).props.disabled, true, 'Materials require an active task file');
const selectedTaskPath = String.raw`D:\蓝图 文件夹\房屋.mcstructure`;
taskState.tasks=[{id:'materials-task',name:'材料任务',selectedFileId:'materials-file',layers:[
  {id:'materials-file',name:'房屋.mcstructure',path:selectedTaskPath,hidden:false,dimension:0}]}];
taskState.currentTaskId=taskState.activeTaskId='materials-task';taskState.revision++;emitTasks();
projectionState({hasProjection:true});
assert.equal(buttonByLabel(labels['material.listTitle']).props.disabled, false);
receive('{"worldAvailable":true,"loading":"invalid","hasProjection":false}', 'lholo:projection');
assert.equal(buttonByLabel(labels['material.listTitle']).props.disabled, false, 'Invalid context cannot replace native state');
const beforeMaterials = events.length;
click(labels['material.listTitle']);
assert.equal(events.at(-1)[0], 'query:subscribe/lholo.materials.request', 'Opening materials modal requests native material snapshot');
assert.equal(events.length, beforeMaterials + 1);
const materialModal = tree.root.findByProps({ className: 'lholo-material-modal' });
assert(nodeText(materialModal).includes('房屋.mcstructure'), 'Task materials identify the selected file');
assert.equal(materialModal.findByProps({className:'lholo-selected-file-label'}).children.join(''),
  labels['tasks.currentSelection'] + ':', 'Materials reuse the compact label with attached colon');

assert(nodeText(materialModal).includes(labels['material.loading']), 'Pending snapshot shows calculation hint');
assert(!nodeText(materialModal).includes(labels['hint.noPlaceableMaterials']));
receive(JSON.stringify({ready: true, items: []}), 'lholo:materials');
assert(nodeText(materialModal).includes(labels['hint.noPlaceableMaterials']), 'Completed empty snapshot shows empty hint');
receive(JSON.stringify({
  ready: true,
  items: [
    { displayName: '石头', typeName: 'minecraft:stone', iconId: 'minecraft:stone', count: 128 },
    { displayName: '橡木木板', typeName: 'minecraft:oak_planks', iconId: 'oak_planks', count: 96 },
    { displayName: '玻璃', typeName: 'minecraft:glass', iconId: 'glass', count: 48 },
    { displayName: '圆石楼梯', typeName: 'minecraft:stone_stairs', iconId: 'stone_stairs', count: 32 },
    { displayName: '橡木原木', typeName: 'minecraft:oak_log', iconId: 'oak_log', count: 24 },
    { displayName: '', nameKey: 'material.water', typeName: 'minecraft:water', iconId: 'minecraft:water', count: 16 },
    { displayName: '灯笼', typeName: 'minecraft:lantern', iconId: 'lantern', count: 8 },
  ],
}), 'lholo:materials');
receive('{"ready":"bad","items":[]}', 'lholo:materials');
receive('{"ready":true,"items":[{"displayName":"bad","typeName":"minecraft:stone","count":-1}]}', 'lholo:materials');
const materialModalText = nodeText(materialModal);
assert(materialModalText.includes(labels['label.materialSummary'].replace('%llu', '352').replace('%zu', '7')));
assert(materialModalText.includes(labels['column.item']));
assert(materialModalText.includes(labels['column.identifier']));
assert(materialModalText.includes(labels['column.total']));
assert(materialModalText.includes('minecraft:stone'));
assert.equal(materialModal.findAllByType('native-item-icon').length, 7,
  'All materials including liquids use native icons');
assert.equal(materialModal.findAllByType('native-item-icon')[0].props.itemId, 'minecraft:stone');
assert(materialModalText.includes(labels['material.water']));
renderer.act(() => materialModal.findAllByType('button').find(node => nodeText(node) === labels['button.close']).props.onClick());
assert.equal(tree.root.findAllByProps({ className: 'lholo-material-modal' }).length, 0, 'Close button dismisses material modal');


click(labels['tasks.nav']);
// Capture drafts live for the whole menu, and only native acknowledgements
// commit player coordinates, world resets and export outcomes.
click(labels['page.createStructure']);
const captureState = (changes = {}) => receive(JSON.stringify({
  draft: {first: null, second: null, includeEntities: false}, revision: 0, ack: 0,
  worldAvailable: true, busy: false, status: labels['captureStatus.needTwoPoints'], error: '', ...changes,
}), 'lholo:capture');
assert.equal(buttonByLabel(labels['button.exportMcstructure']).props.disabled, true);
captureState({worldAvailable: false});
assert.equal(buttonByLabel(labels['button.clearSelection']).props.disabled, true);
captureState();
const playerButtons = () => tree.root.findAllByType('button').filter(n => nodeText(n) === labels['button.usePlayerPosition']);
renderer.act(() => playerButtons()[0].props.onClick());
const captureEvents = () => events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.capture.intent');
const lastCapture = () => JSON.parse(captureEvents().at(-1)[3].request);
assert.equal(lastCapture().action, 'first');
assert.equal(lastCapture().draft.first, null);
const beforeRepeatCapture = events.length;
renderer.act(() => playerButtons()[0].props.onClick());
assert.equal(events.length, beforeRepeatCapture, 'Capture duplicate blocked before acknowledgement');
const firstCapturePoint = {x: -12, y: 63, z: 8};
captureState({revision: 1, ack: 1, draft: {first: firstCapturePoint, second: null, includeEntities: false}});
const inputs = () => tree.root.findAllByType('input');
assert.equal(inputs()[0].props.value, '-12');
const editCoordinate = (index, value) => renderer.act(() => {
  const input = inputMocks.get(inputs()[index].props['data-narrate']);
  input.value = value;
  assert(input.listeners.has('input'), 'Real DOM input handler is attached');
  input.listeners.get('input')();
});
assert.equal(buttonByLabel(labels['oreui.applySelection']), undefined, 'No extra Apply Selection button');
const beforePartial = captureEvents().length;
editCoordinate(3, '2'); editCoordinate(4, '64');
assert.equal(captureEvents().length, beforePartial, 'Incomplete point does not send invalid coordinates');
assert.equal(buttonByLabel(labels['button.exportMcstructure']).props.disabled, true);
editCoordinate(5, '10');
assert.equal(lastCapture().action, 'apply', 'Valid coordinates synchronize automatically');
assert.equal(lastCapture().id, 2);
assert.equal(inputs()[5].props.disabled, false, 'Typing remains enabled during automatic synchronization');
editCoordinate(5, '11'); editCoordinate(5, '12');
renderer.act(() => tree.root.findAllByType('button').find(node => node.props.className?.includes('lholo-checkbox')).props.onClick());
assert.equal(lastCapture().id, 2, 'Edits coalesce behind the in-flight request');
click(labels['page.render']); click(labels['page.createStructure']);
assert.equal(inputs()[3].props.value, '2', 'Page navigation preserves coordinate edits');
const completeDraft = {first: firstCapturePoint, second: {x: 2, y: 64, z: 12}, includeEntities: true};
captureState({revision: 2, ack: 2, draft: {...completeDraft, second: {...completeDraft.second, z: 10}, includeEntities: false}, status: labels['captureStatus.selectionReady']});
assert.equal(lastCapture().id, 3, 'Latest edit is sent after the prior acknowledgement');
assert.equal(lastCapture().draft.includeEntities, true, 'Older acknowledgement cannot overwrite entity edit');
assert.equal(inputs()[5].props.value, '12', 'Older acknowledgement cannot overwrite coordinate text');
assert.equal(lastCapture().draft.second.z, 12, 'Coalesced request carries the newest coordinate');
captureState({revision: 3, ack: 3, draft: completeDraft, status: labels['captureStatus.selectionReady']});
const afterSync = captureEvents().length;
captureState({revision: 3, ack: 3, draft: completeDraft});
assert.equal(captureEvents().length, afterSync, 'Unchanged snapshot does not loop');
assert(JSON.stringify(tree.toJSON()).includes('15 × 2 × 5'), 'Inclusive selection dimensions');
assert(JSON.stringify(tree.toJSON()).includes('150'), 'Selection volume');
click(labels['button.exportMcstructure']);
assert.equal(lastCapture().action, 'export');
assert.equal(lastCapture().revision, 3);
assert.equal(buttonByLabel(labels['button.exportMcstructure']).props.disabled, true);
captureState({revision: 3, ack: 3, draft: completeDraft, busy: true});
assert.equal(buttonByLabel(labels['button.clearSelection']).props.disabled, true);
captureState({revision: 3, ack: 4, draft: completeDraft}); // Cancel retains selection.
assert.equal(inputs()[3].props.value, '2');
assert.equal(buttonByLabel(labels['button.exportMcstructure']).props.disabled, false);
click(labels['button.exportMcstructure']);
captureState({revision: 3, ack: 5, draft: completeDraft, status: labels['captureStatus.exported']});
assert(JSON.stringify(tree.toJSON()).includes(labels['captureStatus.exported']));
receive('{"draft":{"first":{"x":1.5}},"ack":6}', 'lholo:capture');
assert.equal(inputs()[0].props.value, '-12', 'Invalid capture snapshot ignored');
const beforeInvalid = captureEvents().length;
editCoordinate(0, '2147483648');
assert.equal(buttonByLabel(labels['button.exportMcstructure']).props.disabled, true, 'Int32 overflow blocked');
editCoordinate(0, '-');
assert.equal(captureEvents().length, beforeInvalid, 'Invalid text retains last native point');
assert.equal(playerButtons()[0].props.disabled, false, 'Player position can replace invalid text like the old menu');
click(labels['button.clearSelection']);
assert.equal(lastCapture().action, 'clear', 'Invalid local fields can be cleared');
captureState({revision: 4, ack: 6});
assert.equal(inputs()[0].props.value, '');
captureState({revision: 5, ack: 6, worldAvailable: false});
assert.equal(playerButtons()[0].props.disabled, true);
// Reproduce the two player-position buttons independently, then exercise
// the largest int32 spans without relying on the Node host's BigInt runtime.
captureState({revision: 5, ack: 6});
renderer.act(() => playerButtons()[0].props.onClick());
captureState({revision: 6, ack: 7, draft: {first: firstCapturePoint, second: null, includeEntities: false}});
renderer.act(() => playerButtons()[1].props.onClick());
assert.equal(lastCapture().action, 'second');
captureState({revision: 7, ack: 8, draft: {first: firstCapturePoint, second: {x: -12, y: 63, z: 8}, includeEntities: false}});
assert(JSON.stringify(tree.toJSON()).includes('1 × 1 × 1'), 'Second recorded point renders without BigInt');
captureState({revision: 8, ack: 8, draft: {
  first: {x: -2147483648, y: -2147483648, z: -2147483648},
  second: {x: 2147483647, y: 2147483647, z: 2147483647}, includeEntities: false,
}});
assert(JSON.stringify(tree.toJSON()).includes('79228162514264337593543950336'), 'Large volume remains exact without BigInt');
click(labels['tasks.nav']);
assert(events.every(args => ['query:subscribe/lholo.menu.ready', 'query:subscribe/lholo.menu.close',
  'query:subscribe/lholo.materials.request', 'facet:discard', 'facet:request'].includes(args[0])));
// Transform migration is frontend only: exact old controls, no game commands.
const beforeTransform = events.length;
let transformState = {values: {rotation: 0, mirror: 0, x: 0, y: 0, z: 0}, context: 8, ack: 0, accepted: false};
const receiveTransform = () => receive(JSON.stringify(transformState), 'lholo:transform');
const transformRequests = () => events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.transform.intent');
const ackTransform = () => {
  const edit = JSON.parse(transformRequests().at(-1)[3].request);
  assert.equal(edit.expected, transformState.values[edit.field]);
  assert.equal(edit.context, transformState.context);
  transformState = {...transformState, values: {...transformState.values, [edit.field]: edit.value}, ack: edit.id, accepted: true};
  receiveTransform();
};
receiveTransform();
click(labels['page.transform']);
const selectNodes = () => tree.root.findAll(node => typeof node.type === 'string'
  && node.props.className?.split(' ').includes('lholo-select'));
assert.equal(selectNodes().length, 2);
assert.equal(tree.root.findAllByProps({className: 'lholo-stepper'}).length, 3);
const transformInputs = () => tree.root.findAllByType('input');
const selectTrigger = index => selectNodes()[index].findAllByType('button')[0];
renderer.act(() => selectTrigger(0).props.onClick());
assert.equal(dropdownListeners.size, 2, 'Open dropdown listens for outside click and Escape');
assert.deepEqual(tree.root.findByProps({className: 'lholo-select-options'}).findAllByType('button').map(nodeText), ['0°','90°','180°','270°']);
click('90°');
assert.equal(dropdownListeners.size, 0, 'Dropdown subscriptions clean up after choosing');
assert.equal(nodeText(selectTrigger(0)), '90°▼');
renderer.act(() => selectTrigger(1).props.onClick());
assert.deepEqual(tree.root.findByProps({className: 'lholo-select-options'}).findAllByType('button').map(nodeText), [labels['combo.mirrorNone'],'X','Z']);
click('Z');
assert.equal(nodeText(selectTrigger(1)), 'Z▼');
const step = (axisIndex, label) => renderer.act(() => tree.root.findAllByProps({className: 'lholo-stepper'})[axisIndex]
  .findAllByType('button').find(button => nodeText(button) === label).props.onClick());
step(0, '+'); step(1, '-');
assert.equal(transformInputs()[0].props.value, '1');
assert.equal(transformInputs()[1].props.value, '-1');
const typeTransform = (axisIndex, value) => renderer.act(() => {
  const input = inputMocks.get(transformInputs()[axisIndex].props['data-narrate']);
  input.value = value; input.listeners.get('input')();
});
const blurTransform = axisIndex => renderer.act(() => transformInputs()[axisIndex].props.onBlur());
typeTransform(2, '-57'); blurTransform(2);
assert.equal(transformInputs()[2].props.value, '-57');
typeTransform(0, '2147483648'); blurTransform(0); step(0, '+');
assert.equal(transformInputs()[0].props.value, '2147483647', 'Positive int32 boundary clamps without wrapping');
typeTransform(1, '-2147483649'); blurTransform(1); step(1, '-');
assert.equal(transformInputs()[1].props.value, '-2147483648', 'Negative int32 boundary clamps without wrapping');
typeTransform(2, '-'); blurTransform(2);
assert.equal(transformInputs()[2].props.value, '-57', 'Incomplete text restores last valid value on blur');
click(labels['tasks.nav']); click(labels['page.transform']);
assert.equal(nodeText(selectTrigger(0)), '90°▼', 'Transform draft survives page navigation');
assert.equal(nodeText(selectTrigger(1)), 'Z▼');
assert.equal(transformInputs()[2].props.value, '-57');
assert.equal(buttonByLabel(labels['oreui.applySelection']), undefined, 'No added Apply button');
assert(events.length > beforeTransform, 'Transform edits send typed native intents');
// Rapid edits coalesce behind one in-flight field; old ack preserves newer input.
assert.equal(transformRequests().length, 1);
for (let i = 0; i < 5; ++i) ackTransform();
assert.deepEqual(transformState.values, {rotation: 1, mirror: 2, x: 2147483647, y: -2147483648, z: -57});
const countBeforeExternal = transformRequests().length;
transformState = {...transformState, values: {...transformState.values, x: 12}};
receiveTransform();
assert.equal(transformInputs()[0].props.value, '12', 'Native hotkey changes update clean fields');
assert.equal(transformRequests().length, countBeforeExternal, 'Native updates do not echo edits');
step(0, '+'); step(0, '+');
ackTransform();
assert.equal(transformInputs()[0].props.value, '14', 'Ack of first click preserves second click');
ackTransform();
step(2, '+');
transformState = {...transformState, context: 9, values: {rotation: 0, mirror: 0, x: 0, y: 0, z: 0}};
receiveTransform();
assert.equal(transformInputs()[2].props.value, '0', 'New projection discards old local edits');
step(0, '+');
const rejected = JSON.parse(transformRequests().at(-1)[3].request);
transformState = {...transformState, ack: rejected.id, accepted: false, values: {...transformState.values, x: 22}};
receiveTransform();
assert.equal(transformInputs()[0].props.value, '22', 'Conflict reconciles to native state');
// Reverted unsent fields must stop being drafts, so later external changes win.
step(0, '+'); step(1, '+'); step(1, '-'); ackTransform();
const noEcho = transformRequests().length;
transformState = {...transformState, values: {...transformState.values, y: 19}};
receiveTransform();
assert.equal(transformInputs()[1].props.value, '19');
assert.equal(transformRequests().length, noEcho, 'Reverted queued field does not overwrite later native change');
step(0, '+'); step(0, '-');
receiveTransform(); // force/metadata publication before the first edit was applied
ackTransform();
assert.equal(transformInputs()[0].props.value, '23', 'Unacknowledged echo preserves the user revert');
ackTransform();
assert.equal(transformState.values.x, 23, 'Revert is sent after the first edit finishes');
const beforeNoopTransform = transformRequests().length;
typeTransform(0, '23'); blurTransform(0);
transformState = {...transformState, values: {...transformState.values, x: 55}}; receiveTransform();
assert.equal(transformInputs()[0].props.value, '55', 'Unchanged text cannot become a stale draft');
assert.equal(transformRequests().length, beforeNoopTransform);
renderer.act(() => selectTrigger(0).props.onClick());
click(labels['tasks.nav']);
assert.equal(dropdownListeners.size, 0, 'Leaving transform cleans dropdown handlers');

// Render settings migration is frontend only: exact old controls, texts and bounds, no game commands.
const beforeRender = events.length;
let renderState = {values: {bounds: true, axis: 0, mode: 0, layer: 0, fill: 15, outline: 100,
  correctionSeeThrough: false, missingSeeThrough: false}, maximum: [0, 0, 0], context: 10, ack: 0, accepted: false};
const receiveRender = () => receive(JSON.stringify(renderState), 'lholo:render');
const renderRequests = () => events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.render.intent');
const ackRender = () => {
  const edit = JSON.parse(renderRequests().at(-1)[3].request);
  assert.equal(edit.context, renderState.context);
  assert.equal(edit.axis, renderState.values.axis);
  const values = {...renderState.values};
  for (const c of edit.changes) { assert.equal(c.expected, values[c.field]); values[c.field] = c.value; }
  values.layer = Math.min(values.layer, renderState.maximum[values.axis]);
  renderState = {...renderState, values, ack: edit.id, accepted: true};
  receiveRender();
};
receiveRender();
click(labels['page.render']);
assert.equal(selectNodes().length, 2);
assert.equal(tree.root.findAllByProps({className: 'lholo-stepper'}).length, 1);
const pageChecks = () => tree.root.findAllByType('button').filter(node => node.props.className?.includes('lholo-checkbox'));
assert.equal(pageChecks().length, 3);
assert(pageChecks()[0].props.className.includes('is-checked'), 'Structure bounds enabled by default');
assert(!pageChecks()[1].props.className.includes('is-checked'), 'Correction see-through disabled by default');
assert(!pageChecks()[2].props.className.includes('is-checked'), 'Missing see-through disabled by default');
const renderInputs = () => tree.root.findAllByType('input');
assert.equal(renderInputs().length, 3);
assert.equal(renderInputs()[0].props.value, '0', 'Zero-based layer default on Y axis');
assert.equal(renderInputs()[1].props.value, '15', 'Default fill opacity is 15%');
assert.equal(renderInputs()[2].props.value, '100', 'Default outline opacity is 100%');
assert(JSON.stringify(tree.toJSON()).includes('0 - 0（结构 Y 轴起点为 0）'));
assert(JSON.stringify(tree.toJSON()).includes(labels['hint.vibrantVisuals']));
renderer.act(() => selectTrigger(0).props.onClick());
assert.deepEqual(tree.root.findByProps({className: 'lholo-select-options'}).findAllByType('button').map(nodeText),
  [labels['combo.layerAxisY'], labels['combo.layerAxisX'], labels['combo.layerAxisMaterial']]);
click(labels['combo.layerAxisX']);
assert(JSON.stringify(tree.toJSON()).includes('0 - 0（结构 X 轴起点为 0）'));
renderer.act(() => selectTrigger(1).props.onClick());
assert.deepEqual(tree.root.findByProps({className: 'lholo-select-options'}).findAllByType('button').map(nodeText),
  [labels['combo.rangeAll'], labels['combo.rangeSingle'], labels['combo.rangeUpToCurrent'], labels['combo.rangeFromCurrent']]);
click(labels['combo.rangeSingle']);
renderer.act(() => selectTrigger(0).props.onClick());
click(labels['combo.layerAxisMaterial']);
assert.equal(renderInputs()[0].props.value, '1', 'Material axis displays 1-based material number');
assert(JSON.stringify(tree.toJSON()).includes('1 - 1（按材料清单顺序）'));
renderer.act(() => selectTrigger(1).props.onClick());
assert.deepEqual(tree.root.findByProps({className: 'lholo-select-options'}).findAllByType('button').map(nodeText),
  [labels['combo.materialAll'], labels['combo.materialSingle'], labels['combo.materialUpToCurrent'], labels['combo.materialFromCurrent']]);
click(labels['combo.materialUpToCurrent']);
const editRenderInput = (index, value) => renderer.act(() => {
  const input = inputMocks.get(renderInputs()[index].props['data-narrate']);
  input.value = value; input.listeners.get('input')();
});
const blurRenderInput = index => renderer.act(() => renderInputs()[index].props.onBlur());
editRenderInput(1, '150'); blurRenderInput(1);
assert.equal(renderInputs()[1].props.value, '100', 'Fill opacity clamps to 100');
editRenderInput(2, '-5'); blurRenderInput(2);
assert.equal(renderInputs()[2].props.value, '0', 'Outline opacity clamps to 0');
editRenderInput(1, '42'); blurRenderInput(1);
editRenderInput(1, '-'); blurRenderInput(1);
assert.equal(renderInputs()[1].props.value, '42', 'Incomplete opacity text restores last valid value on blur');
renderer.act(() => pageChecks()[0].props.onClick());
renderer.act(() => pageChecks()[1].props.onClick());
renderer.act(() => pageChecks()[2].props.onClick());
click(labels['tasks.nav']); click(labels['page.render']);
assert(!pageChecks()[0].props.className.includes('is-checked'), 'Render draft survives page navigation');
assert(pageChecks()[1].props.className.includes('is-checked'));
assert(pageChecks()[2].props.className.includes('is-checked'));
assert.equal(renderInputs()[1].props.value, '42');
assert.equal(renderInputs()[2].props.value, '0');
click(labels['button.resetCorrectionStyle']);
assert.equal(renderInputs()[1].props.value, '15', 'Reset restores 15% fill opacity');
assert.equal(renderInputs()[2].props.value, '100', 'Reset restores 100% outline opacity');
assert(events.length > beforeRender, 'Render settings use typed native intents');
assert.equal(renderRequests().length, 1, 'Only one render request in flight');
ackRender(); ackRender();
assert.equal(renderState.values.axis, 2);
assert.equal(renderState.values.bounds, false);
assert.equal(renderState.values.correctionSeeThrough, true);
assert.equal(renderState.values.missingSeeThrough, true);
const beforeNativeRender = renderRequests().length;
renderState = {...renderState, maximum: [19, 7, 3], values: {...renderState.values, axis: 0, layer: 12, fill: 37}};
receiveRender();
assert.equal(renderInputs()[0].props.value, '12', 'Native display layer is reflected');
assert.equal(renderInputs()[1].props.value, '37');
assert(JSON.stringify(tree.toJSON()).includes('0 - 19'));
assert.equal(renderRequests().length, beforeNativeRender, 'Native updates do not echo requests');
renderer.act(() => selectTrigger(0).props.onClick()); click(labels['combo.layerAxisX']);
assert.equal(renderInputs()[0].props.value, '7', 'Axis switch clamps to native X limit');
ackRender();
editRenderInput(0, '5'); blurRenderInput(0);
editRenderInput(0, '6'); blurRenderInput(0);
ackRender();
assert.equal(renderInputs()[0].props.value, '6', 'Old ack retains latest layer input');
ackRender();
renderer.act(() => selectTrigger(0).props.onClick()); click(labels['combo.layerAxisMaterial']); ackRender();
assert.equal(renderInputs()[0].props.value, '4', 'Material numbering is one based with real count');
editRenderInput(0, '2'); blurRenderInput(0); ackRender();
assert.equal(renderState.values.layer, 1, 'Native material index stays zero based');
editRenderInput(1, '72'); blurRenderInput(1); ackRender();
editRenderInput(2, '25'); blurRenderInput(2); ackRender();
click(labels['button.resetCorrectionStyle']);
const resetRequest = JSON.parse(renderRequests().at(-1)[3].request);
assert.deepEqual(resetRequest.changes.map(c => c.field), ['fill', 'outline'], 'Style reset is one checked batch');
ackRender();
editRenderInput(1, '42'); blurRenderInput(1);
editRenderInput(1, '15'); blurRenderInput(1); receiveRender();
ackRender();
assert.equal(renderInputs()[1].props.value, '15', 'Render revert survives an unacknowledged snapshot');
ackRender();
const beforeNoopRender = renderRequests().length;
editRenderInput(1, '15'); blurRenderInput(1);
renderState = {...renderState, values: {...renderState.values, fill: 16}}; receiveRender();
assert.equal(renderInputs()[1].props.value, '16', 'Unchanged opacity cannot overwrite a later native update');
assert.equal(renderRequests().length, beforeNoopRender);
editRenderInput(1, '42'); blurRenderInput(1);
const conflicted = JSON.parse(renderRequests().at(-1)[3].request);
renderState = {...renderState, values: {...renderState.values, fill: 64}, ack: conflicted.id, accepted: false}; receiveRender();
assert.equal(renderInputs()[1].props.value, '64', 'Conflict restores native opacity');
editRenderInput(0, '3'); blurRenderInput(0);
renderState = {...renderState, context: 11, values: {...renderState.values, axis: 0, layer: 0}, maximum: [0, 0, 0]}; receiveRender();
assert.equal(renderInputs()[0].props.value, '0', 'New projection discards old layer edit');

// Connected settings use native state; only the OreUI scale is local.
const fixture = JSON.parse(fs.readFileSync('preview/settings-fixture.json', 'utf8'));
const defaultHotkeys = () => fixture.defaults.map((b, slot) => ({...b, slot, capturing:false, display: ''}));
let settingsState = {values: {...fixture.values}, hotkeys: defaultHotkeys(), languages: [
  {code:'en_US',name:'English'},{code:'ru_RU',name:'Русский'},{code:'zh_CN',name:'简体中文'},{code:'zh_TW',name:'繁體中文'}],
  consent:false, context:20, ack:0, accepted:false, hotkeysRevision:0};
const settingsRequests = () => events.filter(e => e[0] === 'facet:request' && e[1] === 'lholo.settings.intent');
const emitSettings = () => {
  const names = {37:'Left',38:'Up',39:'Right',40:'Down',4:labels['key.mouseMiddle'],5:labels['key.mouseSide1'],6:labels['key.mouseSide2']};
  settingsState.hotkeys = settingsState.hotkeys.map(b => ({...b,display:!b.key ? labels['key.notSet'] :
    ((b.modifiers&1)?'Ctrl + ':'')+((b.modifiers&2)?'Alt + ':'')+((b.modifiers&4)?'Shift + ':'')+(names[b.key]||String.fromCharCode(b.key))}));
  receive(JSON.stringify(settingsState), 'lholo:settings');
};
const ackSettings = () => {
  const i = JSON.parse(settingsRequests().at(-1)[3].request);
  assert(i.id > settingsState.ack); assert.equal(i.context,settingsState.context);
  settingsState = {...settingsState,ack:i.id,accepted:true};
  if (i.action === 'set') {
    assert.equal(i.expected,settingsState.values[i.field]);
    settingsState.values = {...settingsState.values,[i.field]:i.value};
  } else if (i.action === 'consent') {
    settingsState.consent=true;settingsState.values.activeMode=i.value;
  } else {
    assert.equal(i.hotkeysRevision,settingsState.hotkeysRevision);
    ++settingsState.hotkeysRevision;
    if (i.action==='capture') settingsState.hotkeys=settingsState.hotkeys.map(b=>({...b,capturing:b.slot===i.slot}));
    if (i.action==='clearHotkey') Object.assign(settingsState.hotkeys[i.slot],{key:0,modifiers:0,capturing:false});
    if (i.action==='resetHotkey') Object.assign(settingsState.hotkeys[i.slot],fixture.defaults[i.slot],{capturing:false});
    if (i.action==='resetHotkeys') {settingsState.hotkeys=defaultHotkeys();settingsState.values.altWheelOffsetEnabled=true;}
  }
  emitSettings();
};
const settleSettings = () => {for(let n=0;n<30 && settingsRequests().length && JSON.parse(settingsRequests().at(-1)[3].request).id>settingsState.ack;++n) ackSettings();};
const nativeCapture = (key,modifiers=0) => {
  const slot=settingsState.hotkeys.findIndex(b=>b.capturing);
  assert(slot>=0);
  if(key===122)return;
  settingsState.hotkeys=settingsState.hotkeys.map((b,n)=>({...b,capturing:false,
    ...(key===27?{}:n===slot?{key:[8,46].includes(key)?0:key,modifiers:[8,46].includes(key)?0:modifiers}
      :b.key===key&&b.modifiers===modifiers?{key:0,modifiers:0}:{})}));
  ++settingsState.hotkeysRevision;emitSettings();
};
emitSettings();
receive(JSON.stringify('hud'), 'lholo:menu-entry');
assert.equal(pageChecks().length, 9, 'native editor return selects the projection HUD tab');
projectionState({worldAvailable: false});
assert.equal(buttonByLabel(labels['hud.layout.edit']).props.disabled, true, 'HUD layout editing requires a world');
projectionState({worldAvailable: true});
click(labels['hud.layout.edit']);
assert(events.some(e=>e[0]==='query:subscribe/lholo.hud.edit-layout'), 'HUD editor entry uses the native command bridge');
assert.equal(pageChecks().length,9);
renderer.act(()=>pageChecks()[0].props.onClick());settleSettings();
assert.equal(settingsState.values.hudEnabled,false);
assert.equal(tree.root.findAllByProps({className: 'lholo-select-trigger'}).length, 0, 'HUD positions use drag editing instead of corner dropdowns');
for(let n=1;n<=8;++n)assert.equal(pageChecks()[n].props.disabled,true);
click(labels['hud.tabMaterials']);
assert.equal(pageChecks().length,2,'Materials HUD settings include stack conversion');
assert.equal(pageChecks()[1].props.disabled,true,'Conversion disabled while materials HUD is off');
const materialItemsInput = () => tree.root.findByProps({'data-narrate': labels['label.materialHudMaxItems']});
assert.equal(materialItemsInput().props.disabled,true);
assert.equal(pageChecks()[0].props.disabled,false,'Materials HUD remains independent of projection HUD');
renderer.act(()=>pageChecks()[0].props.onClick());settleSettings();
assert.equal(settingsState.values.materialHudEnabled,true);
assert.equal(settingsState.values.materialHudStackConversion,false);
assert.equal(pageChecks()[1].props.disabled,false);
assert.equal(materialItemsInput().props.disabled,false);
const editMaterialItems = text => renderer.act(()=>{const input=inputMocks.get(labels['label.materialHudMaxItems']);input.value=text;input.listeners.get('input')();});
editMaterialItems('5');settleSettings();
assert.equal(settingsState.values.materialHudMaxItems,5);
editMaterialItems('');settleSettings();
assert.equal(settingsState.values.materialHudMaxItems,5,'Empty draft does not overwrite native count');
renderer.act(()=>materialItemsInput().props.onBlur());
assert.equal(materialItemsInput().props.value,'5');
editMaterialItems('99');settleSettings();
assert.equal(settingsState.values.materialHudMaxItems,50);
renderer.act(()=>materialItemsInput().props.onBlur());
assert.equal(materialItemsInput().props.value,'50');
click('+');settleSettings();
assert.equal(settingsState.values.materialHudMaxItems,50,'Stepper respects upper bound');
click('-');settleSettings();
assert.equal(settingsState.values.materialHudMaxItems,49);
editMaterialItems('0');settleSettings();
assert.equal(settingsState.values.materialHudMaxItems,1);
renderer.act(()=>materialItemsInput().props.onBlur());
renderer.act(()=>pageChecks()[1].props.onClick());settleSettings();
assert.equal(settingsState.values.materialHudStackConversion,true);
renderer.act(()=>pageChecks()[1].props.onClick());settleSettings();
assert.equal(settingsState.values.materialHudStackConversion,false);
click(labels['hud.tabWrite']);
assert.equal(pageChecks().length,1,'Write HUD has its own independent settings tab');
assert.equal(settingsState.values.writeHudEnabled,true);
renderer.act(()=>pageChecks()[0].props.onClick());settleSettings();
assert.equal(settingsState.values.writeHudEnabled,false);
assert.equal(settingsState.values.materialHudEnabled,true,'Write HUD toggle does not affect other HUD panels');
click(labels['hud.tabProjection']);
assert.equal(pageChecks().length,9);

// Consecutive local HUD changes cannot be lost to the first ack.
renderer.act(()=>pageChecks()[0].props.onClick());renderer.act(()=>pageChecks()[0].props.onClick());
emitSettings();
ackSettings(); assert(!pageChecks()[0].props.className.includes('is-checked'));settleSettings();
settingsState.values={...settingsState.values,hudEnabled:true,hudPosition:2};emitSettings();
assert.equal(tree.root.findAllByProps({className: 'lholo-select-trigger'}).length, 0);
click(labels['page.hotkeys']);
const hotkeyRows=()=>tree.root.findAllByProps({className:'lholo-hotkey-row'});
const hkButton=(n,b=0)=>hotkeyRows()[n].findAllByType('button')[b];
assert.equal(hotkeyRows().length,11);
const hotkeyGroups = () => tree.root.findAllByProps({className:'lholo-hotkey-group'});
assert.deepEqual(hotkeyGroups().map(group=>group.findByProps({className:'lholo-hotkey-group-title'}).children.join('')),
  ['hotkeys.group.menu','hotkeys.group.movement','hotkeys.group.layers','hotkeys.group.materials'].map(key=>labels[key]));
assert.deepEqual(hotkeyGroups().map(group=>group.findAllByProps({className:'lholo-hotkey-row'}).length),[1,6,2,2]);
assert.equal(hotkeyGroups()[1].findAllByProps({className:'lholo-hotkey-gesture'}).length,1,'Alt wheel remains with movement');
assert.equal(nodeText(hotkeyRows()[1].findByProps({className:'lholo-hotkey-label'})),labels['hotkey.moveLeft']);
assert.equal(nodeText(hotkeyRows()[9].findByProps({className:'lholo-hotkey-label'})),labels['hotkey.materialHudPreviousPage']);
assert.equal(nodeText(hkButton(9)),labels['key.notSet']);
assert.equal(nodeText(hkButton(10)),labels['key.notSet']);
renderer.act(()=>hkButton(9).props.onClick());ackSettings();
assert.equal(settingsState.hotkeys[9].capturing,true,'New paging slot uses existing native capture command');
settingsState.hotkeys[9]={...settingsState.hotkeys[9],key:74,modifiers:2,capturing:false,display:'Alt + J'};
++settingsState.hotkeysRevision;emitSettings();
assert.equal(nodeText(hkButton(9)),'Alt + J');
renderer.act(()=>hkButton(9,1).props.onClick());ackSettings();
assert.equal(settingsState.hotkeys[9].key,0,'Reset paging shortcut returns to unbound');
assert.equal(nodeText(hkButton(0)),'Alt + M');
renderer.act(()=>hkButton(7,2).props.onClick());ackSettings();
renderer.act(()=>hkButton(7).props.onClick());ackSettings();
assert.equal(nodeText(hkButton(7)),labels['hint.pressKeys']);
assert.equal(dropdownListeners.size,0,'Game frontend installs no DOM hotkey capture listeners');
nativeCapture(122);assert.equal(nodeText(hkButton(7)),labels['hint.pressKeys']);
nativeCapture(27);assert.equal(nodeText(hkButton(7)),labels['key.notSet']);
renderer.act(()=>hkButton(7).props.onClick());ackSettings();nativeCapture(77,2);
assert.equal(nodeText(hkButton(7)),'Alt + M');assert.equal(nodeText(hkButton(0)),labels['key.notSet']);
renderer.act(()=>hkButton(7).props.onClick());ackSettings();nativeCapture(8);
assert.equal(nodeText(hkButton(7)),labels['key.notSet']);
renderer.act(()=>hkButton(8).props.onClick());ackSettings();nativeCapture(5,1);
assert.equal(nodeText(hkButton(8)),'Ctrl + '+labels['key.mouseSide1']);
renderer.act(()=>hkButton(8,2).props.onClick());ackSettings();
assert.equal(nodeText(hkButton(8)),labels['key.notSet']);
renderer.act(()=>hkButton(0,1).props.onClick());ackSettings();assert.equal(nodeText(hkButton(0)),'Alt + M');
renderer.act(()=>pageChecks()[0].props.onClick());settleSettings();
assert.equal(settingsState.values.altWheelOffsetEnabled,false);
click(labels['button.resetAllHotkeys']);ackSettings();assert(settingsState.values.altWheelOffsetEnabled);
renderer.act(()=>hkButton(1).props.onClick());ackSettings();click(labels['page.interface']);
assert(events.some(e=>e[0]==='query:subscribe/lholo.hotkeys.cancel'),'Leaving hotkey page cancels native capture');
settingsState.hotkeys=settingsState.hotkeys.map(b=>({...b,capturing:false}));++settingsState.hotkeysRevision;emitSettings();
assert.equal(tree.root.findAllByType('input').length,0,'OreUI interface page has no scale placeholder');
assert(!JSON.stringify(tree.toJSON()).includes(labels['label.uiScale']));
// Choices come from the native registry, including future locales.
settingsState.languages.push({code:'test_NEW',name:'Native extra language'});emitSettings();
renderer.act(()=>selectTrigger(0).props.onClick());click('Native extra language');settleSettings();
assert.equal(settingsState.values.language,'test_NEW');
settingsState.values={...settingsState.values,language:'en_US'};emitSettings();
assert.equal(nodeText(selectTrigger(0)),'English▼');
// A changed translation event rerenders the open menu without remounting.
const translated={...labels,'section.interfaceSettings':'Settings live'};receive(JSON.stringify(translated));
assert(JSON.stringify(tree.toJSON()).includes('Settings live'));receive(JSON.stringify(labels));
click(labels['page.experimental']);
assert.equal(pageChecks().length,3);
renderer.act(()=>pageChecks()[0].props.onClick());
const consentBackdrop=tree.root.findByProps({className:'lholo-modal-backdrop'});
let consentContainer=consentBackdrop.parent;
while(consentContainer && !consentContainer.props.className) consentContainer=consentContainer.parent;
assert.equal(consentContainer.props.className,'lholo-backdrop',
  'Consent modal must sit outside the clipped page viewport');
const beforeCancel=settingsRequests().length;click(labels['button.cancel']);
assert.equal(settingsRequests().length,beforeCancel,'Cancelling consent never sends an enable intent');
renderer.act(()=>pageChecks()[1].props.onClick());click(labels['button.consentEnable']);
assert.equal(settingsState.consent,false,'Frontend does not grant native consent optimistically');
ackSettings();assert(settingsState.consent);assert.equal(settingsState.values.activeMode,2);
assert(pageChecks()[1].props.className.includes('is-checked'));
renderer.act(()=>pageChecks()[2].props.onClick());settleSettings();
assert.equal(settingsState.values.activeMode,3);assert(!pageChecks()[1].props.className.includes('is-checked'));
const expInputs=()=>tree.root.findAllByType('input');
const editExp=(n,text)=>renderer.act(()=>{const input=inputMocks.get(expInputs()[n].props['data-narrate']);input.value=text;input.listeners.get('input')();});
const blurExp=n=>renderer.act(()=>expInputs()[n].props.onBlur());
editExp(0,'8');blurExp(0);settleSettings();assert.equal(settingsState.values.placementRadius,4);
editExp(0,'2');blurExp(0);settleSettings();assert.equal(settingsState.values.placementRadius,2);
editExp(1,'99');blurExp(1);settleSettings();assert.equal(settingsState.values.autoPlacementCooldown,60);
editExp(1,'-');blurExp(1);assert.equal(expInputs()[1].props.value,'60');
editExp(1,'20');blurExp(1);const conflict=JSON.parse(settingsRequests().at(-1)[3].request);
settingsState={...settingsState,ack:conflict.id,accepted:false,values:{...settingsState.values,autoPlacementCooldown:17}};emitSettings();
assert.equal(expInputs()[1].props.value,'17','Rejected edit restores authoritative value');
renderer.act(()=>pageChecks()[0].props.onClick());
settingsState={...settingsState,context:21,values:{...settingsState.values,activeMode:0}};emitSettings();
assert(pageChecks().every(b=>!b.props.className.includes('is-checked')),'World context reset clears pending mode');
click(labels['tasks.nav']);

click(labels['menu.close']);
assert(events.some(args => args[0] === 'query:subscribe/lholo.menu.close'));
renderer.act(() => tree.unmount());
assert.equal(listeners.size, 0);
assert([...inputMocks.values()].every(input => input.listeners.size === 0), 'DOM input subscriptions clean up');
mount();
assert.equal(listeners.size, 11);
receive(JSON.stringify(labels));
emitTasks();projectionState({hasProjection:true});
assert.equal(tree.root.findByProps({'data-narrate':labels['tasks.path']}).props.value, selectedTaskPath,
  'Reopened menu restores the native task file snapshot');
renderer.act(() => tree.unmount());
assert.equal(listeners.size, 0);
assert([...inputMocks.values()].every(input => input.listeners.size === 0), 'DOM input subscriptions clean up');

// The version adapter preserves native focus/input and sound ownership.
const theme = { press: 'press', hover: 'hover', focus: 'focus' };
const adapterContext = {
  window: { engine: { trigger() {} } },
  a: { createStaticFacet: value => value },
  e: { createElement: (type, props, ...children) => ({ type, props, children }) },
  Us: Symbol('native-input'), cf: Symbol('native-scroll'), u_: Symbol('native-item-icon'), uc: v => `${v}rem`, no: () => theme,
};
vm.createContext(adapterContext);
vm.runInContext(fs.readFileSync('../tools/oreui/runtime_adapter.js', 'utf8'), adapterContext);
const button = adapterContext.window.__LHoloOreUiRuntime.Interactive({ label: 'LHolo', className: 'lholo-button', children: 'LHolo', onClick() {} });
assert.equal(button.type, adapterContext.Us);
assert.equal(button.props.soundEffectPressed, theme.press);
assert.equal(button.props.soundEffectHovered, theme.hover);
assert.equal(button.props.soundEffectFocused, theme.focus);
const disabled = adapterContext.window.__LHoloOreUiRuntime.Interactive({ label: 'Browse', className: 'lholo-button', disabled: true });
assert.equal(disabled.props.disabled, true);
assert.equal(adapterContext.window.__LHoloOreUiRuntime.TextField, undefined, 'No vanilla visual input component');
assert.equal(adapterContext.window.__LHoloOreUiRuntime.Switch, undefined, 'No vanilla visual switch component');
assert.equal(button.children[0], 'LHolo', 'LHolo authors the primitive children');
const scroll = adapterContext.window.__LHoloOreUiRuntime.Scroll({children: 'content'});
assert.equal(scroll.type, adapterContext.cf);
assert.equal(scroll.props.floating, true);
assert.equal(scroll.props.scrollbarClassName, 'lholo-hidden-scrollbar');
assert.equal(scroll.props.disableScrolling, undefined);
const icon = adapterContext.window.__LHoloOreUiRuntime.ItemIcon({ itemId: 'minecraft:stone' });
assert.equal(icon.type, adapterContext.u_);
assert.equal(icon.props.itemID, 'stone');
assert.equal(icon.props.width, '1.6rem');
assert.equal(icon.props.height, '1.6rem');
assert.equal(adapterContext.window.__LHoloOreUiRuntime.ItemIcon({itemId: 'custom:stone'}).props.itemID, 'custom:stone',
  'Version adapter preserves non-vanilla namespaces');
console.log('Menu tests passed: task navigation and native catalog, deletion confirmations, task material entry, capture/export, transform/render/HUD/hotkeys/language/experimental settings, subscription cleanup and runtime adapters.');
