// What a browser needs from every Drift web build, linked into the page's
// script with --pre-js (CMakeLists.txt, target drift):
//
// - Sound starts on the player's first tap, click or key. Browsers keep a
//   page's audio suspended until then.
// - A tab in the background goes quiet: its audio is suspended until the tab
//   is shown again. The browser already stops sending it frames.
// - Fullscreen and pointer lock asked for outside a tap, click or key (from a
//   game system, a frame later) wait for the next one, since browsers only
//   grant them there.
// - The text the player pastes into the page is kept for the game to read.
// - On touch screens, optional on-screen controls press keys for the game, a
//   notice asks to turn the device when the game wants the other
//   orientation, and a button offers fullscreen.
//
// A page configures the last part by setting `window.DRIFT_PAGE` before this
// runs; `drift build --web` writes it from the [web] section of drift.toml:
//
//     {
//       "sizes": {"drift.wasm": 5728303, "game.pack": 520892},   // bytes, for the loading bar
//       "orientation": "landscape" | "portrait" | "",
//       "fullscreen_button": true,
//       "touch": {
//         "show": "auto" (touch screens only) | "always" | "never",
//         "stick": ["ArrowUp", "ArrowLeft", "ArrowDown", "ArrowRight"],   // or []
//         "buttons": [{"code": "Space", "label": "Jump"}]
//       }
//     }
//
// Key codes are KeyboardEvent.code values, which is what the engine reads.

(function () {
  if (typeof window === 'undefined' || typeof document === 'undefined') return;

  // ---- Loading ----
  //
  // Downloads report here; the sum goes to Module.setStatus as
  // "Loading (<done>/<total>)", which the page's loading screen shows as a
  // bar. A page that knows the real sizes (DRIFT_PAGE.sizes, by file name)
  // gets an exact bar even when the server compresses the files.
  var sizes = (window.DRIFT_PAGE && window.DRIFT_PAGE.sizes) || {};
  var downloads = {};
  Module['driftDownload'] = function (name, done, total) {
    downloads[name] = [done, sizes[name] || total];
    var d = 0, t = 0;
    for (var k in downloads) {
      d += downloads[k][0];
      t += Math.max(downloads[k][0], downloads[k][1]);
    }
    if (Module['setStatus']) Module['setStatus']('Loading (' + d + '/' + t + ')');
  };

  // The engine's .wasm, compiled while it downloads, with progress.
  if (!Module['instantiateWasm'] && typeof findWasmBinary === 'function' &&
      typeof WebAssembly.instantiateStreaming === 'function' && typeof ReadableStream === 'function') {
    Module['instantiateWasm'] = function (imports, receive) {
      var url = findWasmBinary();
      var name = url.substring(url.lastIndexOf('/') + 1);
      fetch(url, {credentials: 'same-origin'}).then(function (response) {
        if (!response.ok) throw new Error(url + ': HTTP ' + response.status);
        var total = response.headers.get('Content-Encoding') ? 0 : +response.headers.get('Content-Length') || 0;
        var reader = response.body.getReader();
        var done = 0;
        Module['driftDownload'](name, 0, total);
        var counted = new ReadableStream({
          pull: function (controller) {
            return reader.read().then(function (r) {
              if (r.done) return controller.close();
              done += r.value.length;
              Module['driftDownload'](name, done, total);
              controller.enqueue(r.value);
            });
          }
        });
        var streamed = new Response(counted, {headers: {'Content-Type': 'application/wasm'}});
        return WebAssembly.instantiateStreaming(streamed, imports);
      }).then(function (result) {
        receive(result.instance, result.module);
      }).catch(function (e) {
        var text = 'Could not load ' + url + ': ' + e;
        if (Module['driftError']) Module['driftError'](text);
        console.error(text);
      });
      return {};
    };
  }

  // ---- The browser ----

  var audioContext = function () {
    var sdl = Module['SDL3'];
    return sdl && sdl.audioContext;
  };
  var gestureSeen = false;

  // Calls waiting for a gesture, run inside the next one.
  var waiting = [];
  var onGesture = function () {
    gestureSeen = true;
    var ctx = audioContext();
    if (ctx && ctx.state === 'suspended' && !document.hidden) ctx.resume();
    var run = waiting;
    waiting = [];
    run.forEach(function (fn) { fn(); });
  };
  ['pointerdown', 'pointerup', 'touchend', 'keydown', 'keyup', 'click'].forEach(function (type) {
    window.addEventListener(type, onGesture, true);
  });

  document.addEventListener('visibilitychange', function () {
    var ctx = audioContext();
    if (!ctx) return;
    if (document.hidden) ctx.suspend();
    else if (gestureSeen) ctx.resume();
  });

  // Runs `fn` now if the page may (the player just interacted), else
  // inside the next gesture. `fn` returns a promise or nothing.
  var withGesture = function (fn) {
    var active = navigator.userActivation ? navigator.userActivation.isActive : gestureSeen;
    if (!active) {
      waiting.push(fn);
      return;
    }
    try {
      var p = fn();
      if (p && p.catch) p.catch(function () { waiting.push(fn); });
    } catch (e) {
      waiting.push(fn);
    }
  };

  var root = document.documentElement;
  var fullscreenElement = function () {
    return document.fullscreenElement || document.webkitFullscreenElement || null;
  };
  Module['driftSetFullscreen'] = function (on) {
    if (!on) {
      waiting = waiting.filter(function (fn) { return fn.kind !== 'fullscreen'; });
      if (fullscreenElement()) (document.exitFullscreen || document.webkitExitFullscreen).call(document);
      return;
    }
    if (fullscreenElement()) return;
    var enter = function () {
      var request = root.requestFullscreen || root.webkitRequestFullscreen;
      if (!request) return;
      var p = request.call(root, {navigationUI: 'hide'});
      var lock = Module['driftOrientation'];
      if (p && p.then && lock && screen.orientation && screen.orientation.lock) {
        p = p.then(function () { return screen.orientation.lock(lock).catch(function () {}); });
      }
      return p;
    };
    enter.kind = 'fullscreen';
    withGesture(enter);
  };
  Module['driftIsFullscreen'] = function () { return fullscreenElement() ? 1 : 0; };

  Module['driftLockPointer'] = function (on) {
    var canvas = Module['canvas'];
    if (!on) {
      waiting = waiting.filter(function (fn) { return fn.kind !== 'pointer'; });
      if (document.pointerLockElement) document.exitPointerLock();
      return;
    }
    if (!canvas || document.pointerLockElement === canvas) return;
    var lock = function () { return canvas.requestPointerLock(); };
    lock.kind = 'pointer';
    withGesture(lock);
  };

  Module['driftPasted'] = '';
  window.addEventListener('paste', function (e) {
    var text = e.clipboardData && e.clipboardData.getData('text');
    if (text) Module['driftPasted'] = text;
  });

  // ---- Touch screens ----

  var page = window.DRIFT_PAGE || {};
  var touch = page.touch || {};
  var coarse = !!(window.matchMedia && window.matchMedia('(pointer: coarse)').matches);
  Module['driftTouchScreen'] = coarse || navigator.maxTouchPoints > 0;
  Module['driftOrientation'] = page.orientation || '';

  var css = [
    '.drift-ui { position: fixed; z-index: 10; touch-action: none; user-select: none;',
    '  -webkit-user-select: none; -webkit-tap-highlight-color: transparent; }',
    '.drift-stick { left: max(24px, env(safe-area-inset-left)); bottom: max(24px, env(safe-area-inset-bottom));',
    '  width: 128px; height: 128px; border-radius: 50%; background: rgba(255,255,255,0.12);',
    '  border: 2px solid rgba(255,255,255,0.3); }',
    '.drift-knob { position: absolute; left: 34px; top: 34px; width: 60px; height: 60px; border-radius: 50%;',
    '  background: rgba(255,255,255,0.45); pointer-events: none; }',
    '.drift-buttons { right: max(24px, env(safe-area-inset-right)); bottom: max(24px, env(safe-area-inset-bottom));',
    '  display: flex; flex-wrap: wrap-reverse; justify-content: flex-end; gap: 16px; max-width: 50vw; }',
    '.drift-button { width: 72px; height: 72px; border-radius: 50%; background: rgba(255,255,255,0.18);',
    '  border: 2px solid rgba(255,255,255,0.35); color: rgba(255,255,255,0.9); font: 600 14px sans-serif;',
    '  display: flex; align-items: center; justify-content: center; text-align: center; touch-action: none; }',
    '.drift-button.down { background: rgba(255,255,255,0.4); }',
    '.drift-fullscreen { top: max(12px, env(safe-area-inset-top)); right: max(12px, env(safe-area-inset-right));',
    '  padding: 8px 12px; border-radius: 6px; border: 0; background: rgba(0,0,0,0.5); color: #fff;',
    '  font: 14px sans-serif; }',
    '.drift-rotate { position: fixed; inset: 0; z-index: 20; display: none; align-items: center;',
    '  justify-content: center; background: #000; color: #ddd; font: 18px sans-serif; text-align: center; }'
  ];
  if (page.orientation === 'landscape') css.push('@media (orientation: portrait) { .drift-rotate { display: flex; } }');
  if (page.orientation === 'portrait') css.push('@media (orientation: landscape) { .drift-rotate { display: flex; } }');

  var held = {};
  var press = function (code, down) {
    if (!code || !!held[code] === down) return;
    held[code] = down;
    window.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', {code: code, bubbles: true, cancelable: true}));
  };

  var element = function (cls, parent) {
    var e = document.createElement('div');
    e.className = cls;
    (parent || document.body).appendChild(e);
    return e;
  };

  // A stick that holds up to two of its four keys: [up, left, down, right].
  var stick = function (codes) {
    var base = element('drift-ui drift-stick');
    var knob = element('drift-knob', base);
    var active = null;
    var move = function (e) {
      var r = base.getBoundingClientRect();
      var radius = r.width / 2;
      var dx = e.clientX - (r.left + radius), dy = e.clientY - (r.top + radius);
      var len = Math.sqrt(dx * dx + dy * dy);
      if (len > radius) { dx *= radius / len; dy *= radius / len; }
      knob.style.transform = 'translate(' + dx + 'px,' + dy + 'px)';
      var t = radius * 0.35;
      press(codes[0], dy < -t);
      press(codes[1], dx < -t);
      press(codes[2], dy > t);
      press(codes[3], dx > t);
    };
    var end = function (e) {
      if (e.pointerId !== active) return;
      active = null;
      knob.style.transform = '';
      codes.forEach(function (c) { press(c, false); });
    };
    base.addEventListener('pointerdown', function (e) {
      if (active !== null) return;
      active = e.pointerId;
      base.setPointerCapture(e.pointerId);
      move(e);
      e.preventDefault();
    });
    base.addEventListener('pointermove', function (e) { if (e.pointerId === active) move(e); });
    base.addEventListener('pointerup', end);
    base.addEventListener('pointercancel', end);
  };

  var buttons = function (list) {
    var row = element('drift-ui drift-buttons');
    list.forEach(function (b) {
      var btn = element('drift-button', row);
      btn.textContent = b.label || b.code;
      var up = function () { btn.classList.remove('down'); press(b.code, false); };
      btn.addEventListener('pointerdown', function (e) {
        btn.setPointerCapture(e.pointerId);
        btn.classList.add('down');
        press(b.code, true);
        e.preventDefault();
      });
      btn.addEventListener('pointerup', up);
      btn.addEventListener('pointercancel', up);
    });
  };

  var build = function () {
    var style = document.createElement('style');
    style.textContent = css.join('\n');
    document.head.appendChild(style);

    var show = touch.show === 'always' || (touch.show !== 'never' && Module['driftTouchScreen']);
    if (show && touch.stick && touch.stick.length === 4) stick(touch.stick);
    if (show && touch.buttons && touch.buttons.length) buttons(touch.buttons);

    if (page.orientation && Module['driftTouchScreen']) {
      element('drift-rotate').textContent = 'Turn your device to ' + page.orientation + ' to play';
    }

    var canFullscreen = root.requestFullscreen || root.webkitRequestFullscreen;
    if (page.fullscreen_button && canFullscreen) {
      var fs = document.createElement('button');
      fs.className = 'drift-ui drift-fullscreen';
      fs.textContent = 'Fullscreen';
      fs.addEventListener('click', function () { Module['driftSetFullscreen'](true); });
      document.body.appendChild(fs);
      var update = function () { fs.style.display = fullscreenElement() ? 'none' : ''; };
      document.addEventListener('fullscreenchange', update);
      document.addEventListener('webkitfullscreenchange', update);
    }
  };
  if (document.body) build();
  else document.addEventListener('DOMContentLoaded', build);
})();
