// Loads a game into the web runtime before main() runs: fetches game.pack
// beside the page (or the URL in Module.gamePack) and writes every file in it
// to the file system at its path in the game folder. web_pack.hpp describes
// the format. Built into drift.js with --pre-js.
//
// The download starts as soon as drift.js runs, beside the download of
// drift.wasm, and reports progress to Module.driftDownload (drift_web.js)
// for the loading screen. A failure goes to Module.driftError when the page
// has one.
//
// Module.driftInstallPack(buffer) writes a pack's files and returns the
// paths whose contents changed; drift-dev.js uses it to bring in a newer
// pack while the game runs.

(function () {
  var url = Module['gamePack'] || 'game.pack';
  var progress = function (done, total) {
    if (Module['driftDownload']) Module['driftDownload']('game.pack', done, total);
  };
  var fail = function (message) {
    var text = 'Could not load the game: ' + message;
    if (Module['driftError']) Module['driftError'](text);
    else if (Module['setStatus']) Module['setStatus'](text);
    console.error(text);
  };

  // The whole pack, reporting progress as it arrives.
  var read = function (response) {
    if (!response.ok) throw new Error(url + ': HTTP ' + response.status);
    var total = +response.headers.get('Content-Length') || 0;
    if (response.headers.get('Content-Encoding')) total = 0;   // the length of the compressed bytes
    if (!response.body || !response.body.getReader) return response.arrayBuffer();
    var reader = response.body.getReader();
    var chunks = [], done = 0;
    var pump = function () {
      return reader.read().then(function (r) {
        if (r.done) return;
        chunks.push(r.value);
        done += r.value.length;
        progress(done, total);
        return pump();
      });
    };
    progress(0, total);
    return pump().then(function () {
      var bytes = new Uint8Array(done), at = 0;
      chunks.forEach(function (c) { bytes.set(c, at); at += c.length; });
      return bytes.buffer;
    });
  };
  var pack = fetch(url).then(read);
  pack.catch(function () {});   // reported once preRun runs

  var install = function (buffer) {
    var bytes = new Uint8Array(buffer);
    var magic = String.fromCharCode.apply(null, bytes.subarray(0, 8));
    var view = new DataView(buffer);
    if (bytes.length < 16 || magic !== 'DRIFTPAK') throw new Error(url + ': not a game pack');
    var version = view.getUint32(8, true);
    if (version !== 1) throw new Error(url + ': pack version ' + version + ', this runtime reads 1');
    var indexLength = view.getUint32(12, true);
    var index = JSON.parse(new TextDecoder().decode(bytes.subarray(16, 16 + indexLength)));
    var base = 16 + indexLength;
    var changed = [];
    index.files.forEach(function (file) {
      var path = '/' + file.path;
      var data = bytes.subarray(base + file.offset, base + file.offset + file.size);
      var old = null;
      try { old = FS.readFile(path); } catch (e) {}
      if (old && old.length === data.length && old.every(function (b, i) { return b === data[i]; })) return;
      var dir = path.substring(0, path.lastIndexOf('/'));
      if (dir) FS.mkdirTree(dir);
      FS.writeFile(path, data);
      changed.push(file.path);
    });
    return changed;
  };
  Module['driftInstallPack'] = install;

  Module['preRun'] = [].concat(Module['preRun'] || []);
  Module['preRun'].push(function () {
    addRunDependency('drift-game-pack');
    pack.then(function (buffer) {
      install(buffer);
      removeRunDependency('drift-game-pack');
    }).catch(function (e) { fail(e.message); });
  });
})();
