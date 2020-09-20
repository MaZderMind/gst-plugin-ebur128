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


DEBUG=0
#DEBUG=9

run-ebur128:
	GST_PLUGIN_PATH=$(realpath builddir) GST_DEBUG=ebur128:$(DEBUG) gst-launch-1.0 -m \
	  filesrc location=example-audio/music.mp3 ! mpegaudioparse ! mpg123audiodec ! \
      ebur128 momentary=true shortterm=true global=true window=5000 range=true sample-peak=true true-peak=true ! \
	  autoaudiosink

run-ebur128-with-seek:
	GST_PLUGIN_PATH=$(realpath builddir) GST_DEBUG=ebur128:$(DEBUG) gst-launch-1.0 -m \
		filesrc location=example-audio/music.mp3 ! \
		mpegaudioparse ! mpg123audiodec ! \
		ebur128 ! \
		navseek ! \
		tee name=t \
		\
		t. ! queue ! audioconvert ! wavescope ! ximagesink \
		t. ! queue ! autoaudiosink

run-ebur128display:
	GST_PLUGIN_PATH=$(realpath builddir) GST_DEBUG=ebur128:9 gst-launch-1.0 audiotestsrc ! \
	  audio/x-raw,format=S16LE,channels=2,rate=48000 ! \
	  ebur128display ! \
	  video/x-raw,format=RGBA,width=640,height=480,framerate=60/1 !
	  glimagesink



.PHONY: build inspect inspect-ebur128 run-ebur128 format
