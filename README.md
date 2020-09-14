# EBU-R 128 Plugin

This plugin contains two elements

* ebur128:
  Passes audio, emitting Events for ebur128 loudness (similar to the level-Elemenr)

* ebur128display:
  Visualizes EBU-R Levels over a period of time as a comfigurable Video-Stream

## License

This code is provided under a MIT license [MIT], which basically means "do
with it as you wish, but don't blame us if it doesn't work".

## Usage

Configure and build  as such:

    meson builddir
    ninja -C builddir

See <https://mesonbuild.com/Quick-guide.html> on how to install the Meson
build system and ninja.

You can check if it has been built correctly with:

    gst-inspect-1.0 builddir/gst-plugins/src/libgstplugin.so

    GST_PLUGIN_PATH=$(realpath builddir) gst-inspect-1.0 ebur128
    GST_PLUGIN_PATH=$(realpath builddir) gst-inspect-1.0 ebur128display

And Test it as with:

    GST_PLUGIN_PATH=$(realpath builddir) gst-launch-1.0 -m audiotestsrc ! \
      audio/x-raw,format=S16LE,channels=2,rate=48000 ! \
      ebur128 ! autoaudiosink

    GST_PLUGIN_PATH=$(realpath builddir) gst-launch-1.0 -m audiotestsrc ! \
      audio/x-raw,format=S16LE,channels=2,rate=48000 ! \
      ebur128display ! \
      video/x-raw,format=RGBA,width=640,height=480,framerate=60/1 !
      glimagesink

[MIT]: http://www.opensource.org/licenses/mit-license.php or COPYING.MIT
