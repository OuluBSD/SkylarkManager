var WebDensityManager = (function() {
  let levels = ['dense', 'compact', 'comfortable', 'spacious'];
  let current = localStorage.getItem('wf_density') || document.documentElement.getAttribute('data-density') || 'comfortable';
  function apply(lvl) {
    current = lvl;
    localStorage.setItem('wf_density', lvl);
    document.documentElement.setAttribute('data-density', lvl);
    let lbl = document.getElementById('wf-density-label');
    if (lbl) lbl.innerText = lvl.charAt(0).toUpperCase() + lvl.slice(1);
    let indicators = document.querySelectorAll('.wf-density-indicator');
    indicators.forEach(function(el) { el.innerText = lvl; });
    if (window.WebAudio) WebAudio.play('switch');
  }
  function cycle() {
    let idx = levels.indexOf(current);
    let next = levels[(idx + 1) % levels.length];
    apply(next);
  }
  function init() {
    let saved = localStorage.getItem('wf_density');
    if (saved) apply(saved);
    else {
      let curAttr = document.documentElement.getAttribute('data-density');
      if (curAttr) current = curAttr;
      let lbl = document.getElementById('wf-density-label');
      if (lbl) lbl.innerText = current.charAt(0).toUpperCase() + current.slice(1);
    }
  }
  return {
    get: function() { return current; },
    set: apply,
    cycleDensity: cycle,
    init: init
  };
})();
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', WebDensityManager.init);
} else {
  WebDensityManager.init();
}
