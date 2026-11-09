var WebAudio = (function() {
  let ctx = null;
  let enabled = localStorage.getItem('wf_sound_enabled') === 'true';
  function getCtx() {
    if (!ctx && (window.AudioContext || window.webkitAudioContext)) {
      ctx = new (window.AudioContext || window.webkitAudioContext)();
    }
    if (ctx && ctx.state === 'suspended') ctx.resume();
    return ctx;
  }
  return {
    isEnabled: function() { return enabled; },
    setEnabled: function(val) {
      enabled = !!val;
      localStorage.setItem('wf_sound_enabled', enabled ? 'true' : 'false');
      let btn = document.getElementById('wf-btn-sound');
      if (btn) btn.classList.toggle('active', enabled);
      if (enabled) this.play('switch');
    },
    toggle: function() {
      this.setEnabled(!enabled);
    },
    play: function(type) {
      if (!enabled) return;
      try {
        let c = getCtx();
        if (!c) return;
        let osc = c.createOscillator();
        let gain = c.createGain();
        osc.connect(gain);
        gain.connect(c.destination);
        let now = c.currentTime;
        if (type === 'click') {
          osc.type = 'sine';
          osc.frequency.setValueAtTime(800, now);
          osc.frequency.exponentialRampToValueAtTime(300, now + 0.04);
          gain.gain.setValueAtTime(0.08, now);
          gain.gain.linearRampToValueAtTime(0.001, now + 0.04);
          osc.start(now);
          osc.stop(now + 0.04);
        } else if (type === 'switch') {
          osc.type = 'triangle';
          osc.frequency.setValueAtTime(450, now);
          osc.frequency.exponentialRampToValueAtTime(750, now + 0.06);
          gain.gain.setValueAtTime(0.07, now);
          gain.gain.linearRampToValueAtTime(0.001, now + 0.06);
          osc.start(now);
          osc.stop(now + 0.06);
        } else if (type === 'notify') {
          osc.type = 'sine';
          osc.frequency.setValueAtTime(587.33, now); // D5
          osc.frequency.setValueAtTime(880, now + 0.08); // A5
          gain.gain.setValueAtTime(0.1, now);
          gain.gain.exponentialRampToValueAtTime(0.001, now + 0.22);
          osc.start(now);
          osc.stop(now + 0.22);
        } else if (type === 'error') {
          osc.type = 'sawtooth';
          osc.frequency.setValueAtTime(220, now);
          osc.frequency.linearRampToValueAtTime(140, now + 0.12);
          gain.gain.setValueAtTime(0.08, now);
          gain.gain.linearRampToValueAtTime(0.001, now + 0.12);
          osc.start(now);
          osc.stop(now + 0.12);
        }
      } catch(e) {}
    }
  };
})();
