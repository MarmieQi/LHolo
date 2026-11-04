import { build } from 'esbuild';
import { copyFile, mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

process.chdir(fileURLToPath(new URL('.', import.meta.url)));

// The version adapter must supply the game's instances before this bundle runs.
// These packages provide types at development time, never a second runtime.
const runtimeModules = {
  react: `const React = window.__LHoloOreUiRuntime.React;
    export default React;
    export const {createElement,Fragment,useCallback,useEffect,useMemo,useRef,useState} = React;`,
  '@react-facet/core': `export const {createFacet,createStaticFacet,useFacetState,useFacetUnwrap,
    useFacetMap,useFacetCallback,useFacetEffect,NO_VALUE}
    = window.__LHoloOreUiRuntime.Facet;`,
};

await mkdir('dist', { recursive: true });
const result = await build({
  entryPoints: ['src/index.tsx'],
  outfile: 'dist/app.js',
  bundle: true,
  format: 'iife',
  globalName: 'LHoloOreUi',
  target: 'es2019',
  legalComments: 'none',
  metafile: true,
  plugins: [{
    name: 'game-runtime',
    setup(api) {
      api.onResolve({ filter: /^(react|@react-facet\/core)$/ }, args => ({
        path: args.path, namespace: 'game-runtime',
      }));
      api.onLoad({ filter: /.*/, namespace: 'game-runtime' }, args => ({
        contents: runtimeModules[args.path], loader: 'js',
      }));
    },
  }],
});
const previewOnly = Object.keys(result.metafile.inputs).filter(name => name.replaceAll('\\', '/').startsWith('preview/'));
if (previewOnly.length) throw new Error(`Preview code leaked into game bundle: ${previewOnly.join(', ')}`);
const duplicated = Object.keys(result.metafile.inputs).filter(name =>
  /node_modules\/(react|react-dom|@react-facet)\//.test(name.replaceAll('\\', '/')));
if (duplicated.length) throw new Error(`Unexpected bundled game runtime: ${duplicated.join(', ')}`);
await writeFile('dist/metafile.json', JSON.stringify(result.metafile, null, 2));
await copyFile('src/styles/menu.css', 'dist/menu.css');
