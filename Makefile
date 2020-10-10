build: builddir
	ninja -C builddir

builddir:
	meson builddir

tests/builddir:
	cd tests && meson builddir

clean: builddir
	ninja -C builddir clean

format:
	clang-format -i src/*.[ch] tests/elements/*.[ch]

inspect:
	gst-inspect-1.0 builddir/libgstebur128.so

inspect-ebur128:
	GST_PLUGIN_PATH=$(realpath builddir) gst-inspect-1.0 ebur128

inspect-ebur128graph:
	GST_PLUGIN_PATH=$(realpath builddir) gst-inspect-1.0 ebur128graph

run-tests: builddir
	cd builddir && meson test -v

run-ebur128:
	GST_PLUGIN_PATH=$(realpath builddir) gst-launch-1.0 -m \
	  filesrc location=example-audio/music.mp3 ! mpegaudioparse ! mpg123audiodec ! \
      ebur128 momentary=true shortterm=true global=true window=5000 range=true sample-peak=true true-peak=true interval=100000000 ! \
	  autoaudiosink

run-ebur128-with-seek:
	GST_PLUGIN_PATH=$(realpath builddir) gst-launch-1.0 -m \
		filesrc location=example-audio/music.mp3 ! \
		mpegaudioparse ! mpg123audiodec ! \
		ebur128 ! \
		navseek ! \
		tee name=t \
		\
		t. ! queue ! audioconvert ! wavescope ! ximagesink \
		t. ! queue ! autoaudiosink

run-ebur128graph:
	GST_PLUGIN_PATH=$(realpath builddir) gst-launch-1.0 \
		filesrc location=example-audio/music.mp3 ! mpegaudioparse ! mpg123audiodec ! tee name=t \
		t. ! queue ! ebur128graph ! videoconvert ! ximagesink \
		t. ! queue ! autoaudiosink

.PHONY: build inspect inspect-ebur128 run-ebur128 format
