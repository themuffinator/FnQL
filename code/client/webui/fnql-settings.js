(function () {
  'use strict';

  window.__fnql_settings_script_loaded = true;

  // FnQL owns engine settings the retail Quake Live WebUI has no rows for, and
  // it owns replacements for the retail rows whose cvars FnQL does not honor.
  // Every control below is rendered with retail's own markup and stylesheet and
  // is appended into the retail section it belongs to, so there is no separate
  // FnQL tab.

  // cl_vidModes in code/client/cl_main.cpp. Retail's own mode table uses a
  // different numbering, so the retail resolution rows are replaced instead of
  // reused.
  var VIDEO_MODES = [
    ['-2', 'Desktop resolution'],
    ['-1', 'Custom resolution'],
    ['0', '320x240'],
    ['1', '400x300'],
    ['2', '512x384'],
    ['3', '640x480'],
    ['4', '800x600'],
    ['5', '960x720'],
    ['6', '1024x768'],
    ['7', '1152x864'],
    ['8', '1280x1024 (5:4)'],
    ['9', '1600x1200'],
    ['10', '2048x1536'],
    ['11', '856x480 (wide)'],
    ['12', '1280x960'],
    ['13', '1280x720'],
    ['14', '1280x800 (16:10)'],
    ['15', '1366x768'],
    ['16', '1440x900 (16:10)'],
    ['17', '1600x900'],
    ['18', '1680x1050 (16:10)'],
    ['19', '1920x1080'],
    ['20', '1920x1200 (16:10)'],
    ['21', '2560x1080 (21:9)'],
    ['22', '3440x1440 (21:9)'],
    ['23', '3840x2160'],
    ['24', '4096x2160 (4K)']
  ];

  var FULLSCREEN_MODES = [['', 'Use base video mode']].concat(VIDEO_MODES);

  function modeRow(name, title, help) {
    return {
      name: name, title: title, type: 'select',
      options: name === 'r_modeFullscreen' ? FULLSCREEN_MODES : VIDEO_MODES,
      restart: 'video', help: help
    };
  }

  // Retail rows FnQL cannot honor. Matched on the retail row title, and on the
  // cvar name when the retail widget exposes one.
  var RETAIL_UNSUPPORTED = {
    basic: [
      ['r_mode', 'full mode (resolution)'],
      ['r_windowedmode', 'windowed mode (resolution)']
    ],
    video: [
      ['r_mode', 'full mode (resolution)'],
      ['r_windowedmode', 'windowed mode (resolution)'],
      ['r_lightmap', 'enable lightmaps'],
      ['r_fullbright', 'enable full bright'],
      ['r_ambientscale', 'ambient light scale']
    ],
    game: [
      ['com_allowconsole', 'enable console']
    ],
    sound: [
      ['s_announcervolume', 'announcer volume'],
      ['s_killbeepvolume', 'kill beep volume'],
      ['s_mutebackground', 'mute in background'],
      ['s_ambient', 'ambient sound']
    ]
  };

  // Notes rendered above the retail rows of a section, explaining a removal.
  var SECTION_NOTES = {
    video: 'FnQL replaces the retail resolution rows with its own video mode ' +
      'table and does not implement the retail post-processing pipeline. The ' +
      'supported framebuffer, lighting, and effect controls are below.',
    sound: 'FnQL routes background muting through separate unfocused and ' +
      'minimized controls, and has no separate announcer or kill-beep volume ' +
      'lane.'
  };

  // Rows appended to the tail of a retail two-column layout row. Column 0 is
  // the retail left column, column 1 the retail right column.
  var COLUMN_ROWS = {
    binds: [
      {
        column: 0,
        settings: [
          { name: 'cl_freelook', title: 'Free Look', type: 'bool' },
          { name: 'in_mouse', title: 'Mouse Input Source', type: 'select', restart: 'video', options: [
            ['2', 'Quake Live raw input'], ['1', 'SDL relative'], ['0', 'Disabled'], ['-1', 'Win32 mouse']
          ] }
        ]
      },
      {
        column: 1,
        settings: [
          { name: 'cl_mouseAccelStyle', title: 'Acceleration Style', type: 'select', options: [
            ['2', 'Quake Live'], ['1', 'Power'], ['0', 'Classic']
          ], help: 'Quake Live style applies mouse DPI, the signed acceleration curve, the sensitivity cap, and the view filter.' },
          { name: 'cl_mouseAccelPower', title: 'Acceleration Power', type: 'range', min: 0, max: 16, step: 0.5,
            help: 'Acceleration exponent used by the Quake Live and Power styles.' },
          { name: 'm_filter', title: 'Mouse Smoothing', type: 'range', min: 0, max: 31, step: 1,
            help: 'Quake Live style averages this many completed view angles; the other styles treat any non-zero value as the legacy two-delta average.' }
        ]
      }
    ],
    basic: [
      {
        column: 1,
        settings: [
          modeRow('r_windowedMode', 'Windowed Resolution', 'Used while Play Fullscreen is off.'),
          modeRow('r_modeFullscreen', 'Fullscreen Resolution', 'Used while Play Fullscreen is on.')
        ]
      }
    ],
    team: [
      {
        column: 0,
        settings: [
          { name: 'cl_playerHighlightTeammateColor', title: 'Teammate Highlight Color', type: 'text',
            help: 'Overrides the team highlight color for players on your team. R G B or R G B A from 0 to 255. Leave blank to use the red and blue team colors.' }
        ]
      },
      {
        column: 1,
        settings: [
          { name: 'cl_playerHighlightEnemyColor', title: 'Opponent Highlight Color', type: 'text',
            help: 'Overrides the highlight color for players who are not on your team, and replaces the free-for-all color in non-team modes. R G B or R G B A from 0 to 255. Leave blank to use the team and free-for-all colors.' }
        ]
      }
    ],
    sound: [
      {
        column: 0,
        settings: [
          { name: 's_muteWhenUnfocused', title: 'Mute When Unfocused', type: 'bool' },
          { name: 's_muteWhenMinimized', title: 'Mute When Minimized', type: 'bool' }
        ]
      }
    ],
    video: [
      {
        column: 1,
        replacesRetailColumn: true,
        settings: [
          { name: 'cl_renderer', title: 'Renderer', type: 'select', restart: 'video', options: [
            ['glx', 'GLx'], ['vk', 'Vulkan'], ['rtx', 'RTX']
          ], help: 'An unavailable renderer falls back to GLx.' },
          modeRow('r_windowedMode', 'Windowed Resolution', 'Used while Play Fullscreen is off.'),
          { name: 'r_windowedWidth', title: 'Windowed Custom Width', type: 'text', restart: 'video',
            help: 'Used by the Custom resolution windowed mode.' },
          { name: 'r_windowedHeight', title: 'Windowed Custom Height', type: 'text', restart: 'video',
            help: 'Used by the Custom resolution windowed mode.' },
          modeRow('r_modeFullscreen', 'Fullscreen Resolution', 'Dedicated fullscreen mode. Falls back to the base video mode when unset.'),
          modeRow('r_mode', 'Base Video Mode', 'Used for fullscreen when no dedicated fullscreen resolution is set.'),
          { name: 'r_customWidth', title: 'Custom Width', type: 'text', restart: 'video',
            help: 'Used by the Custom base video mode.' },
          { name: 'r_customHeight', title: 'Custom Height', type: 'text', restart: 'video',
            help: 'Used by the Custom base video mode.' },
          { name: 'r_customPixelAspect', title: 'Custom Pixel Aspect', type: 'text', restart: 'video' },
          { name: 'r_noborder', title: 'Borderless Window', type: 'bool', restart: 'video' }
        ]
      }
    ]
  };

  // Retail-styled groups appended after the retail section content.
  var SECTION_GROUPS = {
    gamepad: [
      {
        title: 'Gamepad Device',
        left: [
          { name: 'in_joystick', title: 'Enable Gamepad', type: 'bool', restart: 'video' },
          { name: 'in_joystickProfile', title: 'Gamepad Profile', type: 'select', restart: 'video', options: [
            ['1', 'Quake Live'], ['0', 'Classic']
          ], help: 'The view and movement rows above need the Quake Live profile.' }
        ],
        right: [
          { name: 'joy_threshold', title: 'Movement Threshold', type: 'range', min: 0, max: 1, step: 0.01 },
          { name: 'in_joyBallScale', title: 'Movement Scale', type: 'text' }
        ]
      }
    ],
    game: [
      {
        title: 'Interface',
        left: [
          { name: 'cl_menuAspect', title: 'Retail 4:3 Menu Aspect', type: 'bool',
            help: 'Keeps menus and 3D widgets in centered 4:3 space instead of stretching them.' },
          { name: 'cl_cinematicAspect', title: 'Retail Cinematic Aspect', type: 'bool' },
          { name: 'cl_menuBlur', title: 'In-Game Menu Soft Focus', type: 'range', min: 0, max: 1, step: 0.05,
            help: 'Softens the scene behind the in-game menu. 0 leaves it sharp. Requires the framebuffer path.' }
        ],
        right: [
          { name: 'web_zoom', title: 'Menu Zoom', type: 'range', min: 25, max: 400, step: 5, help: 'Percent.' },
          { name: 'com_skipIdLogo', title: 'Skip Startup Logo', type: 'bool' },
          { name: 'com_introplayed', title: 'Skip Intro Cinematic', type: 'bool' },
          { name: 'r_inGameVideo', title: 'In-Game Video Shaders', type: 'bool' },
          { name: 'web_console', title: 'Menu Console Diagnostics', type: 'bool',
            help: 'Logs WebUI console output to the engine console.' }
        ]
      },
      {
        title: 'Console',
        left: [
          { name: 'con_scale', title: 'Console Scale', type: 'range', min: 0.5, max: 8, step: 0.1 },
          { name: 'con_scaleUniform', title: 'Retail Console Metrics', type: 'bool',
            help: 'Uses retail height-derived scaling instead of native pixel sizing.' },
          { name: 'con_backgroundStyle', title: 'Console Background', type: 'select', options: [
            ['1', 'Flat shaded'], ['0', 'Legacy texture']
          ] },
          { name: 'con_backgroundColor', title: 'Console Background Color', type: 'text',
            help: 'R G B from 0 to 255. Leave blank for the style default.' },
          { name: 'con_backgroundOpacity', title: 'Console Background Opacity', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'con_fade', title: 'Fade Console', type: 'bool' },
          { name: 'con_scrollSmooth', title: 'Smooth Console Scrolling', type: 'bool' }
        ],
        right: [
          { name: 'con_timestamps', title: 'Line Timestamps', type: 'bool' },
          { name: 'con_showClock', title: 'Console Clock', type: 'select', options: [
            ['0', 'Off'], ['1', '24 hour'], ['2', '12 hour']
          ] },
          { name: 'con_showVersion', title: 'Console Version', type: 'bool' },
          { name: 'con_completionPopup', title: 'Live Completion Popup', type: 'bool' },
          { name: 'con_autoClear', title: 'Clear Input On Close', type: 'bool' },
          { name: 'con_autoSay', title: 'Bare Input Says In Game', type: 'bool',
            help: 'Slash-prefixed input always stays a command.' },
          { name: 'con_sayRaw', title: 'Raw Say Text', type: 'bool' }
        ]
      },
      {
        title: 'Capture',
        left: [
          { name: 'cl_autoRecordDemo', title: 'Auto Record Demos', type: 'bool' },
          { name: 'cl_drawRecording', title: 'Recording Indicator', type: 'select', options: [
            ['0', 'Hidden'], ['1', 'Detailed'], ['2', 'Compact']
          ] },
          { name: 'cl_aviFrameRate', title: 'Video Capture Frame Rate', type: 'range', min: 1, max: 250, step: 1 },
          { name: 'cl_aviMotionJpeg', title: 'Video Capture MJPEG', type: 'bool' },
          { name: 'r_aviMotionJpegQuality', title: 'Video Capture Quality', type: 'range', min: 10, max: 100, step: 1 }
        ],
        right: [
          { name: 'r_screenshotJpegQuality', title: 'Screenshot JPEG Quality', type: 'range', min: 10, max: 100, step: 1 },
          { name: 'r_screenshotNameFormat', title: 'Screenshot Name Format', type: 'text',
            help: 'Tokens: {map} {date} {time} {datetime} {iter} {cmd} {face} {type}.' },
          { name: 'r_screenshotWriteViewpos', title: 'Screenshot View Metadata', type: 'bool' },
          { name: 'r_screenshotWatermark', title: 'Screenshot Watermark', type: 'text',
            help: 'Image path. Leave blank for no watermark.' },
          { name: 'r_levelshotHideHud', title: 'Hide HUD In Levelshots', type: 'bool' },
          { name: 'r_levelshotHideViewWeapon', title: 'Hide Weapon In Levelshots', type: 'bool' }
        ]
      },
      {
        title: 'Network',
        left: [
          { name: 'cl_timeNudge', title: 'Time Nudge', type: 'range', min: -20, max: 0, step: 1 },
          { name: 'cl_packetdup', title: 'Duplicate Packets', type: 'range', min: 0, max: 5, step: 1,
            help: 'Repeats previous client commands to mitigate packet loss.' }
        ],
        right: [
          { name: 'cl_allowDownload', title: 'Allow Downloads', type: 'select', options: [
            ['0', 'Off'], ['1', 'HTTP and UDP'], ['3', 'UDP only'], ['5', 'HTTP only']
          ] },
          { name: 'cl_mapAutoDownload', title: 'Auto Download Maps', type: 'bool' }
        ]
      },
      {
        title: 'Performance',
        left: [
          { name: 'com_maxfps', title: 'Frame Rate Limit', type: 'range', min: 0, max: 250, step: 5,
            help: '0 removes the limit.' },
          { name: 'com_maxfpsUnfocused', title: 'Unfocused Frame Rate Limit', type: 'range', min: 0, max: 250, step: 5 }
        ],
        right: [
          { name: 'com_yieldCPU', title: 'CPU Yield', type: 'range', min: 0, max: 16, step: 1,
            help: 'Milliseconds slept between rendered frames. 0 only if you see lag.' }
        ]
      }
    ],
    team: [
      {
        title: 'Player Highlighting',
        left: [
          { name: 'cl_playerHighlight', title: 'Player Highlighting', type: 'select', options: [
            ['0', 'Off'], ['1', 'Rimlight'], ['2', 'Outline'], ['3', 'Rimlight and outline']
          ], help: 'Draws an extra pass on other players. Your own model and corpses are excluded. The outline needs a stencil buffer.' },
          { name: 'cl_playerHighlightRimIntensity', title: 'Rimlight Intensity', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 'cl_playerHighlightOutlineIntensity', title: 'Outline Intensity', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 'cl_playerHighlightOutlineScale', title: 'Outline Thickness', type: 'range', min: 1.001, max: 1.25, step: 0.001 }
        ],
        right: [
          { name: 'cl_playerHighlightRedColor', title: 'Red Team Highlight Color', type: 'text',
            help: 'R G B or R G B A from 0 to 255.' },
          { name: 'cl_playerHighlightBlueColor', title: 'Blue Team Highlight Color', type: 'text',
            help: 'R G B or R G B A from 0 to 255.' },
          { name: 'cl_playerHighlightFreeColor', title: 'Free-For-All Highlight Color', type: 'text',
            help: 'Used in non-team modes. R G B or R G B A from 0 to 255.' }
        ]
      }
    ],
    weapons: [
      {
        title: 'Rail Trail Geometry',
        left: [
          { name: 'r_railWidth', title: 'Rail Trail Width', type: 'range', min: 1, max: 64, step: 1 },
          { name: 'r_railCoreWidth', title: 'Rail Core Width', type: 'range', min: 1, max: 32, step: 1 }
        ],
        right: [
          { name: 'r_railSegmentLength', title: 'Rail Segment Length', type: 'range', min: 4, max: 128, step: 1 }
        ]
      }
    ],
    video: [
      {
        title: 'Framebuffer And Anti-Aliasing',
        left: [
          { name: 'r_fbo', title: 'Framebuffer Rendering', type: 'bool',
            help: 'Required by anti-aliasing, render scaling, HDR, bloom, motion blur, liquids, map fog, and desaturation. Windowed overbright and gamma also need it. GLx applies it on the next frame; Vulkan and RTX need a video restart.' },
          { name: 'r_ext_multisample', title: 'Anti-Aliasing Samples', type: 'select', options: [
            ['0', 'Off'], ['2', '2x'], ['4', '4x'], ['8', '8x'], ['16', '16x']
          ], help: 'Unsupported counts resolve down to the best supported one.' },
          { name: 'r_ext_alpha_to_coverage', title: 'Alpha To Coverage', type: 'bool',
            help: 'Smooths alpha-tested edges while anti-aliasing is active. Off keeps strict legacy alpha-test parity.' },
          { name: 'r_ext_supersample', title: 'Supersampling', type: 'bool',
            help: 'Renders at double resolution and downsamples.' }
        ],
        right: [
          { name: 'r_renderScale', title: 'Render Scaling', type: 'select', options: [
            ['0', 'Off'],
            ['1', 'Nearest, stretched'],
            ['2', 'Nearest, aspect preserved'],
            ['3', 'Linear, stretched'],
            ['4', 'Linear, aspect preserved']
          ] },
          { name: 'r_renderWidth', title: 'Render Width', type: 'text', help: 'Used while render scaling is on.' },
          { name: 'r_renderHeight', title: 'Render Height', type: 'text', help: 'Used while render scaling is on.' },
          { name: 'r_hudExcludePostProcess', title: 'Keep HUD Out Of Post Effects', type: 'bool',
            help: 'Excludes 3D HUD scenes from bloom extraction.' }
        ]
      },
      {
        title: 'Texture And Geometry Detail',
        left: [
          { name: 'r_ext_texture_filter_anisotropic', title: 'Anisotropic Filtering', type: 'bool', restart: 'video' },
          { name: 'r_ext_max_anisotropy', title: 'Anisotropic Level', type: 'select', restart: 'video', options: [
            ['1', 'Off'], ['2', '2x'], ['4', '4x'], ['8', '8x'], ['16', '16x']
          ] },
          { name: 'r_picmipFilter', title: 'Texture Detail Scope', type: 'select', restart: 'video', options: [
            ['0', 'All images (legacy)'], ['1', 'World textures'], ['3', 'World and models'], ['15', 'Everything including UI']
          ], help: 'Chooses which shader paths Texture Detail is allowed to reduce.' },
          { name: 'r_nomip', title: 'Worldspawn Textures Only', type: 'bool', restart: 'video' },
          { name: 'r_simpleMipMaps', title: 'Simple Mipmaps', type: 'bool', restart: 'video' },
          { name: 'r_intensity', title: 'Texture Intensity', type: 'range', min: 1, max: 8, step: 0.05, restart: 'video' },
          { name: 'r_mapOverBrightCap', title: 'Map Lighting Cap', type: 'range', min: 0, max: 255, step: 1, restart: 'video',
            help: '255 preserves retail brightness.' }
        ],
        right: [
          { name: 'r_lodbias', title: 'Model Detail', type: 'select', options: [
            ['-2', 'Ultra'], ['-1', 'Very high'], ['0', 'High'], ['1', 'Medium'], ['2', 'Low']
          ] },
          { name: 'r_lodCurveError', title: 'Curve Detail', type: 'range', min: 0, max: 8192, step: 50 },
          { name: 'r_subdivisions', title: 'Curve Subdivision', type: 'range', min: 1, max: 64, step: 1, restart: 'video',
            help: 'Higher values subdivide less.' },
          { name: 'r_detailtextures', title: 'Detail Textures', type: 'bool', restart: 'video' },
          { name: 'r_neatsky', title: 'Sharp Sky Textures', type: 'bool', restart: 'video' },
          { name: 'r_vbo', title: 'Vertex Buffer Objects', type: 'bool', restart: 'video',
            help: 'Caches static map geometry. Costs extra hunk memory.' },
          { name: 'r_mergeLightmaps', title: 'Merge Lightmaps', type: 'bool', restart: 'video' },
          { name: 'r_marksOnTriangleMeshes', title: 'Marks On Mesh Surfaces', type: 'bool' }
        ]
      },
      {
        title: 'Lighting And Shadows',
        left: [
          { name: 'r_dlightMode', title: 'Dynamic Light Quality', type: 'select', options: [
            ['0', 'Classic'], ['1', 'Per-pixel'], ['2', 'Per-pixel with models']
          ] },
          { name: 'r_dlightScale', title: 'Dynamic Light Radius', type: 'range', min: 0.1, max: 1, step: 0.05 },
          { name: 'r_dlightIntensity', title: 'Dynamic Light Intensity', type: 'range', min: 0.1, max: 1, step: 0.05 },
          { name: 'r_dlightShadows', title: 'Dynamic Light Shadows', type: 'bool', restart: 'video' },
          { name: 'r_dlightShadowFilter', title: 'Dynamic Shadow Filtering', type: 'select', options: [
            ['0', 'Hard'], ['1', 'PCF 2x2'], ['2', 'Poisson 4']
          ] },
          { name: 'r_dlightShadowResolution', title: 'Dynamic Shadow Resolution', type: 'select', restart: 'video', options: [
            ['64', '64'], ['128', '128'], ['256', '256'], ['512', '512'], ['1024', '1024']
          ] },
          { name: 'r_dlightShadowMaxLights', title: 'Shadowing Dynamic Lights', type: 'range', min: 0, max: 32, step: 1, restart: 'video' },
          { name: 'r_dlightShadowStrength', title: 'Dynamic Shadow Darkness', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_muzzleFlashDlightShadows', title: 'Muzzle Flash Shadows', type: 'bool' }
        ],
        right: [
          { name: 'r_csmShadows', title: 'Sun Shadows', type: 'bool', restart: 'video',
            help: 'Cascaded shadow maps driven by the map sky sun.' },
          { name: 'r_csmCascadeCount', title: 'Sun Shadow Cascades', type: 'range', min: 1, max: 4, step: 1, restart: 'video' },
          { name: 'r_csmResolution', title: 'Sun Shadow Resolution', type: 'select', restart: 'video', options: [
            ['128', '128'], ['256', '256'], ['512', '512'], ['1024', '1024'], ['2048', '2048'], ['4096', '4096']
          ] },
          { name: 'r_csmMaxDistance', title: 'Sun Shadow Distance', type: 'range', min: 512, max: 8192, step: 128 },
          { name: 'r_csmSplitLambda', title: 'Sun Shadow Cascade Split', type: 'range', min: 0, max: 1, step: 0.05,
            help: 'Higher values pack more resolution into the near cascades.' },
          { name: 'r_csmShadowFilter', title: 'Sun Shadow Filtering', type: 'select', options: [
            ['0', 'Hard'], ['1', 'PCF 2x2'], ['2', 'Poisson 4']
          ] },
          { name: 'r_csmShadowStrength', title: 'Sun Shadow Darkness', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_spotShadows', title: 'Spot Light Shadows', type: 'bool', restart: 'video' },
          { name: 'r_staticLights', title: 'Static Map Lights', type: 'bool',
            help: 'Loads optional maps/<map>.lights.json sidecars.' },
          { name: 'r_staticLightShadows', title: 'Static Light Shadows', type: 'bool' },
          { name: 'r_surfaceLightProxies', title: 'Surface Light Proxies', type: 'bool',
            help: 'Promotes q3map_surfaceLight world surfaces into renderer-only lights.' }
        ]
      },
      {
        title: 'Color And Tone',
        left: [
          { name: 'r_hdr', title: 'High Dynamic Range Pipeline', type: 'bool', restart: 'video',
            help: 'High-precision scene-linear rendering. Requires framebuffer rendering.' },
          { name: 'r_hdrPrecision', title: 'Framebuffer Precision', type: 'select', options: [
            ['0', 'Automatic'], ['8', '8 bit'], ['16', '16 bit']
          ] },
          { name: 'r_tonemap', title: 'Tone Mapping', type: 'select', options: [
            ['0', 'Legacy gamma'], ['1', 'Reinhard'], ['2', 'ACES filmic']
          ] },
          { name: 'r_tonemapExposure', title: 'Exposure', type: 'range', min: 0.1, max: 8, step: 0.1 },
          { name: 'r_srgbTextures', title: 'sRGB Textures', type: 'bool', restart: 'video' }
        ],
        right: [
          { name: 'r_colorGrade', title: 'Color Grading', type: 'select', options: [
            ['0', 'Off'], ['1', 'Lift, gamma, gain'], ['2', '3D LUT'], ['3', 'Both']
          ], help: 'Requires the high dynamic range pipeline.' },
          { name: 'r_colorGradeLift', title: 'Grading Lift', type: 'text', help: 'R G B offset applied to shadows.' },
          { name: 'r_colorGradeGamma', title: 'Grading Gamma', type: 'text', help: 'R G B midtone curve.' },
          { name: 'r_colorGradeGain', title: 'Grading Gain', type: 'text', help: 'R G B highlight multiplier.' },
          { name: 'r_colorGradeWhitePoint', title: 'Grading White Point', type: 'range', min: 1000, max: 40000, step: 100,
            help: 'Kelvin.' },
          { name: 'r_colorGradeLUT', title: 'Color Grading LUT', type: 'text', help: 'LUT atlas image path.' },
          { name: 'r_colorGradeLUTScale', title: 'Color Grading LUT Size', type: 'range', min: 1, max: 32, step: 1 },
          { name: 'r_greyscale', title: 'Desaturate Frame', type: 'range', min: -1, max: 1, step: 0.05,
            help: 'Requires framebuffer rendering.' },
          { name: 'r_mapGreyScale', title: 'Desaturate World Textures', type: 'range', min: -1, max: 1, step: 0.05, restart: 'video',
            help: 'Negative values desaturate lightmaps only.' }
        ]
      },
      {
        title: 'Bloom',
        left: [
          { name: 'r_bloom', title: 'Bloom', type: 'bool',
            help: 'Requires framebuffer rendering. GLx applies it on the next frame; Vulkan and RTX need a video restart.' },
          { name: 'r_bloom_intensity', title: 'Bloom Intensity', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 'r_bloom_threshold', title: 'Bloom Threshold', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 'r_bloom_threshold_mode', title: 'Bloom Extraction', type: 'select', options: [
            ['0', 'Brightest channel'], ['1', 'Average'], ['2', 'Luma']
          ] },
          { name: 'r_bloom_soft_knee', title: 'Bloom Soft Knee', type: 'range', min: 0, max: 1, step: 0.05 }
        ],
        right: [
          { name: 'r_bloom_modulate', title: 'Bloom Modulation', type: 'select', options: [
            ['0', 'Off'], ['1', 'By itself'], ['2', 'By intensity']
          ] },
          { name: 'r_bloom_passes', title: 'Bloom Passes', type: 'range', min: 3, max: 8, step: 1 },
          { name: 'r_bloom_blend_base', title: 'Bloom Base Pass', type: 'range', min: 0, max: 7, step: 1,
            help: 'Must stay below Bloom Passes. Higher values produce a wider, hazier and weaker bloom.' },
          { name: 'r_bloom_filter_size', title: 'Bloom Filter Size', type: 'range', min: 1, max: 20, step: 1 },
          { name: 'r_bloom_reflection', title: 'Bloom Lens Reflection', type: 'range', min: -4, max: 4, step: 0.1 }
        ]
      },
      {
        title: 'Scene Effects',
        left: [
          { name: 'r_depthFade', title: 'Soft Particles', type: 'bool', restart: 'video',
            help: 'Softens where translucent particles meet world geometry.' },
          { name: 'r_flares', title: 'Light Flares', type: 'select', options: [
            ['0', 'Off'], ['1', 'Classic corona'], ['2', 'Corona and lens artifacts']
          ] },
          { name: 'r_flareSize', title: 'Flare Size', type: 'range', min: 1, max: 40, step: 1 },
          { name: 'r_motionBlur', title: 'Camera Motion Blur', type: 'bool',
            help: 'Requires framebuffer rendering. HUD and console stay sharp.' },
          { name: 'r_motionBlurStrength', title: 'Motion Blur Strength', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_crt', title: 'CRT Emulation', type: 'bool', help: 'Requires framebuffer rendering.' },
          { name: 'r_crtAmount', title: 'CRT Amount', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_crtScanlineStrength', title: 'CRT Scanlines', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_crtMaskStrength', title: 'CRT Shadow Mask', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_crtCurvature', title: 'CRT Curvature', type: 'range', min: 0, max: 0.25, step: 0.005 },
          { name: 'r_crtChromatic', title: 'CRT Chromatic Aberration', type: 'range', min: 0, max: 8, step: 0.05 }
        ],
        right: [
          { name: 'r_globalFog', title: 'Map Fog Sidecars', type: 'bool', restart: 'video',
            help: 'Enables optional maps/<map>.fog visuals. Requires framebuffer rendering.' },
          { name: 'r_globalFogStrength', title: 'Map Fog Strength', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_liquid', title: 'Enhanced Liquids', type: 'select', restart: 'video', options: [
            ['0', 'Off'], ['1', 'Water'], ['2', 'Water, slime and lava']
          ], help: 'Refraction and screen-space reflection. Requires framebuffer rendering.' },
          { name: 'r_liquidResolution', title: 'Liquid Resolution', type: 'range', min: 0.25, max: 1, step: 0.05, restart: 'video' },
          { name: 'r_liquidRefraction', title: 'Liquid Refraction', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_liquidReflection', title: 'Liquid Reflection', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_liquidWarpScale', title: 'Liquid Wave Scale', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 'r_liquidRipples', title: 'Liquid Ripples', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 'r_underwater', title: 'Underwater View', type: 'bool', restart: 'video',
            help: 'Warp, colour separation, edge darkening and distance tint while submerged. Requires framebuffer rendering.' },
          { name: 'r_underwaterWarp', title: 'Underwater Warp', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 'r_underwaterDispersion', title: 'Underwater Dispersion', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_underwaterFog', title: 'Underwater Tint', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_underwaterVignette', title: 'Underwater Edge Falloff', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_teleporterFlash', title: 'White Teleporter Flash', type: 'bool' }
        ]
      },
      {
        title: 'Cel Shading',
        left: [
          { name: 'r_celShading', title: 'Model Cel Shading', type: 'bool' },
          { name: 'r_celShadingSteps', title: 'Cel Lighting Bands', type: 'range', min: 2, max: 8, step: 1 },
          { name: 'r_celShadingModelShadows', title: 'Cel Shadow Banding', type: 'bool' },
          { name: 'r_celViewWeapon', title: 'Cel Shade View Weapon', type: 'bool' },
          { name: 'r_celShadingWorld', title: 'World Cel Outlines', type: 'bool' },
          { name: 'r_celShadingWorldWidth', title: 'World Outline Width', type: 'range', min: 1, max: 8, step: 0.1 },
          { name: 'r_celShadingWorldAlpha', title: 'World Outline Opacity', type: 'range', min: 0, max: 1, step: 0.05 }
        ],
        right: [
          { name: 'r_celOutline', title: 'Model Cel Outlines', type: 'bool', help: 'Requires a stencil buffer.' },
          { name: 'r_celOutlineScale', title: 'Model Outline Thickness', type: 'range', min: 1, max: 1.25, step: 0.005 },
          { name: 'r_celOutlineAlpha', title: 'Model Outline Opacity', type: 'range', min: 0, max: 1, step: 0.05 },
          { name: 'r_celOutlineColor', title: 'Cel Outline Color', type: 'text', help: 'R G B A from 0 to 255.' },
          { name: 'r_celViewWeaponOutlineScale', title: 'View Weapon Outline Thickness', type: 'range', min: 1, max: 1.1, step: 0.002 },
          { name: 'r_celViewWeaponOutlineAlpha', title: 'View Weapon Outline Opacity', type: 'range', min: 0, max: 1, step: 0.05 }
        ]
      }
    ],
    sound: [
      {
        title: 'Audio Backend',
        left: [
          { name: 's_backend', title: 'Sound Backend', type: 'select', restart: 'sound', options: [
            ['openal', 'OpenAL spatial audio'], ['legacy', 'Legacy software mixer']
          ] },
          { name: 's_alDevice', title: 'OpenAL Device', type: 'text', restart: 'sound',
            help: 'Leave blank for the system default.' },
          { name: 's_alOutputMode', title: 'Output Mode', type: 'select', restart: 'sound', options: [
            ['auto', 'Automatic'], ['headphones', 'Headphones'], ['speakers', 'Speakers'],
            ['surround', 'Surround'], ['quad', 'Quad'], ['5.1', '5.1'], ['6.1', '6.1'], ['7.1', '7.1']
          ] },
          { name: 's_alHrtf', title: 'Headphone HRTF', type: 'select', restart: 'sound', options: [
            ['auto', 'Automatic'], ['on', 'On'], ['off', 'Off']
          ] },
          { name: 's_alOutputLimiter', title: 'Output Limiter', type: 'bool', restart: 'sound' },
          { name: 's_khz', title: 'Mixer Sample Rate', type: 'select', restart: 'sound', options: [
            ['8', '8 kHz'], ['11', '11 kHz'], ['22', '22 kHz'], ['44', '44 kHz'], ['48', '48 kHz']
          ] }
        ],
        right: [
          { name: 's_alFrequency', title: 'Mix Frequency', type: 'select', restart: 'sound', options: [
            ['22050', '22050 Hz'], ['44100', '44100 Hz'], ['48000', '48000 Hz'], ['96000', '96000 Hz']
          ] },
          { name: 's_alMonoSources', title: 'Positional Voices', type: 'range', min: 16, max: 256, step: 8, restart: 'sound' },
          { name: 's_alStereoSources', title: 'Stereo Voices', type: 'range', min: 0, max: 64, step: 1, restart: 'sound' },
          { name: 's_mixAhead', title: 'Mixer Latency', type: 'range', min: 0.01, max: 0.5, step: 0.01,
            help: 'Seconds pre-mixed ahead by the legacy mixer.' }
        ]
      },
      {
        title: 'Spatial Audio',
        left: [
          { name: 's_alSpatializeStereo', title: 'Spatialize Stereo Sounds', type: 'bool', restart: 'sound' },
          { name: 's_alDistanceModel', title: 'Distance Model', type: 'select', restart: 'sound', options: [
            ['inverse_clamped', 'Inverse clamped'], ['inverse', 'Inverse'],
            ['linear_clamped', 'Linear clamped'], ['linear', 'Linear'],
            ['exponent_clamped', 'Exponent clamped'], ['exponent', 'Exponent'], ['none', 'None']
          ] },
          { name: 's_alReverb', title: 'Environmental Reverb', type: 'bool', restart: 'sound' },
          { name: 's_alReverbGain', title: 'Reverb Amount', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 's_alAudioZones', title: 'Map Audio Zones', type: 'bool',
            help: 'Enables optional maps/<map>.azb sidecars.' }
        ],
        right: [
          { name: 's_alOcclusion', title: 'Geometry Occlusion', type: 'bool' },
          { name: 's_alOcclusionStrength', title: 'Occlusion Strength', type: 'range', min: 0, max: 2, step: 0.05 },
          { name: 's_alAirAbsorption', title: 'Air Absorption', type: 'range', min: 0, max: 10, step: 0.25,
            help: '1 is physically neutral. Higher values darken distant sounds.' },
          { name: 's_alDopplerFactor', title: 'Doppler Amount', type: 'range', min: 0, max: 10, step: 0.25,
            help: 'Applies while Doppler Sound is on.' },
          { name: 's_alDopplerSpeed', title: 'Speed Of Sound', type: 'range', min: 1000, max: 20000, step: 250 },
          { name: 's_pvs', title: 'Cull Unheard Sounds', type: 'bool',
            help: 'Drops world sounds outside the listener visibility set.' }
        ]
      }
    ]
  };

  var SECTION_ROUTES = {
    'mouse settings': 'binds',
    'gamepad': 'gamepad',
    'basic': 'basic',
    'game': 'game',
    'hud': 'hud',
    'team': 'team',
    'weapons': 'weapons',
    'video': 'video',
    'sound': 'sound',
    'spectating': 'spectating'
  };

  var RESTART_ACTIONS = {
    video: { command: 'vid_restart', label: 'Apply Video Settings', tag: 'video restart' },
    sound: { command: 'snd_restart', label: 'Apply Sound Settings', tag: 'sound restart' }
  };

  var currentRoute = null;
  var currentSignature = '';
  var openSelect = null;
  var refreshTimer = null;
  var observer = null;

  function qz() {
    return window.qz_instance && typeof window.qz_instance.GetConfig === 'function'
      ? window.qz_instance : null;
  }

  function cvarCache() {
    var bridge = qz();
    var config = bridge ? bridge.GetConfig() : null;
    return config && config.cvars ? config.cvars : {};
  }

  function hasCvar(cache, name) {
    return Object.prototype.hasOwnProperty.call(cache, String(name).toLowerCase());
  }

  function cvarValue(cache, name) {
    var key = String(name).toLowerCase();
    return Object.prototype.hasOwnProperty.call(cache, key) ? String(cache[key]) : null;
  }

  function setCvar(name, value) {
    var bridge = qz();
    if (bridge) {
      bridge.SetCvar(name, String(value));
    }
  }

  function resetCvar(name) {
    var bridge = qz();
    if (bridge) {
      bridge.ResetCvar(name);
      if (typeof bridge.GetCvar === 'function') {
        bridge.GetCvar(name);
      }
    }
  }

  function sendCommand(command) {
    var bridge = qz();
    if (bridge) {
      bridge.SendGameCommand(command + '\n');
    }
  }

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) {
      node.className = className;
    }
    if (typeof text !== 'undefined' && text !== null) {
      node.appendChild(document.createTextNode(text));
    }
    return node;
  }

  function text(node) {
    return String(node && (node.textContent || node.innerText) || '')
      .replace(/^\s+|\s+$/g, '')
      .toLowerCase();
  }

  function decimalsOf(step) {
    var value = String(typeof step === 'undefined' ? 1 : step);
    var dot = value.indexOf('.');
    return dot === -1 ? 0 : value.length - dot - 1;
  }

  function formatRange(value, step) {
    var parsed = parseFloat(value);
    if (isNaN(parsed)) {
      parsed = 0;
    }
    return parsed.toFixed(decimalsOf(step));
  }

  function optionLabel(setting, value) {
    var index;
    for (index = 0; index < setting.options.length; index += 1) {
      if (setting.options[index][0] === value) {
        return setting.options[index][1];
      }
    }
    return value === '' ? 'Default' : value;
  }

  function closeOpenSelect() {
    if (!openSelect) {
      return;
    }
    if (openSelect.menu && openSelect.menu.parentNode) {
      openSelect.menu.parentNode.removeChild(openSelect.menu);
    }
    openSelect.root.className = openSelect.baseClass;
    openSelect = null;
  }

  // react-select renders the retail dropdowns. Awesomium's offscreen view does
  // not composite native select popups, so the retail markup and stylesheet are
  // reproduced here instead of using a <select> element.
  function makeSelectWidget(setting, value) {
    var root = el('div', 'Select');
    var control = el('div', 'Select-control');
    var label = el('div', 'Select-placeholder', optionLabel(setting, value));
    var inputStub = el('div', 'Select-input');
    var menu = null;

    inputStub.setAttribute('tabindex', '0');
    inputStub.appendChild(document.createTextNode('\u00a0'));
    control.appendChild(label);
    control.appendChild(inputStub);
    control.appendChild(el('span', 'Select-arrow-zone'));
    control.appendChild(el('span', 'Select-arrow'));
    root.appendChild(control);

    function baseClass(selected) {
      return selected === '' ? 'Select' : 'Select has-value';
    }

    root.className = baseClass(value);

    function buildMenu(selected) {
      var outer = el('div', 'Select-menu-outer');
      var list = el('div', 'Select-menu');
      var index;
      for (index = 0; index < setting.options.length; index += 1) {
        (function (option) {
          var item = el('div', option[0] === selected ? 'Select-option is-selected' : 'Select-option', option[1]);
          item.onmousedown = function (event) {
            if (event && event.preventDefault) {
              event.preventDefault();
            }
            closeOpenSelect();
            root.__fnqlValue = option[0];
            label.firstChild.nodeValue = option[1];
            root.className = baseClass(option[0]);
            setCvar(setting.name, option[0]);
          };
          list.appendChild(item);
        }(setting.options[index]));
      }
      outer.appendChild(list);
      return outer;
    }

    control.onmousedown = function (event) {
      var wasOpen = openSelect && openSelect.root === root;
      if (event && event.preventDefault) {
        event.preventDefault();
      }
      closeOpenSelect();
      if (wasOpen) {
        return;
      }
      menu = buildMenu(root.__fnqlValue);
      root.appendChild(menu);
      root.className = baseClass(root.__fnqlValue) + ' is-open is-focused';
      openSelect = { root: root, menu: menu, baseClass: baseClass(root.__fnqlValue) };
    };

    root.__fnqlValue = value;
    root.__fnqlSync = function (next) {
      if (openSelect && openSelect.root === root) {
        return;
      }
      root.__fnqlValue = next;
      label.firstChild.nodeValue = optionLabel(setting, next);
      root.className = baseClass(next);
    };
    return root;
  }

  function makeBoolWidget(setting, value) {
    var control = document.createElement('input');
    control.type = 'checkbox';
    control.checked = parseFloat(value) !== 0;
    control.onchange = function () {
      setCvar(setting.name, control.checked ? '1' : '0');
    };
    control.__fnqlSync = function (next) {
      control.checked = parseFloat(next) !== 0;
    };
    return control;
  }

  function makeRangeWidget(setting, value) {
    var wrapper = el('div', 'range');
    var output = el('output', 'input-val', formatRange(value, setting.step));
    var control = document.createElement('input');
    var dragging = false;

    control.type = 'range';
    control.min = setting.min;
    control.max = setting.max;
    control.step = setting.step;
    control.value = formatRange(value, setting.step);
    control.setAttribute('id', setting.name);
    output.setAttribute('for', setting.name);

    control.oninput = function () {
      dragging = true;
      output.firstChild.nodeValue = formatRange(control.value, setting.step);
    };
    control.onchange = function () {
      dragging = false;
      output.firstChild.nodeValue = formatRange(control.value, setting.step);
      setCvar(setting.name, formatRange(control.value, setting.step));
    };
    control.onmouseup = control.onchange;

    wrapper.appendChild(output);
    wrapper.appendChild(control);
    wrapper.__fnqlSync = function (next) {
      if (dragging || document.activeElement === control) {
        return;
      }
      control.value = formatRange(next, setting.step);
      output.firstChild.nodeValue = formatRange(next, setting.step);
    };
    return wrapper;
  }

  function makeTextWidget(setting, value) {
    var control = document.createElement('input');
    control.type = 'text';
    control.maxLength = 200;
    control.value = value;
    control.onchange = function () {
      setCvar(setting.name, control.value);
    };
    control.onblur = function () {
      setCvar(setting.name, control.value);
    };
    control.__fnqlSync = function (next) {
      if (document.activeElement !== control) {
        control.value = next;
      }
    };
    return control;
  }

  function makeWidget(setting, value) {
    if (setting.type === 'select') {
      return makeSelectWidget(setting, value);
    }
    if (setting.type === 'bool') {
      return makeBoolWidget(setting, value);
    }
    if (setting.type === 'range') {
      return makeRangeWidget(setting, value);
    }
    return makeTextWidget(setting, value);
  }

  function makeRow(setting, value) {
    var row = el('div', 'row cvar ' + setting.type + ' fnql-cvar');
    var children = el('div', 'children seven columns');
    var val = el('div', 'val five columns');
    var reset = el('a', 'resetLink', '(Reset)');
    var restart = RESTART_ACTIONS[setting.restart];
    var help = setting.help;
    var widget = makeWidget(setting, value);

    children.appendChild(document.createTextNode(setting.title + ' '));
    children.appendChild(el('span', 'fnql-cvar-name', setting.name));
    if (setting.restart && restart) {
      // A compact tag rather than a sentence: whole groups of rows share the
      // same restart requirement and repeating it in prose buries the help.
      children.appendChild(document.createTextNode(' '));
      children.appendChild(el('span', 'fnql-cvar-flag', restart.tag));
    }
    children.appendChild(document.createTextNode(' '));
    reset.href = '#';
    reset.onclick = function (event) {
      if (event && event.preventDefault) {
        event.preventDefault();
      }
      resetCvar(setting.name);
      return false;
    };
    children.appendChild(reset);
    if (help) {
      children.appendChild(el('div', 'fnql-cvar-help', help));
    }

    val.appendChild(widget);
    row.appendChild(children);
    row.appendChild(val);
    row.__fnqlCvar = setting.name;
    row.__fnqlSync = widget.__fnqlSync;
    return row;
  }

  function appendRows(container, settings, cache) {
    var index;
    var appended = 0;
    for (index = 0; index < settings.length; index += 1) {
      var value = cvarValue(cache, settings[index].name);
      if (value === null) {
        continue;
      }
      container.appendChild(makeRow(settings[index], value));
      appended += 1;
    }
    return appended;
  }

  function makeGroup(group, cache) {
    var fragment = el('div', 'fnql-group');
    var layout = el('div', 'row');
    var left = el('div', 'one-half column');
    var right = el('div', 'one-half column');
    var count = 0;

    count += appendRows(left, group.left || [], cache);
    count += appendRows(right, group.right || [], cache);
    if (!count) {
      return null;
    }

    fragment.appendChild(el('h1', null, group.title));
    layout.appendChild(left);
    layout.appendChild(right);
    fragment.appendChild(layout);
    return fragment;
  }

  // Only the heading's own text. The section note is appended into the same
  // heading, and counting it would make the route unrecognisable on the pass
  // right after the note is added.
  function ownText(node) {
    var parts = [];
    var child = node ? node.firstChild : null;
    while (child) {
      if (child.nodeType === 3) {
        parts.push(child.nodeValue);
      }
      child = child.nextSibling;
    }
    return parts.join('').replace(/^\s+|\s+$/g, '').toLowerCase();
  }

  function sectionRoute(section) {
    var heading = section.querySelector('h1');
    return heading ? SECTION_ROUTES[ownText(heading)] || null : null;
  }

  function hasClass(node, name) {
    return (' ' + String(node && node.className || '') + ' ').indexOf(' ' + name + ' ') !== -1;
  }

  function insideInjectedGroup(node, section) {
    while (node && node !== section) {
      if (hasClass(node, 'fnql-group')) {
        return true;
      }
      node = node.parentNode;
    }
    return false;
  }

  // The first retail two-column layout row of the section. Retail cvar rows
  // carry both "row" and "cvar", so only the bare layout row matches.
  function layoutRow(section) {
    var rows = section.getElementsByTagName('div');
    var index;
    for (index = 0; index < rows.length; index += 1) {
      if (!hasClass(rows[index], 'row') || hasClass(rows[index], 'cvar')) {
        continue;
      }
      if (insideInjectedGroup(rows[index], section)) {
        continue;
      }
      return rows[index];
    }
    return null;
  }

  function layoutColumn(section, index) {
    var row = layoutRow(section);
    if (!row) {
      return null;
    }
    var columns = row.children;
    return columns && columns.length > index ? columns[index] : null;
  }

  function rowCvarName(row) {
    var ranged = row.querySelector('input[type="range"]');
    var output = row.querySelector('output[for]');
    if (ranged && ranged.id) {
      return String(ranged.id).toLowerCase();
    }
    if (output) {
      return String(output.getAttribute('for') || '').toLowerCase();
    }
    return '';
  }

  function markRetailRow(row, hidden) {
    var className = String(row.className || '');
    var marked = className.indexOf('fnql-retail-hidden') !== -1;
    if (hidden && !marked) {
      row.className = className + ' fnql-retail-hidden';
    } else if (!hidden && marked) {
      row.className = className.replace(/\s*fnql-retail-hidden/g, '');
    }
  }

  function applyRetailPolicy(section, route) {
    var unsupported = RETAIL_UNSUPPORTED[route] || [];
    var rows = section.querySelectorAll('.cvar');
    var index;
    var entry;

    for (index = 0; index < rows.length; index += 1) {
      var row = rows[index];
      if (String(row.className || '').indexOf('fnql-cvar') !== -1) {
        continue;
      }
      var label = text(row.querySelector('.children')).replace(/\(reset\)$/, '')
        .replace(/^\s+|\s+$/g, '');
      var name = rowCvarName(row);
      var hidden = false;
      for (entry = 0; entry < unsupported.length; entry += 1) {
        if ((name && name === unsupported[entry][0])
          || label.indexOf(unsupported[entry][1]) === 0) {
          hidden = true;
          break;
        }
      }
      markRetailRow(row, hidden);
    }
  }

  // The retail Video page's second column is entirely the legacy post-process
  // pipeline, which FnQL does not implement. Its rows are removed from view and
  // the column is reused for FnQL's own renderer controls.
  function hideRetailColumnRows(column) {
    var rows = column.children;
    var index;
    for (index = 0; index < rows.length; index += 1) {
      var className = String(rows[index].className || '');
      if (className.indexOf('cvar') !== -1 && className.indexOf('fnql-cvar') === -1) {
        markRetailRow(rows[index], true);
      }
    }
  }

  function sectionNote(section, route) {
    var note = SECTION_NOTES[route];
    var heading;
    if (!note || section.querySelector('.fnql-note')) {
      return;
    }
    heading = section.querySelector('h1');
    if (!heading || !heading.parentNode) {
      return;
    }
    // Appended at the tail of the heading, never inserted between React's own
    // children, so React's index-based child updates stay correct.
    heading.appendChild(el('span', 'fnql-note', note));
  }

  function markRetailActions(section, hidden) {
    var paragraphs = section.getElementsByTagName('p');
    var index;
    for (index = 0; index < paragraphs.length; index += 1) {
      var className = String(paragraphs[index].className || '');
      if (className.indexOf('button-row') === -1 || className.indexOf('fnql') !== -1) {
        continue;
      }
      var marked = className.indexOf('fnql-retail-hidden') !== -1;
      if (hidden && !marked) {
        paragraphs[index].className = className + ' fnql-retail-hidden';
      } else if (!hidden && marked) {
        paragraphs[index].className = className.replace(/\s*fnql-retail-hidden/g, '');
      }
    }
  }

  function appendActions(section, route) {
    var action = RESTART_ACTIONS[route];
    var actions;
    var button;
    if (!action || section.querySelector('.fnql-actions')) {
      return;
    }
    actions = el('p', 'button-row fnql-actions');
    button = el('button', null, action.label);
    button.type = 'button';
    button.onclick = function () {
      sendCommand(action.command);
    };
    actions.appendChild(button);
    section.appendChild(actions);
  }

  function removeInjected(root) {
    var injected = root.querySelectorAll('.fnql-group, .fnql-actions, .fnql-cvar, .fnql-note');
    var hidden = root.querySelectorAll('.fnql-retail-hidden');
    var index;
    closeOpenSelect();
    for (index = 0; index < injected.length; index += 1) {
      if (injected[index].parentNode) {
        injected[index].parentNode.removeChild(injected[index]);
      }
    }
    for (index = 0; index < hidden.length; index += 1) {
      hidden[index].className = String(hidden[index].className)
        .replace(/\s*fnql-retail-hidden/g, '');
    }
  }

  function injectColumns(section, route, cache) {
    var injections = COLUMN_ROWS[route] || [];
    var index;
    for (index = 0; index < injections.length; index += 1) {
      var column = layoutColumn(section, injections[index].column);
      if (!column) {
        continue;
      }
      if (injections[index].replacesRetailColumn) {
        hideRetailColumnRows(column);
      }
      if (!column.querySelector('.fnql-cvar')) {
        appendRows(column, injections[index].settings, cache);
      }
    }
  }

  function routeSettings(route) {
    var injections = COLUMN_ROWS[route] || [];
    var definitions = SECTION_GROUPS[route] || [];
    var settings = [];
    var index;
    for (index = 0; index < injections.length; index += 1) {
      settings = settings.concat(injections[index].settings);
    }
    for (index = 0; index < definitions.length; index += 1) {
      settings = settings.concat(definitions[index].left || [], definitions[index].right || []);
    }
    return settings;
  }

  // Which of the route's cvars the engine currently publishes. Renderer and
  // sound cvars come and go with the active backend, so the injected content is
  // rebuilt whenever that set changes instead of being patched in place.
  function routeSignature(route, cache) {
    var settings = routeSettings(route);
    var present = [];
    var index;
    for (index = 0; index < settings.length; index += 1) {
      if (hasCvar(cache, settings[index].name)) {
        present.push(settings[index].name);
      }
    }
    return present.join(' ');
  }

  function injectGroups(section, route, cache) {
    var definitions = SECTION_GROUPS[route] || [];
    var existing = section.querySelectorAll('.fnql-group');
    var index;
    if (!definitions.length || existing.length) {
      return;
    }
    for (index = 0; index < definitions.length; index += 1) {
      var group = makeGroup(definitions[index], cache);
      if (group) {
        // Always appended, so React's own children keep the leading indices its
        // reconciler assumes when it inserts or moves nodes.
        section.appendChild(group);
      }
    }
  }

  function syncValues(root, cache) {
    var rows = root.querySelectorAll('.fnql-cvar');
    var index;
    for (index = 0; index < rows.length; index += 1) {
      var row = rows[index];
      var value = cvarValue(cache, row.__fnqlCvar);
      if (value !== null && row.__fnqlSync) {
        row.__fnqlSync(value);
      }
    }
  }

  function attach() {
    var root = document.querySelector('.game-settings');
    var section;
    var route;
    var cache;
    var signature;

    if (!root) {
      currentRoute = null;
      currentSignature = '';
      closeOpenSelect();
      return;
    }

    section = root.querySelector('section');
    if (!section) {
      return;
    }

    route = sectionRoute(section);
    cache = cvarCache();
    signature = route ? routeSignature(route, cache) : '';
    if (route !== currentRoute || signature !== currentSignature) {
      removeInjected(root);
      currentRoute = route;
      currentSignature = signature;
    }
    if (!route) {
      return;
    }

    applyRetailPolicy(section, route);
    sectionNote(section, route);
    injectColumns(section, route, cache);
    injectGroups(section, route, cache);
    if (SECTION_GROUPS[route] && SECTION_GROUPS[route].length) {
      markRetailActions(section, !!RESTART_ACTIONS[route]);
      appendActions(section, route);
    }
    syncValues(root, cache);
  }

  document.addEventListener('mousedown', function (event) {
    var node = event.target;
    while (node && node !== document) {
      if (String(node.className || '').indexOf('Select-control') !== -1) {
        return;
      }
      node = node.parentNode;
    }
    closeOpenSelect();
  }, false);

  function start() {
    attach();
    if (window.MutationObserver) {
      observer = new MutationObserver(attach);
      observer.observe(document.body, { childList: true, subtree: true });
    }
    window.addEventListener('hashchange', function () {
      currentRoute = null;
      currentSignature = '';
      window.setTimeout(attach, 0);
    }, false);
    // Awesomium's Chromium build does not reliably deliver MutationObserver
    // callbacks for every React route replacement, and the engine pushes cvar
    // values asynchronously. Keep a cheap periodic pass so rows appear after
    // navigation and stay in step with the engine.
    refreshTimer = window.setInterval(attach, 500);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start, false);
  } else {
    start();
  }
}());
