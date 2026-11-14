// Times the benchmark pages (and any other Candela page) in headless Chrome.
//
//   node measure.mjs <pages dir> <page>[?query] ... [--seconds 6] [--warmup 3]
//                    [--profile out.cpuprofile [--startup]] [--gpu]
//
// <pages dir> is the build folder holding the pages, such as
// build-web/bindings/candela; each <page> is a folder in it, such as
// bench_sprites?n=4000. For each page it prints startup times and the
// frame times once the game runs:
//
//   cpu    time inside each requestAnimationFrame callback: script and
//          engine work on the main thread, with GL calls only queued
//   frame  time between callbacks, which includes waiting for the GPU
//
// Chrome draws with SwiftShader (a CPU rasterizer) unless --gpu is given, so
// frame intervals there say little about a real GPU; cpu times hold.
// puppeteer-core is found through PUPPETEER_CORE (a path to the package) or
// the usual module resolution; CHROME names the browser executable.
// --startup profiles loading the page and compiling the game instead of
// the frames after it.

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const puppeteer = require(process.env.PUPPETEER_CORE || 'puppeteer-core');

const argv = process.argv.slice(2);
const opt = { seconds: 6, warmup: 3, profile: '', startup: false, gpu: false };
const pages = [];
let root = '';
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === '--seconds') opt.seconds = Number(argv[++i]);
  else if (a === '--warmup') opt.warmup = Number(argv[++i]);
  else if (a === '--profile') opt.profile = argv[++i];
  else if (a === '--gpu') opt.gpu = true;
  else if (a === '--startup') opt.startup = true;
  else if (!root) root = a;
  else pages.push(a);
}
if (!root || pages.length === 0) {
  console.error('usage: node measure.mjs <pages dir> <page>[?query] ... [--seconds s] [--warmup s] [--profile file] [--gpu]');
  process.exit(2);
}

const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pack': 'application/octet-stream' };
const server = http.createServer((req, res) => {
  const file = path.join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
    res.end(data);
  });
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

const gl = opt.gpu ? ['--use-angle=default', '--enable-gpu', '--ignore-gpu-blocklist']
                   : ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
const browser = await puppeteer.launch({
  executablePath: process.env.CHROME || '/usr/bin/google-chrome-stable',
  headless: 'new',
  args: ['--mute-audio', '--no-sandbox', '--disable-background-timer-throttling',
         '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows', ...gl],
});

// Times each animation frame callback and notes when the game says it loaded.
const probe = () => {
  const raf = window.requestAnimationFrame.bind(window);
  const w = window.__bench = { cpu: [], at: [], loaded: 0, firstFrame: 0, errors: [] };
  window.requestAnimationFrame = cb => raf(t => {
    const start = performance.now();
    if (!w.firstFrame) w.firstFrame = start;
    try { cb(t); } finally {
      w.cpu.push(performance.now() - start);
      w.at.push(start);
    }
  });
  for (const level of ['log', 'info', 'warn', 'error']) {
    const orig = console[level].bind(console);
    console[level] = (...args) => {
      const text = args.join(' ');
      if (!w.loaded && text.includes('Candela:') && text.includes('systems')) w.loaded = performance.now();
      if (/\[ERROR\]|raised/.test(text)) w.errors.push(text);
      orig(...args);
    };
  }
};

const stats = xs => {
  if (xs.length === 0) return { n: 0, median: 0, p95: 0, mean: 0 };
  const s = [...xs].sort((a, b) => a - b);
  const q = p => s[Math.min(s.length - 1, Math.floor(p * s.length))];
  return { n: s.length, median: q(0.5), p95: q(0.95), mean: s.reduce((a, b) => a + b, 0) / s.length };
};
const ms = x => x.toFixed(2);

// The functions that took the most time in a CPU profile: self time, and
// total time including what they called. The wasm needs its function names
// (link with --profiling-funcs) to say more than wasm-function[N].
function printProfile(profile) {
  const byId = new Map(profile.nodes.map(n => [n.id, n]));
  const parent = new Map();
  for (const n of profile.nodes) for (const c of n.children || []) parent.set(c, n.id);
  const dt = new Map();
  profile.samples.forEach((id, i) => dt.set(id, (dt.get(id) || 0) + (profile.timeDeltas[i] || 0)));
  const self = new Map(), total = new Map();
  let all = 0;
  for (const [id, t] of dt) {
    all += t;
    const name = n => n.callFrame.functionName || `(${n.callFrame.url.split('/').pop() || 'anonymous'})`;
    self.set(name(byId.get(id)), (self.get(name(byId.get(id))) || 0) + t);
    const seen = new Set();
    for (let at = id; at !== undefined; at = parent.get(at)) {
      const f = name(byId.get(at));
      if (!seen.has(f)) { seen.add(f); total.set(f, (total.get(f) || 0) + t); }
    }
  }
  const top = (m, k) => [...m].sort((a, b) => b[1] - a[1]).slice(0, k);
  const pct = t => (100 * t / all).toFixed(1).padStart(5) + '%';
  console.log('  self time:');
  for (const [f, t] of top(self, 25)) console.log(`    ${pct(t)}  ${f.slice(0, 110)}`);
  console.log('  total time:');
  for (const [f, t] of top(total, 40)) console.log(`    ${pct(t)}  ${f.slice(0, 110)}`);
}

for (const p of pages) {
  const page = await browser.newPage();
  await page.setViewport({ width: 960, height: 540 });
  await page.evaluateOnNewDocument(probe);
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  const [dir, query] = p.split('?');
  const url = `http://127.0.0.1:${port}/${dir}/index.html${query ? '?' + query : ''}`;
  const file = pages.length > 1 ? opt.profile.replace(/(\.cpuprofile)?$/, `-${dir}.cpuprofile`) : opt.profile;
  const startProfile = async () => {
    const session = await page.createCDPSession();
    await session.send('Profiler.enable');
    await session.send('Profiler.setSamplingInterval', { interval: 200 });
    await session.send('Profiler.start');
    return session;
  };
  const stopProfile = async session => {
    const { profile } = await session.send('Profiler.stop');
    fs.writeFileSync(file, JSON.stringify(profile));
    printProfile(profile);
  };
  let profiler = opt.profile && opt.startup ? await startProfile() : null;
  await page.goto(url, { waitUntil: 'load' });
  await page.waitForFunction(() => window.__bench.loaded > 0, { timeout: 60000 }).catch(() => {});
  if (profiler) {
    await new Promise(r => setTimeout(r, 500));
    await stopProfile(profiler);
    profiler = null;
  }
  await new Promise(r => setTimeout(r, opt.warmup * 1000));
  const from = await page.evaluate(() => window.__bench.cpu.length);
  if (opt.profile && !opt.startup) profiler = await startProfile();
  await new Promise(r => setTimeout(r, opt.seconds * 1000));
  if (profiler) await stopProfile(profiler);
  const r = await page.evaluate(from => {
    const w = window.__bench;
    const nav = performance.getEntriesByType('navigation')[0];
    const res = name => performance.getEntriesByType('resource').find(e => e.name.includes(name));
    return {
      cpu: w.cpu.slice(from),
      at: w.at.slice(from),
      first: w.cpu[0] || 0,
      loadedFrame: w.cpu[w.at.findIndex(t => t >= w.loaded) - 1] || 0,
      wasm: res('drift.wasm')?.responseEnd || 0,
      pack: res('game.pack')?.responseEnd || 0,
      firstFrame: w.firstFrame,
      loaded: w.loaded,
      domLoaded: nav ? nav.domContentLoadedEventEnd : 0,
      errors: w.errors,
    };
  }, from);
  const cpu = stats(r.cpu);
  const gaps = r.at.slice(1).map((t, i) => t - r.at[i]);
  const frame = stats(gaps);
  console.log(`${p}`);
  console.log(`  startup: wasm fetched ${ms(r.wasm)} ms, pack ${ms(r.pack)} ms, first frame ${ms(r.firstFrame)} ms, ` +
              `game loaded ${ms(r.loaded)} ms (compile frame ${ms(r.loadedFrame)} ms)`);
  console.log(`  cpu   median ${ms(cpu.median)} ms  p95 ${ms(cpu.p95)} ms  mean ${ms(cpu.mean)} ms  over ${cpu.n} frames`);
  console.log(`  frame median ${ms(frame.median)} ms  p95 ${ms(frame.p95)} ms  (${(1000 / (frame.mean || 1)).toFixed(1)} fps)`);
  for (const e of [...new Set([...errors, ...r.errors])].slice(0, 5)) console.log(`  error: ${e}`);
  await page.close();
}
await browser.close();
server.close();
