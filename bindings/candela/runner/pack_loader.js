// Loads a game into the web runtime before main() runs: fetches game.pack
// beside the page (or the URL in Module.gamePack) and writes every file in it
// to the file system at its path in the game folder. web_pack.hpp describes
// the format. Built into drift.js with --pre-js.

Module['preRun'] = [].concat(Module['preRun'] || []);
Module['preRun'].push(function () {
  var url = Module['gamePack'] || 'game.pack';
  var fail = function (message) {
    var text = 'Could not load the game: ' + message;
    if (Module['setStatus']) Module['setStatus'](text);
    console.error(text);
  };
  addRunDependency('drift-game-pack');
  fetch(url)
    .then(function (response) {
      if (!response.ok) throw new Error(url + ': HTTP ' + response.status);
      return response.arrayBuffer();
    })
    .then(function (buffer) {
      var bytes = new Uint8Array(buffer);
      var magic = String.fromCharCode.apply(null, bytes.subarray(0, 8));
      var view = new DataView(buffer);
      if (bytes.length < 16 || magic !== 'DRIFTPAK') throw new Error(url + ': not a game pack');
      var version = view.getUint32(8, true);
      if (version !== 1) throw new Error(url + ': pack version ' + version + ', this runtime reads 1');
      var indexLength = view.getUint32(12, true);
      var index = JSON.parse(new TextDecoder().decode(bytes.subarray(16, 16 + indexLength)));
      var base = 16 + indexLength;
      index.files.forEach(function (file) {
        var path = '/' + file.path;
        var dir = path.substring(0, path.lastIndexOf('/'));
        if (dir) FS.mkdirTree(dir);
        FS.writeFile(path, bytes.subarray(base + file.offset, base + file.offset + file.size));
      });
      removeRunDependency('drift-game-pack');
    })
    .catch(function (e) { fail(e.message); });
});
