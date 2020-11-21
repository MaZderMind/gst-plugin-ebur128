project('gst-ebur128', 'c', version : '1.0.0.0', license : 'MIT')

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')

gst_version = meson.project_version()

api_version = '1.0'

gst_dep = dependency('gstreamer-1.0', fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', fallback : ['gstreamer', 'gst_base_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', fallback : ['gst-plugins-base', 'audio_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', fallback : ['gst-plugins-base', 'video_dep'])

libebur128_dep = dependency('libebur128')
cairo_dep = dependency('cairo')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-ebur128-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer ebur128 Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://mazdermind.de')
configure_file(output : 'config.h', configuration : cdata)

plugin_sources = [
  'src/gstebur128plugin.c',
  'src/gstebur128shared.c',
  'src/gstebur128element.c',
  'src/gstebur128graphelement.c',
  'src/gstebur128graphrender.c',
]

ebur128 = library('gstebur128',
  plugin_sources,
  c_args: plugin_c_args,
  dependencies : [
    gst_dep,
    gstbase_dep,
    gstaudio_dep,
    gstvideo_dep,

    libebur128_dep,
    cairo_dep,

    m_dep
  ],
  install : true,
  install_dir : plugins_install_dir,
)

if not get_option('tests').disabled()
  subdir('tests')
endif
