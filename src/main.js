(function (AP) {
  'use strict';
  const fail = document.getElementById('fail');
  try {
    const engine = new AP.Engine(document.getElementById('c'));
    window.antipode = engine;   // handy for debugging / automated tests
    engine.start();
  } catch (err) {
    fail.textContent = String(err && err.message || err);
    fail.hidden = false;
    console.error(err);
  }
})(window.AP = window.AP || {});
