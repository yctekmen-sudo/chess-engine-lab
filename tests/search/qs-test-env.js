// Test-only bridge: Emscripten's virtual environment does not necessarily
// inherit the Node process environment. Do not install in shipped artifacts.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  for (const key of ['CELAB_S1_QUIESCENCE', 'CELAB_S17C_VERIFY']) {
    if (typeof process !== 'undefined' && process.env[key] !== undefined)
      ENV[key] = process.env[key];
  }
});
