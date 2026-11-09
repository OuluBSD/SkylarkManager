var TreeNavigator = (function() {
  function init() {
    // Bind keyboard shortcut '/' to focus nearest filter input
    document.addEventListener('keydown', function(e) {
      if (e.key === '/' && !['INPUT', 'TEXTAREA'].includes(document.activeElement.tagName)) {
        e.preventDefault();
        let inp = document.querySelector('.wf-tree-filter-input');
        if (inp) { inp.focus(); inp.select(); }
      }
    });
  }
  function toggleNode(event, row) {
    if (event) event.stopPropagation();
    let node = row.closest('.wf-tree-node');
    if (!node) return;
    let isExpanded = node.classList.toggle('is-expanded');
    node.setAttribute('aria-expanded', isExpanded ? 'true' : 'false');
    // Toggle open icon if configured
    let iconEl = row.querySelector('.wf-tree-icon');
    if (iconEl) {
      let closedIc = iconEl.getAttribute('data-icon-closed');
      let openIc = iconEl.getAttribute('data-icon-open');
      if (openIc) {
        // SVG swap handled dynamically if needed
      }
    }
    if (window.WebAudio) WebAudio.play('click');
  }
  function rowClick(event, row) {
    // If clicked directly on row (and not on an <a> link or chevron), toggle expansion
    if (event.target.tagName !== 'A' && !event.target.closest('.wf-tree-chevron')) {
      toggleNode(event, row);
    }
  }
  function expandAll(treeId) {
    let container = document.getElementById(treeId);
    if (!container) return;
    container.querySelectorAll('.wf-tree-node.has-children').forEach(n => {
      n.classList.add('is-expanded');
      n.setAttribute('aria-expanded', 'true');
    });
    if (window.WebAudio) WebAudio.play('switch');
  }
  function collapseAll(treeId) {
    let container = document.getElementById(treeId);
    if (!container) return;
    container.querySelectorAll('.wf-tree-node.has-children').forEach(n => {
      n.classList.remove('is-expanded');
      n.setAttribute('aria-expanded', 'false');
    });
    if (window.WebAudio) WebAudio.play('switch');
  }
  function filter(treeId, query) {
    let container = document.getElementById(treeId);
    if (!container) return;
    let q = (query || '').trim().toLowerCase();
    let bar = container.querySelector('.wf-tree-filter-bar');
    if (bar) bar.classList.toggle('has-text', q.length > 0);

    let allNodes = container.querySelectorAll('.wf-tree-node');
    if (!q) {
      allNodes.forEach(n => n.classList.remove('filter-hidden'));
      return;
    }
    allNodes.forEach(n => {
      let labelEl = n.querySelector('.wf-tree-label');
      let txt = labelEl ? labelEl.innerText.toLowerCase() : '';
      let matches = txt.includes(q);
      if (matches) {
        n.classList.remove('filter-hidden');
        // Expand all parents up to root
        let p = n.parentElement;
        while (p && p !== container) {
          if (p.classList.contains('wf-tree-node')) {
            p.classList.remove('filter-hidden');
            p.classList.add('is-expanded');
          }
          p = p.parentElement;
        }
      } else {
        n.classList.add('filter-hidden');
      }
    });
    // Ensure matched parents with children unhide their matching descendants
    container.querySelectorAll('.wf-tree-node:not(.filter-hidden)').forEach(n => {
      let parent = n.parentElement;
      while (parent && parent !== container) {
        if (parent.classList.contains('wf-tree-node')) parent.classList.remove('filter-hidden');
        parent = parent.parentElement;
      }
    });
  }
  function clearFilter(treeId) {
    let container = document.getElementById(treeId);
    if (!container) return;
    let inp = container.querySelector('.wf-tree-filter-input');
    if (inp) { inp.value = ''; filter(treeId, ''); inp.focus(); }
  }
  function toggleDock(dockId) {
    let el = document.getElementById(dockId);
    if (!el) return;
    let isCol = el.classList.toggle('collapsed');
    localStorage.setItem('wf_dock_' + dockId, isCol ? 'collapsed' : 'open');
    if (window.WebAudio) WebAudio.play('switch');
  }
  return {
    init: init,
    toggleNode: toggleNode,
    rowClick: rowClick,
    expandAll: expandAll,
    collapseAll: collapseAll,
    filter: filter,
    clearFilter: clearFilter,
    toggleDock: toggleDock
  };
})();
document.addEventListener('DOMContentLoaded', TreeNavigator.init);
