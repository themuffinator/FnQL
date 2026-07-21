(function () {
  'use strict';

  window.__fnql_settings_script_loaded = true;

  var active = false;
  var currentRoot = null;
  var refreshTimer = null;
  var observer = null;

  var replacedRetailVideoSettings = [
    'full mode (resolution)',
    'windowed mode (resolution)',
    'display refresh',
    'play fullscreen',
    'vertical sync'
  ];

  var groups = [
    {
      title: 'Display and renderer',
      settings: [
        { name: 'cl_renderer', title: 'Renderer backend', type: 'select', restart: 'video', options: [
          ['glx', 'GLx (default)'], ['vk', 'Vulkan'], ['rtx', 'RTX']
        ], help: 'Selects a built renderer module. Unavailable modules fail through FnQL\'s normal renderer fallback to GLx.' },
        { name: 'r_fullscreen', title: 'Fullscreen', type: 'bool', restart: 'video' },
        { name: 'r_noborder', title: 'Borderless window', type: 'bool', restart: 'video' },
        { name: 'r_mode', title: 'Windowed video mode', type: 'number', restart: 'video', help: '-2 uses the desktop mode; -1 uses the custom dimensions.' },
        { name: 'r_modefullscreen', title: 'Fullscreen video mode', type: 'number', restart: 'video', help: 'FnQL keeps a dedicated fullscreen mode instead of overwriting the windowed mode.' },
        { name: 'r_customwidth', title: 'Custom width', type: 'number', min: 4, restart: 'video' },
        { name: 'r_customheight', title: 'Custom height', type: 'number', min: 4, restart: 'video' },
        { name: 'r_displayrefresh', title: 'Fullscreen refresh override', type: 'number', min: 0, max: 500, restart: 'video', help: '0 keeps the current monitor refresh rate.' },
        { name: 'r_swapinterval', title: 'Vertical sync', type: 'select', options: [['0', 'Off'], ['1', 'On']], help: 'Number of vertical blanks waited before presenting.' },
        { name: 'r_ext_multisample', title: 'Multisampling', type: 'select', restart: 'video', options: [['0', 'Off'], ['2', '2x'], ['4', '4x'], ['8', '8x']] },
        { name: 'r_renderscale', title: 'Render scale', type: 'number', min: 0, max: 4, step: 0.05, restart: 'video', help: '0 selects the backend default.' }
      ]
    },
    {
      title: 'FnQL rendering features',
      settings: [
        { name: 'r_fbo', title: 'Framebuffer rendering', type: 'bool', restart: 'video' },
        { name: 'r_hdr', title: 'High dynamic range pipeline', type: 'bool', restart: 'video' },
        { name: 'r_bloom', title: 'Bloom', type: 'bool' },
        { name: 'r_depthfade', title: 'Soft particle intersections', type: 'bool', restart: 'video' },
        { name: 'r_globalfog', title: 'Map fog sidecars', type: 'bool', restart: 'video', help: 'Enables optional maps/<map>.fog visual sidecars when supported.' },
        { name: 'r_globalfogstrength', title: 'Global fog strength', type: 'range', min: 0, max: 2, step: 0.05 },
        { name: 'r_celshading', title: 'Model cel shading', type: 'bool' },
        { name: 'r_celshadingworld', title: 'World cel outlines', type: 'bool' },
        { name: 'r_celshadingsteps', title: 'Cel lighting bands', type: 'range', min: 2, max: 8, step: 1 },
        { name: 'r_celoutline', title: 'Model silhouette outlines', type: 'bool' },
        { name: 'r_dlightmode', title: 'Dynamic light quality', type: 'select', options: [['0', 'Retail-compatible'], ['1', 'Per-pixel'], ['2', 'Per-pixel including models']] },
        { name: 'r_dlightshadows', title: 'Dynamic light shadows', type: 'bool', restart: 'video' }
      ]
    },
    {
      title: 'Player visibility',
      settings: [
        { name: 'cl_playerhighlight', title: 'Player highlighting', type: 'select', options: [['0', 'Off'], ['1', 'Rim light'], ['2', 'Outline'], ['3', 'Rim light and outline']] },
        { name: 'cl_playerhighlightrimintensity', title: 'Highlight rim intensity', type: 'range', min: 0, max: 2, step: 0.05 },
        { name: 'cl_playerhighlightoutlineintensity', title: 'Highlight outline intensity', type: 'range', min: 0, max: 2, step: 0.05 },
        { name: 'cl_playerhighlightoutlinescale', title: 'Highlight outline scale', type: 'range', min: 1.001, max: 1.25, step: 0.001 },
        { name: 'cl_playerhighlightredcolor', title: 'Red team highlight', type: 'text', help: 'R G B [A], each channel from 0 to 255.' },
        { name: 'cl_playerhighlightbluecolor', title: 'Blue team highlight', type: 'text', help: 'R G B [A], each channel from 0 to 255.' },
        { name: 'cl_playerhighlightfreecolor', title: 'Free-for-all highlight', type: 'text', help: 'R G B [A], each channel from 0 to 255.' }
      ]
    },
    {
      title: 'Interface and capture',
      settings: [
        { name: 'cl_menuaspect', title: 'Retail 4:3 menu aspect', type: 'bool' },
        { name: 'cl_menudepthoffield', title: 'In-game menu depth of field', type: 'range', min: 0, max: 1, step: 0.05 },
        { name: 'cl_menudepthoffieldtime', title: 'Depth-of-field fade time', type: 'range', min: 0, max: 1000, step: 10, help: 'Milliseconds.' },
        { name: 'cl_cinematicaspect', title: 'Retail cinematic aspect', type: 'bool' },
        { name: 'cl_autorecorddemo', title: 'Automatically record demos', type: 'bool' },
        { name: 'cl_drawrecording', title: 'Recording indicator', type: 'select', options: [['0', 'Hidden'], ['1', 'Detailed'], ['2', 'Compact REC']] },
        { name: 'r_levelshothidehud', title: 'Hide HUD in levelshots', type: 'bool' },
        { name: 'r_levelshothideviewweapon', title: 'Hide weapon in levelshots', type: 'bool' },
        { name: 'web_zoom', title: 'WebUI zoom', type: 'range', min: 25, max: 400, step: 5, help: 'Percentage.' },
        { name: 'web_console', title: 'WebUI console diagnostics', type: 'bool' }
      ]
    },
    {
      title: 'Audio backend',
      settings: [
        { name: 's_backend', title: 'Sound backend', type: 'select', restart: 'sound', options: [['openal', 'OpenAL spatial audio'], ['legacy', 'Legacy software mixer']] },
        { name: 's_aldevice', title: 'OpenAL device', type: 'text', restart: 'sound', help: 'Leave empty for the system default.' },
        { name: 's_alhrtf', title: 'Headphone HRTF', type: 'select', restart: 'sound', options: [['auto', 'Automatic'], ['on', 'On'], ['off', 'Off']] },
        { name: 's_aloutputmode', title: 'OpenAL output mode', type: 'select', restart: 'sound', options: [['auto', 'Automatic'], ['headphones', 'Headphones'], ['speakers', 'Speakers'], ['surround', 'Surround'], ['quad', 'Quad'], ['5.1', '5.1'], ['6.1', '6.1'], ['7.1', '7.1']] },
        { name: 's_alfrequency', title: 'OpenAL mix frequency', type: 'number', min: 8000, max: 192000, step: 1000, restart: 'sound' },
        { name: 's_aloutputlimiter', title: 'OpenAL output limiter', type: 'bool', restart: 'sound' },
        { name: 's_alspatializestereo', title: 'Spatialize stereo world sounds', type: 'bool', restart: 'sound' },
        { name: 's_mutewhenunfocused', title: 'Mute when unfocused', type: 'bool' },
        { name: 's_mutewhenminimized', title: 'Mute when minimized', type: 'bool' }
      ]
    }
  ];

  function qz() {
    return window.qz_instance && typeof window.qz_instance.GetConfig === 'function' ? window.qz_instance : null;
  }

  function cvarCache() {
    var bridge = qz();
    var config = bridge ? bridge.GetConfig() : null;
    return config && config.cvars ? config.cvars : {};
  }

  function settingsSignature(cache) {
    var values = [];
    var groupIndex;
    var settingIndex;
    for (groupIndex = 0; groupIndex < groups.length; groupIndex += 1) {
      for (settingIndex = 0; settingIndex < groups[groupIndex].settings.length; settingIndex += 1) {
        var name = groups[groupIndex].settings[settingIndex].name.toLowerCase();
        if (Object.prototype.hasOwnProperty.call(cache, name)) {
          values.push(name + '=' + String(cache[name]));
        }
      }
    }
    return values.join('\n');
  }

  function hasOwn(object, name) {
    return Object.prototype.hasOwnProperty.call(object, name.toLowerCase());
  }

  function element(tag, className, text) {
    var node = document.createElement(tag);
    if (className) {
      node.className = className;
    }
    if (typeof text !== 'undefined') {
      node.appendChild(document.createTextNode(text));
    }
    return node;
  }

  function setCvar(name, value) {
    var bridge = qz();
    if (bridge) {
      bridge.SetCvar(name, String(value));
      var panel = currentRoot ? currentRoot.querySelector('.fnql-settings-panel') : null;
      if (panel) {
        panel.__fnqlSettingsSignature = settingsSignature(cvarCache());
      }
    }
  }

  function makeControl(setting, value) {
    var control;
    var option;
    var index;

    if (setting.type === 'bool') {
      control = element('input');
      control.type = 'checkbox';
      control.checked = parseFloat(value) !== 0;
      control.onchange = function () { setCvar(setting.name, control.checked ? '1' : '0'); };
      return control;
    }

    if (setting.type === 'select') {
      control = element('select');
      for (index = 0; index < setting.options.length; index += 1) {
        option = element('option', '', setting.options[index][1]);
        option.value = setting.options[index][0];
        control.appendChild(option);
      }
      if (!Array.prototype.some.call(control.options, function (item) { return item.value === value; })) {
        option = element('option', '', value + ' (current)');
        option.value = value;
        control.insertBefore(option, control.firstChild);
      }
      control.value = value;
      control.onchange = function () { setCvar(setting.name, control.value); };
      return control;
    }

    control = element('input');
    control.type = setting.type === 'range' ? 'range' : setting.type === 'number' ? 'number' : 'text';
    control.value = value;
    if (typeof setting.min !== 'undefined') { control.min = setting.min; }
    if (typeof setting.max !== 'undefined') { control.max = setting.max; }
    if (typeof setting.step !== 'undefined') { control.step = setting.step; }

    if (setting.type === 'range') {
      var wrapper = element('span');
      var output = element('span', 'fnql-range-value', value);
      control.oninput = function () { output.textContent = control.value; };
      control.onchange = function () { output.textContent = control.value; setCvar(setting.name, control.value); };
      wrapper.appendChild(control);
      wrapper.appendChild(output);
      return wrapper;
    }

    control.onchange = function () { setCvar(setting.name, control.value); };
    return control;
  }

  function makeSetting(setting, cache) {
    var row = element('div', 'fnql-setting');
    var copy = element('div', 'fnql-setting-copy');
    var control = element('div', 'fnql-setting-control');
    var canonicalName = setting.name.toLowerCase();

    copy.appendChild(element('span', 'fnql-setting-title', setting.title));
    copy.appendChild(element('span', 'fnql-setting-name', setting.name));
    if (setting.help) {
      copy.appendChild(element('span', 'fnql-setting-help', setting.help));
    }
    control.appendChild(makeControl(setting, String(cache[canonicalName])));
    row.appendChild(copy);
    row.appendChild(control);
    return row;
  }

  function sendCommand(command) {
    var bridge = qz();
    if (bridge) {
      bridge.SendGameCommand(command + '\n');
    }
  }

  function renderPanel(panel) {
    var cache = cvarCache();
    var renderedSettings = 0;
    var groupIndex;
    var settingIndex;

    panel.innerHTML = '';
    panel.appendChild(element('h1', '', 'FnQL Settings'));
    panel.appendChild(element('p', 'fnql-settings-intro', 'Engine-owned controls supplied by fnql-web.pak. Unsupported renderer or platform controls are hidden.'));

    for (groupIndex = 0; groupIndex < groups.length; groupIndex += 1) {
      var groupNode = element('div', 'fnql-settings-group');
      var groupCount = 0;
      groupNode.appendChild(element('h2', '', groups[groupIndex].title));
      for (settingIndex = 0; settingIndex < groups[groupIndex].settings.length; settingIndex += 1) {
        var setting = groups[groupIndex].settings[settingIndex];
        if (!hasOwn(cache, setting.name)) {
          continue;
        }
        groupNode.appendChild(makeSetting(setting, cache));
        groupCount += 1;
        renderedSettings += 1;
      }
      if (groupCount) {
        panel.appendChild(groupNode);
      }
    }

    if (!renderedSettings) {
      panel.appendChild(element('p', 'fnql-setting-unavailable', 'FnQL settings are waiting for the engine cvar snapshot.'));
    }

    var actions = element('p', 'fnql-settings-actions');
    var soundRestart = element('button', '', 'Restart Sound');
    var videoRestart = element('button', '', 'Apply Video Settings');
    soundRestart.onclick = function () { sendCommand('snd_restart'); };
    videoRestart.onclick = function () { sendCommand('vid_restart'); };
    actions.appendChild(soundRestart);
    actions.appendChild(videoRestart);
    panel.appendChild(actions);
    panel.__fnqlSettingsSignature = settingsSignature(cache);
  }

  function deactivate(retailTab) {
    active = false;
    if (!currentRoot) {
      return;
    }
    var nav = currentRoot.querySelector('nav.button-row');
    var section = currentRoot.querySelector('section');
    var panel = currentRoot.querySelector('.fnql-settings-panel');
    var tab = currentRoot.querySelector('.fnql-settings-tab');
    if (nav && retailTab && retailTab.parentNode === nav) {
      clearRetailActiveTabs(nav);
      retailTab.classList.add('active');
    }
    if (section) { section.style.display = ''; }
    if (panel) { panel.style.display = 'none'; }
    if (tab) { tab.className = 'button fnql-settings-tab'; }
  }

  function clearRetailActiveTabs(nav) {
    var links = nav.querySelectorAll('a.button.active');
    var index;
    for (index = 0; index < links.length; index += 1) {
      links[index].classList.remove('active');
    }
  }

  function activate() {
    active = true;
    attach();
  }

  function startsWithAny(text, values) {
    var index;
    text = String(text || '').toLowerCase().replace(/^\s+|\s+$/g, '');
    for (index = 0; index < values.length; index += 1) {
      if (text.indexOf(values[index]) === 0) {
        return true;
      }
    }
    return false;
  }

  function patchLegacyVideoSettings(section) {
    var heading = section.querySelector('h1');
    var content;
    var layout;
    var columns;
    var rows;
    var index;
    var label;

    if (!heading || String(heading.textContent).toLowerCase() !== 'video') {
      return;
    }
    content = heading.parentNode;
    layout = content ? content.querySelector('.row') : null;
    columns = layout ? layout.children : null;
    if (!columns || columns.length < 2) {
      return;
    }

    // Retail's second Video column consists solely of its legacy post-process
    // pipeline. FnQL does not implement or register the retail bloom path; its
    // supported FnQ3 controls live in the adjacent FnQL tab.
    columns[1].className += columns[1].className.indexOf('fnql-retail-unsupported') === -1 ? ' fnql-retail-unsupported' : '';
    columns[0].className += columns[0].className.indexOf('fnql-retail-supported') === -1 ? ' fnql-retail-supported' : '';

    rows = columns[0].querySelectorAll('.cvar');
    for (index = 0; index < rows.length; index += 1) {
      label = rows[index].querySelector('.children');
      if (label && startsWithAny(label.textContent, replacedRetailVideoSettings)) {
        rows[index].className += rows[index].className.indexOf('fnql-retail-replaced') === -1 ? ' fnql-retail-replaced' : '';
      }
    }

    if (!content.querySelector('.fnql-legacy-video-note')) {
      var note = element('p', 'fnql-legacy-video-note', 'FnQL renderer, display, and post-processing controls are available in the FnQL tab. Unsupported retail post-processing controls have been removed.');
      content.insertBefore(note, layout);
    }
  }

  function attach() {
    var root = document.querySelector('.game-settings');
    if (!root) {
      active = false;
      currentRoot = null;
      return;
    }
    if (currentRoot && currentRoot !== root) {
      active = false;
    }
    currentRoot = root;
    var nav = root.querySelector('nav.button-row');
    var section = root.querySelector('section');
    if (!nav || !section) {
      return;
    }
    patchLegacyVideoSettings(section);

    var tab = nav.querySelector('.fnql-settings-tab');
    if (!tab) {
      tab = element('button', 'button fnql-settings-tab', 'FnQL');
      tab.type = 'button';
      tab.onclick = activate;
      nav.appendChild(tab);
    } else if (tab.nextSibling) {
      // React can rebuild or reorder the retail links without replacing the
      // navigation node. Keep FnQL last in visual and keyboard tab order.
      nav.appendChild(tab);
    }

    var panel = root.querySelector('.fnql-settings-panel');
    if (!panel) {
      panel = element('div', 'fnql-settings-panel');
      panel.style.display = 'none';
      root.insertBefore(panel, section.nextSibling);
    }

    if (active) {
      clearRetailActiveTabs(nav);
      section.style.display = 'none';
      panel.style.display = '';
      tab.className = 'button fnql-settings-tab active';
      if (panel.__fnqlSettingsSignature !== settingsSignature(cvarCache())) {
        renderPanel(panel);
      }
    }
  }

  document.addEventListener('click', function (event) {
    var node = event.target;
    while (node && node !== document) {
      if (node.tagName === 'A' && node.parentNode && node.parentNode.className.indexOf('button-row') !== -1) {
        deactivate(node);
        return;
      }
      node = node.parentNode;
    }
  }, false);

  function start() {
    attach();
    if (window.MutationObserver) {
      observer = new MutationObserver(attach);
      observer.observe(document.body, { childList: true, subtree: true });
    }
    window.addEventListener('hashchange', function () {
      deactivate();
      window.setTimeout(attach, 0);
    }, false);
    // Awesomium's Chromium build does not reliably deliver MutationObserver
    // callbacks for every React route replacement. Keep a cheap periodic attach
    // so the tab appears after navigation even while it is not yet active.
    refreshTimer = window.setInterval(attach, 1000);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start, false);
  } else {
    start();
  }
}());
