var WebThemeManager = (function() {
  let themes = ['vivid-dark', 'vivid-light', 'vivid-mixed', 'github-dark', 'github-light', 'sourceforge-dark', 'sourceforge-classic'];
  // The wf_theme cookie is host-wide (ports are ignored), so every composed app on this host shares the choice.
  function readCookie(name) {
    let m = document.cookie.match(new RegExp('(?:^|; )' + name + '=([^;]*)'));
    return m ? decodeURIComponent(m[1]) : null;
  }
  function writeCookie(name, value) {
    document.cookie = name + '=' + encodeURIComponent(value) + '; path=/; max-age=31536000; SameSite=Lax';
  }
  function originOf(url) {
    try {
      let u = new URL(url, location.href);
      return (u.protocol === 'http:' || u.protocol === 'https:') ? u.origin : '';
    } catch (e) { return ''; }
  }
  function sameHost(origin) {
    try {
      let u = new URL(origin);
      return (u.protocol === 'http:' || u.protocol === 'https:') && u.hostname === location.hostname;
    } catch (e) { return false; }
  }
  let current = readCookie('wf_theme') || localStorage.getItem('wf_theme') || 'vivid-dark';
  function apply(themeId) {
    current = themeId;
    localStorage.setItem('wf_theme', themeId);
    writeCookie('wf_theme', themeId);
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
  // The shell tells its framed app about a user's change. Only a whitelisted id is sent, to the frame's own origin.
  function broadcast(themeId) {
    let frame = document.getElementById('portal-frame');
    if (!frame || !frame.contentWindow) return;
    let origin = originOf(frame.src);
    if (origin) frame.contentWindow.postMessage({ type: 'wf-theme', value: themeId }, origin);
  }
  function choose(themeId) {
    if (themes.indexOf(themeId) < 0) return;
    apply(themeId);
    broadcast(themeId);
  }
  function cycle() {
    let idx = themes.indexOf(current);
    let next = themes[(idx + 1) % themes.length];
    choose(next);
  }
  function init() {
    let saved = readCookie('wf_theme') || localStorage.getItem('wf_theme');
    if (saved && themes.indexOf(saved) >= 0) apply(saved);
    else {
      let curAttr = document.documentElement.getAttribute('data-theme');
      if (curAttr) current = curAttr;
      apply(current);
    }
  }
  window.addEventListener('message', function(e) {
    if (!sameHost(e.origin) || !e.data || e.data.type !== 'wf-theme') return;
    if (themes.indexOf(e.data.value) < 0) return;
    apply(e.data.value);
  });
  return {
    get: function() { return current; },
    set: choose,
    toggleTheme: cycle,
    init: init
  };
})();
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', WebThemeManager.init);
} else {
  WebThemeManager.init();
}
