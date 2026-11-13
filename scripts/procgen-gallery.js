#!/usr/bin/env node
// Makes the pictures of the Candela book's procedural generation guide from
// the programs that draw them, so the pictures never drift from the code.
//
// Each folder in bindings/candela/docs/gallery is a small Drift game. This
// script writes it as a web page with the web runtime of a build, opens the
// page in headless Chrome, and captures the canvas. The page's clock and
// frames are driven from here, one frame of 1/60 s at a time, so a picture
// or an animation comes out the same on every run.
//
//   node scripts/procgen-gallery.js [--build build-web] [--only name,...] [--check]
//
// --build    a web build of Drift with DRIFT_BUILD_CANDELA=ON, holding
//            bindings/candela/web (drift.html, .js, .wasm) and
//            drift_web_pack.js; build them with
//            `cmake --build build-web --target drift_web drift_web_pack`
// --only     make only these pictures
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
const images = path.join(repo, 'bindings/candela/docs/src/images/procgen');

// What to capture from each program: `frames` to run before a still
// picture, or an animation of `count` captures `every` frames apart, played
// at `fps`. `route` walks: each [key, frames] holds a key down for that many
// frames, one after another from the first capture; a null key waits.
const pictures = [
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

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'procgen-gallery-'));
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

function animate(frameDir, fps, colors, out) {
    run('ffmpeg', ['-v', 'error', '-y', '-framerate', String(fps), '-i', path.join(frameDir, '%04d.png'), '-vf',
        `split[a][b];[a]palettegen=max_colors=${colors}:stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle`,
        '-loop', '0', out]);
}

async function capture(browser, base, pic) {
    const src = path.join(gallery, pic.name);
    run(process.execPath, [packer, src, path.join(pages, pic.name), runtime]);
    const toml = fs.readFileSync(path.join(src, 'drift.toml'), 'utf8');
    const width = +/^width\s*=\s*(\d+)/m.exec(toml)[1];
    const height = +/^height\s*=\s*(\d+)/m.exec(toml)[1];

    const page = await browser.newPage();
    const log = [];
    page.on('console', m => log.push(m.text()));
    page.on('pageerror', e => log.push(String(e)));
    await page.setViewport({ width, height, deviceScaleFactor: 1 });
    await page.evaluateOnNewDocument(hookFrames);
    await page.goto(`${base}/${pic.name}/`);
    try {
        await page.waitForFunction(() => window.__drift_waiting() > 0
            || getComputedStyle(document.getElementById('drift-error')).display !== 'none',
            { timeout: 60000, polling: 100 });
    } catch (e) {
        throw new Error(`${pic.name}: the page did not start\n${log.join('\n')}`);
    }
    const failed = await page.evaluate(() => {
        const panel = document.getElementById('drift-error');
        return getComputedStyle(panel).display !== 'none' ? panel.innerText : '';
    });
    if (failed) throw new Error(`${pic.name}: ${failed}`);

    const canvas = await page.$('canvas');
    const keys = [];
    let at = pic.frames || 0;
    for (const [key, n] of pic.route || []) {
        if (key) keys.push({ key, down: at, up: at + n });
        at += n;
    }
    let frame = 0;
    const step = async n => {
        for (let i = 0; i < n; i++) {
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
        const dir = path.join(work, pic.name);
        fs.mkdirSync(dir);
        await step(pic.frames || 1);
        for (let i = 0; i < pic.count; i++) {
            await canvas.screenshot({ path: path.join(dir, String(i).padStart(4, '0') + '.png') });
            await step(pic.every);
        }
        const out = path.join(work, pic.name + '.gif');
        animate(dir, pic.fps, pic.colors || 64, out);
        outputs.push(out);
    } else {
        await step(pic.frames);
        const out = path.join(work, pic.name + '.png');
        quantize(await canvas.screenshot(), pic.colors || 64, out);
        outputs.push(out);
    }
    const errors = log.filter(l => /\[ERROR\]|error/i.test(l) && !/favicon|404/.test(l));
    if (errors.length) throw new Error(`${pic.name}:\n${errors.join('\n')}`);
    await page.close();
    return outputs;
}

(async () => {
    fs.mkdirSync(pages);
    const server = await serve();
    const base = `http://127.0.0.1:${server.address().port}`;
    const browser = await puppeteer.launch({
        executablePath: process.env.CHROME || 'google-chrome-stable',
        headless: 'new',
        args: ['--mute-audio', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    });
    const wanted = only ? only.split(',') : null;
    let differ = 0;
    try {
        for (const pic of pictures) {
            if (wanted && !wanted.includes(pic.name)) continue;
            for (const out of await capture(browser, base, pic)) {
                const dest = path.join(images, path.basename(out));
                const size = fs.statSync(out).size;
                if (check) {
                    const same = fs.existsSync(dest) && Buffer.compare(fs.readFileSync(dest), fs.readFileSync(out)) === 0;
                    if (!same) differ++;
                    console.log(`${same ? 'same' : 'DIFFERS'}  ${path.relative(repo, dest)}`);
                } else {
                    fs.mkdirSync(images, { recursive: true });
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
        console.error(`${differ} picture(s) differ from their programs; run scripts/procgen-gallery.js to write them`);
        process.exit(1);
    }
})().catch(e => {
    console.error(e.message || e);
    process.exit(1);
});
