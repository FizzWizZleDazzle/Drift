// Hot reload for pages built with `drift build --web --dev`. Release pages do
// not load this file.
//
// The page asks the server about game.pack twice a second (a HEAD request;
// any static server answers it). When the pack's ETag, Last-Modified or
// length changes, the page fetches it, writes the files whose contents
// changed into the game's file system (Module.driftInstallPack in
// pack_loader.js) and names them to the engine (drift_files_changed). The
// engine reloads what was loaded from them: the game's code is compiled
// again and swapped in, keeping the running game, and textures, sounds,
// fonts and scenes are read again.
//
// The engine answers through Module.driftDevStatus: "" when the game runs
// (after its start and after each reload), the compiler's error when the
// changed code does not compile. The error shows over the game, which keeps
// running the code it had, until the next change compiles or the player
// closes it. Module.driftDevReload loads the whole page again when a change
// cannot be made in place; so does a change while the game is not running
// (still loading, or it failed to start).

(function () {
  var url = (window.Module && Module['gamePack']) || 'game.pack';
  var running = false, busy = false, seen = null;
  var panel = null;

  var showError = function (text) {
    if (!panel) {
      panel = document.createElement('div');
      panel.id = 'drift-dev-error';
      panel.style.cssText = 'position:fixed;left:12px;right:12px;bottom:12px;max-height:45vh;display:flex;' +
        'flex-direction:column;gap:8px;padding:12px 14px;box-sizing:border-box;background:rgba(20,20,24,0.94);' +
        'border:1px solid #a33;border-radius:6px;font:14px system-ui,sans-serif;color:#eee;z-index:10;';
      var head = document.createElement('div');
      head.style.cssText = 'display:flex;justify-content:space-between;align-items:center;gap:12px;';
      var title = document.createElement('span');
      title.textContent = 'The changed code does not compile. The game keeps running the code it had.';
      var close = document.createElement('button');
      close.textContent = 'Close';
      close.style.cssText = 'font:inherit;color:#eee;background:#333;border:1px solid #555;border-radius:4px;' +
        'padding:2px 10px;cursor:pointer;';
      close.onclick = function () { panel.style.display = 'none'; Module.canvas.focus(); };
      var pre = document.createElement('pre');
      pre.style.cssText = 'margin:0;overflow:auto;color:#f3b0a8;font:13px ui-monospace,monospace;' +
        'white-space:pre-wrap;user-select:text;';
      head.appendChild(title);
      head.appendChild(close);
      panel.appendChild(head);
      panel.appendChild(pre);
      document.body.appendChild(panel);
    }
    panel.querySelector('pre').textContent = text;
    panel.style.display = 'flex';
  };

  Module['driftDevStatus'] = function (error) {
    running = true;
    if (error) showError(error);
    else if (panel) panel.style.display = 'none';
  };
  Module['driftDevReload'] = function () { location.reload(); };

  // What says a pack changed, from the headers of a HEAD request.
  var signature = function (r) {
    return [r.headers.get('ETag'), r.headers.get('Last-Modified'), r.headers.get('Content-Length')].join(' ');
  };

  var bringIn = function () {
    return fetch(url, {cache: 'no-store'}).then(function (r) {
      if (!r.ok) throw new Error(url + ': HTTP ' + r.status);
      return r.arrayBuffer();
    }).then(function (buffer) {
      var changed = Module['driftInstallPack'](buffer);
      if (!changed.length) return;
      console.log('[drift] changed: ' + changed.join(', '));
      Module['driftChangedFiles'] = changed;
      Module['_drift_files_changed']();
    });
  };

  var check = function () {
    if (busy) return;
    busy = true;
    fetch(url, {method: 'HEAD', cache: 'no-store'}).then(function (r) {
      if (!r.ok) return;
      var now = signature(r);
      if (seen === null || now === seen) {
        seen = now;
        return;
      }
      seen = now;
      if (!running) {
        location.reload();
        return;
      }
      return bringIn();
    }).catch(function (e) {
      console.warn('[drift] hot reload: ' + e.message);
    }).then(function () { busy = false; });
  };
  setInterval(check, 500);
  check();
})();
