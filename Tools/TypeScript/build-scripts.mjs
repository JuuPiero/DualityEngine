#!/usr/bin/env node
// Compiles a DualityEngine project's TypeScript scripts into ONE classic-script bundle for
// QuickJS (DualityEngine/Source/Scripting/JS):
//
//   node Tools/TypeScript/build-scripts.mjs <projectDir> [options]
//
//   --out <dir>        output folder (default <project>/Library/ScriptAssemblies)
//   --release          minify (class names kept) -- used for 3DS builds
//   --jsc <exe>        also compile scripts.js to QuickJS bytecode scripts.jsbc with DualityJsc
//   --typecheck        run the TypeScript type checker first; any type error fails the build
//                      and leaves the previous bundle untouched (like Unity's compile errors)
//   --dts-only         only (re)generate types/duality.d.ts
//   --no-project-types don't write <project>/tsconfig.json + Library/Types (test fixtures)
//
// Every *.ts under <project>/Assets (not *.d.ts) is imported by a generated entry; every
// exported class extending MonoBehaviour becomes a script class (name = export name). The
// `duality` module (duality/duality.ts) is bundled in. Output: scripts.js + scripts.js.map
// (sources rewritten to project-relative paths so runtime errors read Assets/...ts:line:col).
//
// Diagnostics are printed as "<file>:<line>:<col>: error: <message>" (one per line); the last
// line is always "DUALITY_TS_RESULT {json}" for the Editor/build scripts to parse.

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const toolDir = path.dirname(fileURLToPath(import.meta.url));
const dualitySource = path.join(toolDir, 'duality', 'duality.ts');
const typesDir = path.join(toolDir, 'types');
const dtsPath = path.join(typesDir, 'duality.d.ts');

const args = process.argv.slice(2);
const flag = (name) => args.includes(name);
const option = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const positional = args.filter((a, i) => !a.startsWith('--') && !(i > 0 && ['--out', '--jsc'].includes(args[i - 1])));

const started = Date.now();
const result = { ok: false, files: 0, bytes: 0, bytecodeBytes: 0, ms: 0, errors: 0, typeErrors: 0, out: '' };

function finish(code) {
  result.ms = Date.now() - started;
  console.log('DUALITY_TS_RESULT ' + JSON.stringify(result));
  process.exit(code);
}
function slash(p) { return p.split(path.sep).join('/'); }

// --- toolchain bootstrap (npm install once) -------------------------------------------------
function ensureToolchain() {
  if (fs.existsSync(path.join(toolDir, 'node_modules', 'esbuild')) && fs.existsSync(path.join(toolDir, 'node_modules', 'typescript')))
    return;
  console.log('[duality-ts] installing the TypeScript toolchain (npm install, first run only)...');
  const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
  const r = spawnSync(npm, ['install', '--no-audit', '--no-fund', '--loglevel=error'], { cwd: toolDir, stdio: 'inherit', shell: process.platform === 'win32' });
  if (r.status !== 0) {
    console.log(`${slash(toolDir)}:1:1: error: npm install failed -- run "npm install" in Tools/TypeScript manually`);
    result.errors++;
    finish(1);
  }
}

async function load(name) {
  return import(pathToFileURL(path.join(toolDir, 'node_modules', name, name === 'typescript' ? 'lib/typescript.js' : 'lib/main.js')).href)
    .then((m) => m.default ?? m);
}

// --- duality.d.ts ----------------------------------------------------------------------------
async function generateDts(force) {
  const upToDate = fs.existsSync(dtsPath) && fs.statSync(dtsPath).mtimeMs >= fs.statSync(dualitySource).mtimeMs;
  if (upToDate && !force) return true;
  const ts = await load('typescript');
  fs.mkdirSync(typesDir, { recursive: true });
  const program = ts.createProgram([dualitySource], {
    declaration: true, emitDeclarationOnly: true, outDir: typesDir, rootDir: path.dirname(dualitySource),
    target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.ESNext, experimentalDecorators: true,
    useDefineForClassFields: false, stripInternal: true, skipLibCheck: true, strict: true, lib: ['lib.es2020.d.ts'],
  });
  const emit = program.emit();
  const diagnostics = ts.getPreEmitDiagnostics(program).concat(emit.diagnostics);
  for (const d of diagnostics) printTsDiagnostic(ts, d, 'error');
  if (diagnostics.length) { result.errors += diagnostics.length; return false; }
  const header = '// GENERATED from Tools/TypeScript/duality/duality.ts by build-scripts.mjs -- do not edit.\n' +
    '// The DualityEngine TypeScript API: import { MonoBehaviour, ... } from "duality".\n';
  fs.writeFileSync(dtsPath, header + fs.readFileSync(dtsPath, 'utf8'));
  return true;
}

function printTsDiagnostic(ts, d, severity) {
  const message = ts.flattenDiagnosticMessageText(d.messageText, '\n').replace(/\n/g, ' ');
  if (d.file && d.start !== undefined) {
    const { line, character } = d.file.getLineAndCharacterOfPosition(d.start);
    const file = projectDir ? slash(path.relative(projectDir, d.file.fileName)) : slash(d.file.fileName);
    console.log(`${file}:${line + 1}:${character + 1}: ${severity}: TS${d.code}: ${message}`);
  } else {
    console.log(`(project):1:1: ${severity}: TS${d.code}: ${message}`);
  }
}

// --- project ---------------------------------------------------------------------------------
let projectDir = positional[0] ? path.resolve(positional[0]) : '';

function findScripts(dir, out) {
  if (!fs.existsSync(dir)) return out;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === 'node_modules' || entry.name.startsWith('.')) continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) findScripts(full, out);
    else if (entry.name.endsWith('.ts') && !entry.name.endsWith('.d.ts')) out.push(full);
  }
  return out;
}

const PROJECT_TSCONFIG = {
  compilerOptions: {
    target: 'ES2020', module: 'ESNext', moduleResolution: 'Bundler', strict: true,
    experimentalDecorators: true, useDefineForClassFields: false, noEmit: true, skipLibCheck: true,
    lib: ['ES2020'], types: [],
    paths: { duality: ['./Library/Types/duality.d.ts'] },
  },
  include: ['Assets/**/*.ts', 'Library/Types/*.d.ts'],
};
const GLOBALS_DTS = `// GENERATED by DualityEngine's build-scripts.mjs -- globals available to scripts.
declare const console: { log(...args: any[]): void; info(...args: any[]): void; debug(...args: any[]): void; warn(...args: any[]): void; error(...args: any[]): void };
`;

function installProjectTypes() {
  const libTypes = path.join(projectDir, 'Library', 'Types');
  fs.mkdirSync(libTypes, { recursive: true });
  fs.copyFileSync(dtsPath, path.join(libTypes, 'duality.d.ts'));
  fs.writeFileSync(path.join(libTypes, 'globals.d.ts'), GLOBALS_DTS);
  const tsconfig = path.join(projectDir, 'tsconfig.json');
  if (!fs.existsSync(tsconfig)) fs.writeFileSync(tsconfig, JSON.stringify(PROJECT_TSCONFIG, null, 2) + '\n');
}

async function typecheck(files) {
  const ts = await load('typescript');
  const options = {
    ...ts.convertCompilerOptionsFromJson(PROJECT_TSCONFIG.compilerOptions, projectDir).options,
    baseUrl: projectDir, noEmit: true,
  };
  const program = ts.createProgram([...files, path.join(projectDir, 'Library', 'Types', 'globals.d.ts')], options);
  const diagnostics = ts.getPreEmitDiagnostics(program);
  for (const d of diagnostics) printTsDiagnostic(ts, d, 'error');
  result.typeErrors = diagnostics.length;
}

async function main() {
  ensureToolchain();
  const dtsOk = await generateDts(flag('--dts-only'));
  if (flag('--dts-only')) { result.ok = dtsOk; finish(dtsOk ? 0 : 1); }
  if (!projectDir || !fs.existsSync(projectDir)) {
    console.log(`(project):1:1: error: project folder not found: ${positional[0] ?? '(none given)'}`);
    result.errors++;
    finish(1);
  }

  const outDir = path.resolve(option('--out') ?? path.join(projectDir, 'Library', 'ScriptAssemblies'));
  const outFile = path.join(outDir, 'scripts.js');
  fs.mkdirSync(outDir, { recursive: true });
  if (!flag('--no-project-types')) installProjectTypes();

  const files = findScripts(path.join(projectDir, 'Assets'), []).sort();
  result.files = files.length;
  const imports = files.map((f, i) => `import * as m${i} from ${JSON.stringify(slash(f))};`).join('\n');
  const list = files.map((f, i) => `[${JSON.stringify(slash(path.relative(projectDir, f)))}, m${i}]`).join(',\n  ');
  const entry = `import { __registerScripts } from 'duality';\n${imports}\n__registerScripts([\n  ${list}\n]);\n`;

  // Type errors stop the build BEFORE anything is written: the previous (working) bundle stays.
  if (flag('--typecheck')) {
    await typecheck(files);
    if (result.typeErrors > 0) finish(1);
  }

  const esbuild = await load('esbuild');
  const release = flag('--release');
  let build;
  try {
    build = await esbuild.build({
      stdin: { contents: entry, resolveDir: projectDir, sourcefile: '__duality_entry__.ts', loader: 'ts' },
      bundle: true, format: 'iife', target: 'es2020', platform: 'neutral', mainFields: ['module', 'main'],
      outfile: outFile, sourcemap: 'external', sourcesContent: false, keepNames: true,
      minify: release, legalComments: 'none', charset: 'utf8', logLevel: 'silent',
      alias: { duality: dualitySource },
      tsconfigRaw: { compilerOptions: { experimentalDecorators: true, useDefineForClassFields: false } },
    });
  } catch (error) {
    for (const e of error.errors ?? [{ text: String(error) }]) {
      const loc = e.location;
      const file = loc ? slash(path.relative(projectDir, path.resolve(projectDir, loc.file))) : '(bundle)';
      console.log(`${file}:${loc?.line ?? 1}:${(loc?.column ?? 0) + 1}: error: ${e.text}`);
      result.errors++;
    }
    finish(1);
  }
  for (const w of build.warnings) {
    const loc = w.location;
    console.log(`${loc ? slash(path.relative(projectDir, path.resolve(projectDir, loc.file))) : '(bundle)'}:${loc?.line ?? 1}:${(loc?.column ?? 0) + 1}: warning: ${w.text}`);
  }

  // Source map: project-relative sources ("Assets/Scripts/Mover.ts"), engine API as "duality.ts".
  const mapPath = outFile + '.map';
  const map = JSON.parse(fs.readFileSync(mapPath, 'utf8'));
  map.sources = map.sources.map((s) => {
    const abs = path.resolve(outDir, s);
    if (path.resolve(abs) === path.resolve(dualitySource)) return 'duality.ts';
    const rel = slash(path.relative(projectDir, abs));
    return rel.startsWith('..') ? slash(path.basename(abs)) : rel;
  });
  delete map.sourcesContent;
  fs.writeFileSync(mapPath, JSON.stringify(map));
  result.bytes = fs.statSync(outFile).size;
  result.out = slash(outFile);

  const jsc = option('--jsc');
  if (jsc) {
    const bc = path.join(outDir, 'scripts.jsbc');
    const r = spawnSync(jsc, [outFile, bc], { encoding: 'utf8' });
    if (r.status !== 0) {
      console.log(`${slash(outFile)}:1:1: error: DualityJsc failed: ${(r.stderr || r.stdout || r.error || '').toString().trim()}`);
      result.errors++;
      finish(1);
    }
    result.bytecodeBytes = fs.statSync(bc).size;
  }

  result.ok = true;
  finish(0);
}

main().catch((error) => {
  console.log(`(toolchain):1:1: error: ${error?.stack ?? error}`);
  result.errors++;
  finish(1);
});
