gstcheck_dep = dependency('gstreamer-check-1.0', fallback : ['gstreamer', 'gst_check_dep'])

test_deps = [gst_dep, gstbase_dep, gstcheck_dep, gstaudio_dep]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  #'-DGST_TEST_FILES_PATH="' + meson.current_source_dir() + '/../files"',
  '-DGST_USE_UNSTABLE_API',
]

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required: true)
  pluginsdirs = [gst_dep.get_pkgconfig_variable('pluginsdir'),
                 pbase.get_pkgconfig_variable('pluginsdir')]
endif


tests = [
  # name, skip?, extra_deps, extra_sources
  [ 'elements/ebur128', false, [gst_dep, gstaudio_dep, libebur128_dep] ],
  [ 'elements/ebur128graph', false, [gst_dep, gstaudio_dep, libebur128_dep] ],
]


foreach t : tests
  fname = '@0@.c'.format(t.get(0))
  test_name = t.get(0).underscorify()
  skip_test = t.get(1, false)
  extra_deps = t.get(2, [ ])
  extra_sources = t.get(3, [ ])

  if not skip_test
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base', 'ebur128')
    env.set('GST_PLUGIN_PATH_1_0', [meson.build_root()] + pluginsdirs)
    env.set('GSETTINGS_BACKEND', 'memory')

    env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))

    exe = executable(test_name, fname, extra_sources,
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      dependencies : test_deps + extra_deps,
    )
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach
