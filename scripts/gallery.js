#!/usr/bin/env node
// Makes the pictures in the Candela book's guides from the programs that
// draw them, so the pictures never drift from the code.
//
// Each folder in bindings/candela/docs/gallery/<guide> is a small Drift game
// whose picture goes to bindings/candela/docs/src/images/<guide>. This
// script writes it as a web page with the web runtime of a build, opens the
// page in headless Chrome, and captures the canvas. The page's clock and
// frames, and its keys and mouse, are driven from here, one frame of 1/60 s
// at a time, so a picture or an animation comes out the same on every run.
//
//   node scripts/gallery.js [--build build-web] [--only guide,name,guide/name,...] [--check]
//
// --build    a web build of Drift with DRIFT_BUILD_CANDELA=ON, holding
//            bindings/candela/web (drift.html, .js, .wasm) and
//            drift_web_pack.js; build them with
//            `cmake --build build-web --target drift_web drift_web_pack`
// --only     make only these pictures: a guide's folder name makes all of
//            its pictures, a picture's name or guide/name just that one
// --check    write nothing; fail when a picture differs from the one in the
//            book
//
// Needs Node.js, puppeteer-core (found through NODE_PATH or PUPPETEER_DIR),
// Chrome (CHROME, default google-chrome-stable) and ffmpeg. Chrome runs with
// its sound muted.

'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { spawnSync } = require('child_process');

const repo = path.resolve(__dirname, '..');
const gallery = path.join(repo, 'bindings/candela/docs/gallery');
const images = path.join(repo, 'bindings/candela/docs/src/images');

// What to capture from each program, gallery/<guide>/<name> unless `source`
// names another folder: `frames` to run before a still picture, or an
// animation of `count` captures `every` frames apart, played at `fps` and
// `width` pixels wide (default the canvas's). `route` presses keys: each
// [key, frames] holds a key down for that many frames, one after another
// from the end of `frames`; a null key waits. `mouse(frame)` gives the
// mouse position on the canvas, [x, y], for every frame from the start.
// `colors` caps the palette (default 64).
const procgen = [
    { name: 'seeds', frames: 3 },
    { name: 'noise', frames: 3 },
    { name: 'warp', frames: 3 },
    { name: 'terrain', frames: 3 },
    { name: 'caves', frames: 3 },
    { name: 'dungeon', frames: 3 },
    { name: 'random_walk', frames: 2, count: 60, every: 5, fps: 12 },
    { name: 'wfc', frames: 3 },
    { name: 'poisson', frames: 3 },
    { name: 'loot', frames: 3 },
    { name: 'flow_field', frames: 3 },
    { name: 'level', frames: 2, count: 124, every: 4, fps: 15,
      route: [[null, 20], ['ArrowRight', 75], ['ArrowUp', 9], ['ArrowRight', 61], ['ArrowUp', 9],
              ['ArrowRight', 74], ['ArrowDown', 148], ['ArrowRight', 77]] },
];

// A mouse path for the animation pictures: from point to point every
// `hold` frames, gliding for the first `glide` frames of each.
function hops(points, hold, glide) {
    return f => {
        const i = Math.floor(f / hold);
        const a = points[i % points.length];
        const b = points[(i + 1) % points.length];
        const t = Math.min(1, (f % hold) / glide);
        const s = t * t * (3 - 2 * t);
        return [a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s];
    };
}

// Most animations loop: they run one period before the first capture and
// capture exactly one period.
const animation = [
    { name: 'pivot', frames: 2, count: 40, every: 3, fps: 20 },
    { name: 'rig', frames: 2, count: 40, every: 3, fps: 20 },
    { name: 'look', frames: 240, count: 60, every: 4, fps: 15,
      mouse: hops([[60, 40], [660, 60], [620, 285], [90, 270]], 60, 1) },
    { name: 'springs', frames: 192, count: 48, every: 4, fps: 15 },
    { name: 'spring_tuning', frames: 240, count: 40, every: 3, fps: 20 },
    { name: 'waves', frames: 2, count: 40, every: 3, fps: 20 },
    { name: 'bounce', frames: 2, count: 40, every: 3, fps: 20 },
    { name: 'two_bone', frames: 2, count: 60, every: 4, fps: 15,
      mouse: f => [360 + 100 * Math.sin(2 * Math.PI * f / 240), 175 + 80 * Math.sin(4 * Math.PI * f / 240)] },
    { name: 'tentacle', frames: 240, count: 60, every: 4, fps: 15,
      mouse: f => [360 + 320 * Math.sin(2 * Math.PI * f / 240), 170 + 110 * Math.cos(2 * Math.PI * f / 120)] },
    { name: 'tail', frames: 240, count: 40, every: 3, fps: 20 },
    { name: 'rope', frames: 240, count: 60, every: 4, fps: 15 },
    { name: 'gait', frames: 480, count: 96, every: 5, fps: 12 },
    { name: 'secondary', frames: 240, count: 60, every: 4, fps: 15,
      mouse: hops([[170, 170], [550, 120], [540, 230], [220, 210]], 60, 30) },
    { name: 'tween', frames: 30, count: 68, every: 3, fps: 20,
      route: [[null, 20], ['Space', 9], [null, 84], ['Space', 9], [null, 9], ['KeyX', 9], [null, 64]] },
    { name: 'creature', source: 'bindings/candela/examples/creature', frames: 360, count: 90, every: 4, fps: 15,
      width: 480, mouse: f => [480 + 300 * Math.cos(2 * Math.PI * f / 360), 270 + 170 * Math.sin(2 * Math.PI * f / 360)] },
];

const pictures = [
    ...procgen.map(p => ({ guide: 'procgen', ...p })),
    ...animation.map(p => ({ guide: 'animation', ...p })),
];

function arg(name, fallback) {
    const i = process.argv.indexOf(name);
    return i >= 0 ? process.argv[i + 1] : fallback;
}

const build = path.resolve(arg('--build', path.join(repo, 'build-web')));
const only = arg('--only', '');
const check = process.argv.includes('--check');
const runtime = path.join(build, 'bindings/candela/web');
const packer = path.join(build, 'bindings/candela/drift_web_pack.js');
for (const f of [path.join(runtime, 'drift.wasm'), packer]) {
    if (!fs.existsSync(f)) {
        console.error(`no ${f}: build drift_web and drift_web_pack in ${build} first`);
        process.exit(2);
    }
}

// Chrome by path: CHROME, or google-chrome-stable found on PATH.
function chrome() {
    const name = process.env.CHROME || 'google-chrome-stable';
    if (name.includes(path.sep)) return name;
    for (const dir of (process.env.PATH || '').split(path.delimiter)) {
        const f = path.join(dir, name);
        if (fs.existsSync(f)) return f;
    }
    return name;
}

let puppeteer;
try {
    puppeteer = require(process.env.PUPPETEER_DIR ? path.join(process.env.PUPPETEER_DIR, 'puppeteer-core') : 'puppeteer-core');
} catch (e) {
    console.error('puppeteer-core not found: npm install it, or set PUPPETEER_DIR to the node_modules holding it');
    process.exit(2);
}

function run(cmd, args, input) {
    const r = spawnSync(cmd, args, { input, maxBuffer: 1 << 28 });
    if (r.status !== 0) {
        throw new Error(`${cmd} ${args.join(' ')} failed:\n${r.stderr}`);
    }
    return r.stdout;
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'drift-gallery-'));
const pages = path.join(work, 'pages');

// Serves the pages written into `pages`.
function serve() {
    const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
    const server = http.createServer((req, res) => {
        let file = path.join(pages, decodeURIComponent(req.url.split('?')[0]));
        if (file.endsWith('/')) file += 'index.html';
        fs.readFile(file, (err, data) => {
            if (err) {
                res.writeHead(404);
                res.end();
                return;
            }
            res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream' });
            res.end(data);
        });
    });
    return new Promise(resolve => server.listen(0, '127.0.0.1', () => resolve(server)));
}

// Runs before the page's own scripts: the page's clock stands still and its
// animation frames wait until __drift_step runs them.
function hookFrames() {
    let now = 0;
    let queue = [];
    performance.now = () => now;
    Date.now = () => 1767225600000 + now;
    window.requestAnimationFrame = cb => {
        queue.push(cb);
        return queue.length;
    };
    window.__drift_waiting = () => queue.length;
    window.__drift_step = dt => {
        now += dt;
        const ready = queue;
        queue = [];
        for (const cb of ready) cb(now);
    };
}

// A screenshot as a PNG of at most `colors` colors, without dithering.
function quantize(png, colors, out) {
    run('ffmpeg', ['-v', 'error', '-y', '-f', 'png_pipe', '-i', '-', '-vf',
        `split[a][b];[a]palettegen=max_colors=${colors}:reserve_transparent=0:stats_mode=full[p];[b][p]paletteuse=dither=none`,
        '-frames:v', '1', '-pix_fmt', 'pal8', out], png);
}

function animate(frameDir, fps, colors, width, out) {
    const scale = width ? `scale=${width}:-1:flags=lanczos,` : '';
    run('ffmpeg', ['-v', 'error', '-y', '-framerate', String(fps), '-i', path.join(frameDir, '%04d.png'), '-vf',
        `${scale}split[a][b];[a]palettegen=max_colors=${colors}:stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle`,
        '-loop', '0', out]);
}

async function capture(browser, base, pic) {
    const src = pic.source ? path.join(repo, pic.source) : path.join(gallery, pic.guide, pic.name);
    const id = `${pic.guide}_${pic.name}`;
    run(process.execPath, [packer, src, path.join(pages, id), runtime]);
    const toml = fs.readFileSync(path.join(src, 'drift.toml'), 'utf8');
    const width = +/^width\s*=\s*(\d+)/m.exec(toml)[1];
    const height = +/^height\s*=\s*(\d+)/m.exec(toml)[1];

    const page = await browser.newPage();
    const log = [];
    page.on('console', m => log.push(m.text()));
    page.on('pageerror', e => log.push(String(e)));
    await page.setViewport({ width, height, deviceScaleFactor: 1 });
    await page.evaluateOnNewDocument(hookFrames);
    await page.goto(`${base}/${id}/`);
    try {
        await page.waitForFunction(() => window.__drift_waiting() > 0
            || getComputedStyle(document.getElementById('drift-error')).display !== 'none',
            { timeout: 60000, polling: 100 });
    } catch (e) {
        throw new Error(`${pic.guide}/${pic.name}: the page did not start\n${log.join('\n')}`);
    }
    const failed = await page.evaluate(() => {
        const panel = document.getElementById('drift-error');
        return getComputedStyle(panel).display !== 'none' ? panel.innerText : '';
    });
    if (failed) throw new Error(`${pic.guide}/${pic.name}: ${failed}`);

    const canvas = await page.$('canvas');
    const box = await canvas.boundingBox();
    const keys = [];
    let at = pic.frames || 0;
    for (const [key, n] of pic.route || []) {
        if (key) keys.push({ key, down: at, up: at + n });
        at += n;
    }
    let frame = 0;
    const step = async n => {
        for (let i = 0; i < n; i++) {
            if (pic.mouse) {
                const [x, y] = pic.mouse(frame);
                await page.mouse.move(box.x + x, box.y + y);
            }
            for (const k of keys) {
                if (k.down === frame) await page.keyboard.down(k.key);
                if (k.up === frame) await page.keyboard.up(k.key);
            }
            await page.evaluate(() => window.__drift_step(1000 / 60));
            frame++;
        }
    };
    const outputs = [];
    if (pic.count) {
        const dir = path.join(work, id);
        fs.mkdirSync(dir);
        await step(pic.frames || 1);
        for (let i = 0; i < pic.count; i++) {
            await canvas.screenshot({ path: path.join(dir, String(i).padStart(4, '0') + '.png') });
            await step(pic.every);
        }
        const out = path.join(work, id + '.gif');
        animate(dir, pic.fps, pic.colors || 64, pic.width, out);
        outputs.push(out);
    } else {
        await step(pic.frames);
        const out = path.join(work, id + '.png');
        quantize(await canvas.screenshot(), pic.colors || 64, out);
        outputs.push(out);
    }
    const errors = log.filter(l => /\[ERROR\]|error/i.test(l) && !/favicon|404/.test(l));
    if (errors.length) throw new Error(`${pic.guide}/${pic.name}:\n${errors.join('\n')}`);
    await page.close();
    return outputs;
}

(async () => {
    fs.mkdirSync(pages);
    const server = await serve();
    const base = `http://127.0.0.1:${server.address().port}`;
    const browser = await puppeteer.launch({
        executablePath: chrome(),
        headless: 'new',
        args: ['--mute-audio', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    });
    const wanted = only ? only.split(',') : null;
    let differ = 0;
    try {
        for (const pic of pictures) {
            if (wanted && ![pic.guide, pic.name, `${pic.guide}/${pic.name}`].some(w => wanted.includes(w))) continue;
            for (const out of await capture(browser, base, pic)) {
                const dest = path.join(images, pic.guide, pic.name + path.extname(out));
                const size = fs.statSync(out).size;
                if (check) {
                    const same = fs.existsSync(dest) && Buffer.compare(fs.readFileSync(dest), fs.readFileSync(out)) === 0;
                    if (!same) differ++;
                    console.log(`${same ? 'same' : 'DIFFERS'}  ${path.relative(repo, dest)}`);
                } else {
                    fs.mkdirSync(path.dirname(dest), { recursive: true });
                    fs.copyFileSync(out, dest);
                    console.log(`wrote ${path.relative(repo, dest)} (${Math.round(size / 1024)} KB)`);
                }
            }
        }
    } finally {
        await browser.close();
        server.close();
        fs.rmSync(work, { recursive: true, force: true });
    }
    if (differ) {
        console.error(`${differ} picture(s) differ from their programs; run scripts/gallery.js to write them`);
        process.exit(1);
    }
})().catch(e => {
    console.error(e.message || e);
    process.exit(1);
});
