build: builddir
	ninja -C builddir

builddir:
	meson builddir

clean:
	ninja -C builddir clean

format:
	clang-format -i src/**

inspect:
	gst-inspect-1.0 builddir/libgstebur128.so

inspect-ebur128:
	GST_PLUGIN_PATH=$(realpath builddir) gst-inspect-1.0 ebur128

inspect-ebur128display:
	GST_PLUGIN_PATH=$(realpath builddir) gst-inspect-1.0 ebur128display

run-ebur128:
	GST_PLUGIN_PATH=$(realpath builddir) GST_DEBUG=ebur128:9 gst-launch-1.0 audiotestsrc ! \
      audio/x-raw,format=S16LE,channels=2,rate=48000 ! \
      ebur128 ! autoaudiosink

run-ebur128display:
	GST_PLUGIN_PATH=$(realpath builddir) GST_DEBUG=ebur128:9 gst-launch-1.0 audiotestsrc ! \
	  audio/x-raw,format=S16LE,channels=2,rate=48000 ! \
	  ebur128display ! \
	  video/x-raw,format=RGBA,width=640,height=480,framerate=60/1 !
	  glimagesink



.PHONY: build inspect inspect-ebur128 run-ebur128 format
