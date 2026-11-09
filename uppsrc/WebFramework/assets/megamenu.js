var MegaMenu = (function() {
  let activeCat = '';
  let closeTimer = null;
  let dropdownEl = null;
  function init() {
    dropdownEl = document.getElementById('wf-mega-dropdown');
    if (!dropdownEl) return;
    document.querySelectorAll('.wf-cat-tab').forEach(function(tab) {
      tab.addEventListener('mouseenter', function() {
        clearTimeout(closeTimer);
        let catId = tab.getAttribute('data-cat-id');
        openCategory(catId);
      });
    });
    let navBar = document.querySelector('.wf-nav-bar');
    if (navBar) {
      navBar.addEventListener('mouseleave', scheduleClose);
    }
    dropdownEl.addEventListener('mouseenter', function() { clearTimeout(closeTimer); });
    dropdownEl.addEventListener('mouseleave', scheduleClose);
  }
  function openCategory(catId) {
    if (!dropdownEl) return;
    activeCat = catId;
    document.querySelectorAll('.wf-cat-tab').forEach(t => {
      let isMatch = t.getAttribute('data-cat-id') === catId;
      t.classList.toggle('menu-open', isMatch);
    });
    document.querySelectorAll('.wf-mega-cat-pill').forEach(p => {
      p.classList.toggle('active', p.getAttribute('data-cat-id') === catId);
    });
    document.querySelectorAll('.wf-mega-cat-content').forEach(c => {
      c.style.display = (c.getAttribute('data-cat-id') === catId) ? 'grid' : 'none';
    });
    dropdownEl.classList.add('open');
    if (window.WebAudio) WebAudio.play('click');
  }
  function scheduleClose() {
    closeTimer = setTimeout(closeMenu, 180);
  }
  function closeMenu() {
    if (!dropdownEl) return;
    dropdownEl.classList.remove('open');
    document.querySelectorAll('.wf-cat-tab').forEach(t => t.classList.remove('menu-open'));
  }
  return {
    init: init,
    openCategory: openCategory,
    closeMenu: closeMenu
  };
})();
document.addEventListener('DOMContentLoaded', MegaMenu.init);
