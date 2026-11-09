var WebThemeManager = (function() {
  let themes = ['vivid-dark', 'vivid-light', 'vivid-mixed', 'github-dark', 'github-light', 'sourceforge-dark', 'sourceforge-classic'];
  let current = localStorage.getItem('wf_theme') || 'vivid-dark';
  function apply(themeId) {
    current = themeId;
    localStorage.setItem('wf_theme', themeId);
    document.documentElement.setAttribute('data-theme', themeId);
    let btn = document.getElementById('wf-btn-theme');
    if (btn) {
      let label = btn.querySelector('.wf-btn-label');
      if (label) {
        if (themeId === 'vivid-dark') label.innerText = 'Dark';
        else if (themeId === 'vivid-light') label.innerText = 'Light';
        else if (themeId === 'vivid-mixed') label.innerText = 'Mixed';
        else if (themeId === 'github-dark') label.innerText = 'GH Dark';
        else if (themeId === 'github-light') label.innerText = 'GH Light';
        else if (themeId === 'sourceforge-dark') label.innerText = 'SF Dark';
        else if (themeId === 'sourceforge-classic') label.innerText = 'SF Classic';
        else label.innerText = themeId;
      }
    }
    let indicators = document.querySelectorAll('.wf-theme-indicator');
    indicators.forEach(function(el) { el.innerText = themeId; });
    if (window.WebAudio) WebAudio.play('switch');
  }
  function cycle() {
    let idx = themes.indexOf(current);
    let next = themes[(idx + 1) % themes.length];
    apply(next);
  }
  function init() {
    let saved = localStorage.getItem('wf_theme');
    if (saved) apply(saved);
    else {
      let curAttr = document.documentElement.getAttribute('data-theme');
      if (curAttr) current = curAttr;
      apply(current);
    }
  }
  return {
    get: function() { return current; },
    set: apply,
    toggleTheme: cycle,
    init: init
  };
})();
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', WebThemeManager.init);
} else {
  WebThemeManager.init();
}
