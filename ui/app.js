// OPEN5150 - High Gain Precision Tube Amplifier & 3D Studio Controller
// Minimalist Flat Dark Hardware Architecture

let params = {
  channel: 1,       // 1 = Red Lead, 0 = Green
  crunch: 0,        // 1 = Green Crunch, 0 = Green Clean
  bright: 0,        // 1 = Bright (+6dB air), 0 = Off
  gateThreshold: -55.0,
  preGain: 5.0,
  bass: 5.0,
  mid: 5.0,
  treble: 5.0,
  postGain: 3.0,
  resonance: 5.0,
  presence: 5.0,
  bufferSize: 64,

  // Cabinet & Mic Studio State
  cabEnabled: false,
  customPath: '',
  micId: 0,
  posX: 0.25,       // 0.0 (Center) to 1.0 (Edge)
  distanceZ: 0.15,  // 0.0 (Grille) to 1.0 (10 cm)
  is45: false,
  lowCut: 75.0,
  highCut: 11500.0
};

// Bridge to macOS Native WKWebView
function postToNative(action, data = {}) {
  if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.audioEngine) {
    window.webkit.messageHandlers.audioEngine.postMessage({ action, ...data });
  }
}

// -----------------------------------------------------------------------------
// 0. View Switcher (ALL / AMP / CAB)
// -----------------------------------------------------------------------------
function initViewSwitch() {
  const btns = document.querySelectorAll('.view-btn');
  btns.forEach(btn => {
    btn.addEventListener('click', () => {
      btns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const view = btn.dataset.view;
      document.documentElement.dataset.view = view;

      setTimeout(() => {
        const conePad = document.getElementById('conePad');
        const micTarget = document.getElementById('micTarget');
        if (conePad && micTarget) {
          const padRect = conePad.getBoundingClientRect();
          const cx = padRect.width / 2;
          const cy = padRect.height / 2;
          const maxR = Math.max(16, cx - 6);
          const r = Math.min(params.posX * maxR, maxR);
          micTarget.style.left = `${cx + r}px`;
          micTarget.style.top = `${cy}px`;
        }
      }, 50);
    });
  });
}

// -----------------------------------------------------------------------------
// 1. Rotary Dials Matrix
// -----------------------------------------------------------------------------
function initDials() {
  const arcLength = 188.5; // 270 degrees on r=40 circle

  document.querySelectorAll('.dial-widget').forEach(widget => {
    const id = widget.dataset.id;
    const min = parseFloat(widget.dataset.min ?? '0.0');
    const max = parseFloat(widget.dataset.max ?? '10.0');
    const defaultVal = parseFloat(widget.dataset.default ?? '5.0');
    const step = parseFloat(widget.dataset.step ?? '0.1');

    const fillArc = widget.querySelector('.dial-fill');
    const needle = widget.querySelector('.dial-needle');
    const centerVal = widget.querySelector('.dial-center-val');

    function updateVisual(val) {
      const norm = Math.max(0, Math.min(1, (val - min) / (max - min)));
      const offset = arcLength * (1 - norm);
      fillArc.style.strokeDashoffset = offset;

      const angleDeg = -135 + norm * 270;
      needle.setAttribute('transform', `rotate(${angleDeg} 50 50)`);

      centerVal.textContent = val.toFixed(1);
    }

    // Set initial position
    const currentVal = params[id] !== undefined ? params[id] : defaultVal;
    updateVisual(currentVal);
    widget.addEventListener('parameterchange', () => updateVisual(params[id]));

    // Mouse Drag Interaction (vertical delta)
    let startY = 0;
    let startVal = 0;

    function onMouseDown(e) {
      startY = e.clientY;
      startVal = params[id] !== undefined ? params[id] : defaultVal;

      function onMouseMove(ev) {
        const deltaPx = startY - ev.clientY;
        const range = max - min;
        const deltaVal = (deltaPx / 120.0) * range;
        let newVal = Math.max(min, Math.min(max, startVal + deltaVal));
        newVal = Math.round(newVal / step) * step;

        if (newVal !== params[id]) {
          params[id] = newVal;
          updateVisual(newVal);
          postToNative('setParam', { id, value: newVal });
        }
      }

      function onMouseUp() {
        window.removeEventListener('mousemove', onMouseMove);
        window.removeEventListener('mouseup', onMouseUp);
      }

      window.addEventListener('mousemove', onMouseMove);
      window.addEventListener('mouseup', onMouseUp);
    }

    widget.addEventListener('mousedown', onMouseDown);

    // Mouse Wheel fine adjustment
    widget.addEventListener('wheel', (e) => {
      e.preventDefault();
      const direction = e.deltaY < 0 ? 1 : -1;
      let newVal = Math.max(min, Math.min(max, params[id] + direction * step));
      newVal = Math.round(newVal / step) * step;
      if (newVal !== params[id]) {
        params[id] = newVal;
        updateVisual(newVal);
        postToNative('setParam', { id, value: newVal });
      }
    }, { passive: false });

    // Double-click to reset to default
    widget.addEventListener('dblclick', () => {
      params[id] = defaultVal;
      updateVisual(defaultVal);
      postToNative('setParam', { id, value: defaultVal });
    });
  });
}

window.restoreAmpParameters = function(values) {
  Object.assign(params, values);
  document.querySelectorAll('.dial-widget').forEach(widget => {
    if (Object.hasOwn(values, widget.dataset.id)) {
      widget.dispatchEvent(new Event('parameterchange'));
    }
  });
};

// -----------------------------------------------------------------------------
// 2. Channel Voicing Selector (Red Lead, Green Crunch, Green Clean)
// -----------------------------------------------------------------------------
function initVoicing() {
  const btnModeRed = document.getElementById('btnModeRed');
  const btnModeCrunch = document.getElementById('btnModeCrunch');
  const btnModeClean = document.getElementById('btnModeClean');
  const btnBright = document.getElementById('btnBright');

  function setMode(channel, crunch) {
    params.channel = channel;
    params.crunch = crunch;

    btnModeRed.classList.toggle('active', channel === 1);
    btnModeCrunch.classList.toggle('active', channel === 0 && crunch === 1);
    btnModeClean.classList.toggle('active', channel === 0 && crunch === 0);

    if (channel === 1) {
      document.documentElement.dataset.theme = 'red';
      btnBright.style.display = 'none';
    } else if (crunch === 1) {
      document.documentElement.dataset.theme = 'crunch';
      btnBright.style.display = 'block';
    } else {
      document.documentElement.dataset.theme = 'clean';
      btnBright.style.display = 'block';
    }
    postToNative('setMode', { channel, crunch });
  }

  btnModeRed.addEventListener('click', () => setMode(1, 0));
  btnModeCrunch.addEventListener('click', () => setMode(0, 1));
  btnModeClean.addEventListener('click', () => setMode(0, 0));

  btnBright.addEventListener('click', () => {
    params.bright = (params.bright === 1) ? 0 : 1;
    btnBright.classList.toggle('on', params.bright === 1);
    postToNative('setBright', { value: params.bright });
  });

  // Default to Red Lead
  setMode(1, 0);
}

// -----------------------------------------------------------------------------
// 3. Signal Oscilloscope Canvas (Flat Minimalist Line)
// -----------------------------------------------------------------------------
let latestMeters = {
  peakIn: 0.0,
  rmsIn: 0.0,
  gateOpen: false,
  peakOut: 0.0,
  rmsOut: 0.0,
  smoothPeakOut: 0.0
};

function initScope() {
  const canvas = document.getElementById('scopeCanvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width;
  const h = canvas.height;
  const midY = h / 2;

  let phase = 0;

  function renderScope() {
    requestAnimationFrame(renderScope);

    latestMeters.smoothPeakOut += (latestMeters.peakOut - latestMeters.smoothPeakOut) * 0.25;

    // Dark flat background clearing
    ctx.fillStyle = '#0d0e11';
    ctx.fillRect(0, 0, w, h);

    // Subtle center line
    ctx.strokeStyle = '#1a1b22';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, midY);
    ctx.lineTo(w, midY);
    ctx.stroke();

    // Channel color
    const theme = document.documentElement.dataset.theme;
    let strokeColor = '#e53935';
    if (theme === 'crunch') strokeColor = '#10b981';
    else if (theme === 'clean') strokeColor = '#0284c7';

    const amp = Math.min(1.0, latestMeters.smoothPeakOut * 3.5);
    phase += 0.22 + amp * 0.15;

    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1.4;
    ctx.beginPath();

    const points = 45;
    for (let i = 0; i <= points; i++) {
      const x = (i / points) * w;
      const t = (i / points) * Math.PI * 4 + phase;

      let yOffset = 0;
      if (amp > 0.005) {
        const fundamental = Math.sin(t);
        const secondHarmonic = Math.sin(t * 2 + 0.3) * 0.35;
        const thirdHarmonic = Math.sin(t * 3) * 0.2;
        let rawWave = fundamental + secondHarmonic + thirdHarmonic;

        if (params.channel === 1) {
          rawWave = Math.tanh(rawWave * 2.2);
        } else if (params.crunch === 1) {
          rawWave = Math.tanh(rawWave * 1.6);
        }

        yOffset = rawWave * amp * (midY - 2);
      } else {
        yOffset = (Math.random() - 0.5) * 0.8;
      }

      const y = midY - yOffset;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }

    ctx.stroke();
  }

  requestAnimationFrame(renderScope);
}

// -----------------------------------------------------------------------------
// 4. Cabinet & 3D Microphone Studio
// -----------------------------------------------------------------------------
function initCabStudio() {
  const btnPower = document.getElementById('btnCabPower');
  const btnBrowseIr = document.getElementById('btnBrowseIr');
  const micButtons = document.querySelectorAll('.mic-btn, .mic-chip');

  const conePad = document.getElementById('conePad');
  const micTarget = document.getElementById('micTarget');
  const micTag = document.getElementById('micTag');

  const sliderDistance = document.getElementById('sliderDistance');
  const valDistance = document.getElementById('valDistance');
  const btnAngle0 = document.getElementById('btnAngle0');
  const btnAngle45 = document.getElementById('btnAngle45');

  const sliderLowCut = document.getElementById('sliderLowCut');
  const valLowCut = document.getElementById('valLowCut');
  const sliderHighCut = document.getElementById('sliderHighCut');
  const valHighCut = document.getElementById('valHighCut');

  // Cab Power Switch
  btnPower.addEventListener('click', () => {
    params.cabEnabled = !params.cabEnabled;
    btnPower.classList.toggle('active', params.cabEnabled);
    const powerText = btnPower.querySelector('.power-text') || btnPower.querySelector('span:last-child');
    if (powerText) powerText.textContent = params.cabEnabled ? 'ACTIVE' : 'BYPASS';
    postToNative('setCabEnabled', { enabled: params.cabEnabled });
  });

  btnBrowseIr.addEventListener('click', () => {
    postToNative('browseCustomIr');
  });

  // Mic Selection
  micButtons.forEach(chip => {
    chip.addEventListener('click', () => {
      micButtons.forEach(c => c.classList.remove('active'));
      chip.classList.add('active');
      params.micId = parseInt(chip.dataset.mic);
      postToNative('setMic', { micId: params.micId });
    });
  });

  // 2D Speaker Cone Radar Positioning
  function updateMicPositionUI(xNorm, angleRad = 0) {
    const padRect = conePad.getBoundingClientRect();
    const w = padRect.width > 0 ? padRect.width : 96;
    const h = padRect.height > 0 ? padRect.height : 96;
    const cx = w / 2;
    const cy = h / 2;
    const maxR = Math.max(16, cx - 6);

    const r = Math.min(xNorm * maxR, maxR);
    const px = cx + r * Math.cos(angleRad);
    const py = cy + r * Math.sin(angleRad);

    micTarget.style.left = `${px}px`;
    micTarget.style.top = `${py}px`;

    let tag = '';
    if (xNorm < 0.18) tag = `Centro (${xNorm.toFixed(2)})`;
    else if (xNorm < 0.45) tag = `Cap-Edge ★`;
    else if (xNorm < 0.75) tag = `Cono`;
    else tag = `Borde (${xNorm.toFixed(2)})`;

    micTag.textContent = tag;
  }

  setTimeout(() => updateMicPositionUI(params.posX, 0), 50);

  let isDraggingMic = false;

  function onPadMove(e) {
    const padRect = conePad.getBoundingClientRect();
    const cx = padRect.left + padRect.width / 2;
    const cy = padRect.top + padRect.height / 2;

    const dx = e.clientX - cx;
    const dy = e.clientY - cy;
    const dist = Math.sqrt(dx * dx + dy * dy);
    const maxR = Math.max(16, (padRect.width / 2) - 6);

    const xNorm = Math.min(1.0, Math.max(0.0, dist / maxR));
    const angle = Math.atan2(dy, dx);

    params.posX = xNorm;
    updateMicPositionUI(xNorm, angle);

    postToNative('setMicPosition', {
      x: params.posX,
      z: params.distanceZ,
      is45: params.is45
    });
  }

  conePad.addEventListener('mousedown', (e) => {
    isDraggingMic = true;
    onPadMove(e);

    function onMouseMove(ev) {
      if (isDraggingMic) onPadMove(ev);
    }

    function onMouseUp() {
      isDraggingMic = false;
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
    }

    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
  });

  // Distance Slider (Z)
  sliderDistance.addEventListener('input', () => {
    const val = parseFloat(sliderDistance.value);
    params.distanceZ = val;
    valDistance.textContent = `${(val * 10).toFixed(1)} cm`;
    postToNative('setMicPosition', {
      x: params.posX,
      z: params.distanceZ,
      is45: params.is45
    });
  });

  // Angle Switch (0° / 45°)
  btnAngle0.addEventListener('click', () => {
    params.is45 = false;
    btnAngle0.classList.add('active');
    btnAngle45.classList.remove('active');
    postToNative('setMicPosition', {
      x: params.posX,
      z: params.distanceZ,
      is45: false
    });
  });

  btnAngle45.addEventListener('click', () => {
    params.is45 = true;
    btnAngle45.classList.add('active');
    btnAngle0.classList.remove('active');
    postToNative('setMicPosition', {
      x: params.posX,
      z: params.distanceZ,
      is45: true
    });
  });

  // Studio High-Pass & Low-Pass Filters
  sliderLowCut.addEventListener('input', () => {
    const val = parseFloat(sliderLowCut.value);
    params.lowCut = val;
    valLowCut.textContent = `${Math.round(val)} Hz`;
    postToNative('setCabFilters', {
      lowCut: params.lowCut,
      highCut: params.highCut
    });
  });

  sliderHighCut.addEventListener('input', () => {
    const val = parseFloat(sliderHighCut.value);
    params.highCut = val;
    valHighCut.textContent = `${(val / 1000).toFixed(1)} kHz`;
    postToNative('setCabFilters', {
      lowCut: params.lowCut,
      highCut: params.highCut
    });
  });

  window.addEventListener('resize', () => {
    updateMicPositionUI(params.posX, 0);
  });
}

// -----------------------------------------------------------------------------
// 5. Hardware Device Selectors & Buffer
// -----------------------------------------------------------------------------
function initHardware() {
  const inSelect = document.getElementById('selectInput');
  const outSelect = document.getElementById('selectOutput');
  const bufSelect = document.getElementById('selectBuffer');

  inSelect.addEventListener('change', () => {
    postToNative('changeDevice', {
      type: 'input',
      id: parseInt(inSelect.value)
    });
  });

  outSelect.addEventListener('change', () => {
    postToNative('changeDevice', {
      type: 'output',
      id: parseInt(outSelect.value)
    });
  });

  bufSelect.addEventListener('change', () => {
    const buf = parseInt(bufSelect.value);
    params.bufferSize = buf;
    postToNative('changeBuffer', { bufferSize: buf });
  });
}

// -----------------------------------------------------------------------------
// 6. Lossless DAW Session Recorder
// -----------------------------------------------------------------------------
let isCurrentlyRecording = false;
let lastSavedRecordingPath = '';
let toastTimeout = null;

function initRecorder() {
  const btnRecord = document.getElementById('btnRecord');
  const btnOpenRecordings = document.getElementById('btnOpenRecordings');
  const btnToastReveal = document.getElementById('btnToastReveal');

  btnRecord.addEventListener('click', () => {
    if (!isCurrentlyRecording) {
      postToNative('startRecording');
    } else {
      postToNative('stopRecording');
    }
  });

  btnOpenRecordings.addEventListener('click', () => {
    postToNative('revealInFinder', { path: '' });
  });

  btnToastReveal.addEventListener('click', () => {
    if (lastSavedRecordingPath) {
      postToNative('revealInFinder', { path: lastSavedRecordingPath });
    }
  });
}

// -----------------------------------------------------------------------------
// Callbacks from Native C++ Engine
// -----------------------------------------------------------------------------
window.populateDevices = function(inputs, outputs, currentIn, currentOut, currentBuf, sampleRate) {
  const inSelect = document.getElementById('selectInput');
  const outSelect = document.getElementById('selectOutput');
  const bufSelect = document.getElementById('selectBuffer');

  inSelect.innerHTML = '';
  inputs.forEach(dev => {
    const opt = document.createElement('option');
    opt.value = dev.id;
    opt.textContent = `${dev.name} (${dev.inCh} in)`;
    if (dev.id === currentIn) opt.selected = true;
    inSelect.appendChild(opt);
  });

  outSelect.innerHTML = '';
  outputs.forEach(dev => {
    const opt = document.createElement('option');
    opt.value = dev.id;
    opt.textContent = `${dev.name} (${dev.outCh} out)`;
    if (dev.id === currentOut) opt.selected = true;
    outSelect.appendChild(opt);
  });

  bufSelect.value = currentBuf;
  params.bufferSize = currentBuf;

  const latMs = (currentBuf / sampleRate * 1000).toFixed(2);
  document.getElementById('lblLatency').textContent = `${latMs} ms`;
};

window.onCustomIrLoaded = function(fileName, fullPath) {
  params.customPath = fullPath;
  const cabDesc = document.getElementById('cabDesc');
  cabDesc.textContent = `IR: ${fileName}`;
};

window.restoreCabState = function(values) {
  Object.assign(params, values);
  document.getElementById('cabDesc').textContent = params.customPath
    ? `IR: ${params.customPath.split('/').pop()}` : 'Sin IR cargada. Selecciona un archivo WAV propio.';
  const button = document.getElementById('btnCabPower');
  button.classList.toggle('active', params.cabEnabled);
  const powerText = button.querySelector('.power-text') || button.querySelector('span:last-child');
  if (powerText) powerText.textContent = params.cabEnabled ? 'ACTIVE' : 'BYPASS';
};

window.onRecordingFinished = function(savedPath) {
  lastSavedRecordingPath = savedPath;
  const fileName = savedPath.split('/').pop();
  const toastMsg = document.getElementById('toastMsg');
  const recToast = document.getElementById('recToast');

  toastMsg.textContent = `Take guardado: ${fileName}`;
  recToast.classList.add('show');

  if (toastTimeout) clearTimeout(toastTimeout);
  toastTimeout = setTimeout(() => {
    recToast.classList.remove('show');
  }, 5000);
};

window.updateMeters = function(peakIn, rmsIn, gateOpen, gateRed, peakOut, rmsOut, isRec, recSecs) {
  latestMeters.peakIn = peakIn;
  latestMeters.rmsIn = rmsIn;
  latestMeters.gateOpen = gateOpen;
  latestMeters.peakOut = peakOut;
  latestMeters.rmsOut = rmsOut;

  // 1. Input Meter (Strict tabular width)
  const pInDb = peakIn > 1e-4 ? 20 * Math.log10(peakIn) : -80;
  const inPct = Math.max(0, Math.min(100, (pInDb + 60) * (100 / 60)));
  const hudBarIn = document.getElementById('hudBarIn');
  const hudValIn = document.getElementById('hudValIn');
  if (hudBarIn) hudBarIn.style.width = `${inPct}%`;
  if (hudValIn) {
    if (pInDb <= -75) hudValIn.textContent = '-inf';
    else if (pInDb > 0) hudValIn.textContent = `+${pInDb.toFixed(1)}`;
    else hudValIn.textContent = `${pInDb.toFixed(1)}`;
  }

  // 2. Gate Status Badge
  const gatePill = document.getElementById('gatePill');
  if (gatePill) {
    if (gateOpen) {
      gatePill.className = 'gate-badge open';
      gatePill.textContent = 'OPEN';
    } else {
      gatePill.className = 'gate-badge closed';
      gatePill.textContent = 'MUTE';
    }
  }

  // 3. Output Meter (Strict tabular width)
  const pOutDb = peakOut > 1e-4 ? 20 * Math.log10(peakOut) : -80;
  const outPct = Math.max(0, Math.min(100, (pOutDb + 60) * (100 / 60)));
  const hudBarOut = document.getElementById('hudBarOut');
  const hudValOut = document.getElementById('hudValOut');
  if (hudBarOut) hudBarOut.style.width = `${outPct}%`;
  if (hudValOut) {
    if (pOutDb <= -75) hudValOut.textContent = '-inf';
    else if (pOutDb > 0) hudValOut.textContent = `+${pOutDb.toFixed(1)}`;
    else hudValOut.textContent = `${pOutDb.toFixed(1)}`;
  }

  // 4. Lossless Session Recorder State
  isCurrentlyRecording = isRec;
  const btnRecord = document.getElementById('btnRecord');
  const lblRecText = document.getElementById('lblRecText');
  const lblRecTimer = document.getElementById('lblRecTimer');

  if (isRec) {
    btnRecord.classList.add('recording');
    lblRecText.textContent = 'STOP';
    lblRecTimer.classList.add('active');

    const totalSecs = Math.floor(recSecs);
    const mins = Math.floor(totalSecs / 60).toString().padStart(2, '0');
    const secs = (totalSecs % 60).toString().padStart(2, '0');
    const tenths = Math.floor((recSecs - totalSecs) * 10);
    lblRecTimer.textContent = `${mins}:${secs}.${tenths}`;
  } else {
    btnRecord.classList.remove('recording');
    lblRecText.textContent = 'REC';
    lblRecTimer.classList.remove('active');
  }
};

// -----------------------------------------------------------------------------
// App Initialization
// -----------------------------------------------------------------------------
document.addEventListener('DOMContentLoaded', () => {
  initViewSwitch();
  initDials();
  initVoicing();
  initScope();
  initCabStudio();
  initHardware();
  initRecorder();
  postToNative('uiReady');
});
