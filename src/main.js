(function (AP) {
  'use strict';
  const fail = document.getElementById('fail');
  try {
    const engine = new AP.Engine(document.getElementById('c'));
    window.antipode = engine;   // handy for debugging / automated tests
    engine.start();
    console.log('Antipode: WebGL' + engine.glVersion);
  } catch (err) {
    fail.textContent = String(err && err.message || err) + '\n\n' + navigator.userAgent;
    fail.hidden = false;
    console.error(err);
    if (!/[?&]noretro\b/.test(location.search)) { location.replace('retro/play/index.html' + location.search); return; }   // WebGL failed after all: the CPU-only version
    // WebGL is out; see whether WebGPU would have fared any better on this device.
    if (navigator.gpu && navigator.gpu.requestAdapter) {
      const add = function (t) { fail.textContent += '\n' + t; };
      navigator.gpu.requestAdapter().then(function (a) {
        if (!a) return add('WebGPU adapter: none');
        const i = a.info || {};
        add('WebGPU adapter: yes ' + [i.vendor, i.architecture, i.device, i.description].filter(Boolean).join(' / ') +
            (a.isFallbackAdapter ? ' (fallback/software)' : ''));
      }).catch(function (e2) { add('WebGPU adapter error: ' + e2); });
    } else {
      fail.textContent += '\nWebGPU adapter: navigator.gpu missing';
    }
  }
})(window.AP = window.AP || {});
