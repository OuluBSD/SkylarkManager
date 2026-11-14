var WebDensityManager = (function() {
  let levels = ['dense', 'compact', 'comfortable', 'spacious'];
  // The wf_density cookie is host-wide (ports are ignored), like wf_theme.
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
  let current = readCookie('wf_density') || localStorage.getItem('wf_density') || document.documentElement.getAttribute('data-density') || 'comfortable';
  function apply(lvl) {
    current = lvl;
    localStorage.setItem('wf_density', lvl);
    writeCookie('wf_density', lvl);
    document.documentElement.setAttribute('data-density', lvl);
    let lbl = document.getElementById('wf-density-label');
    if (lbl) lbl.innerText = lvl.charAt(0).toUpperCase() + lvl.slice(1);
    let indicators = document.querySelectorAll('.wf-density-indicator');
    indicators.forEach(function(el) { el.innerText = lvl; });
    if (window.WebAudio) WebAudio.play('switch');
  }
  // The shell tells its framed app about a user's change. Only a whitelisted level is sent, to the frame's own origin.
  function broadcast(lvl) {
    let frame = document.querySelector('iframe[data-wf-broadcast]');
    if (!frame || !frame.contentWindow) return;
    let origin = originOf(frame.src);
    if (origin) frame.contentWindow.postMessage({ type: 'wf-density', value: lvl }, origin);
  }
  function choose(lvl) {
    if (levels.indexOf(lvl) < 0) return;
    apply(lvl);
    broadcast(lvl);
  }
  function cycle() {
    let idx = levels.indexOf(current);
    let next = levels[(idx + 1) % levels.length];
    choose(next);
  }
  function init() {
    let saved = readCookie('wf_density') || localStorage.getItem('wf_density');
    if (saved && levels.indexOf(saved) >= 0) apply(saved);
    else {
      let curAttr = document.documentElement.getAttribute('data-density');
      if (curAttr) current = curAttr;
      let lbl = document.getElementById('wf-density-label');
      if (lbl) lbl.innerText = current.charAt(0).toUpperCase() + current.slice(1);
    }
  }
  window.addEventListener('message', function(e) {
    if (!sameHost(e.origin) || !e.data || e.data.type !== 'wf-density') return;
    if (levels.indexOf(e.data.value) < 0) return;
    apply(e.data.value);
  });
  return {
    get: function() { return current; },
    set: choose,
    cycleDensity: cycle,
    init: init
  };
})();
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', WebDensityManager.init);
} else {
  WebDensityManager.init();
}
