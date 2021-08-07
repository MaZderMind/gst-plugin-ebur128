# EBU-R 128 Plugin

This plugin contains two elements

* ebur128:
  Passes audio, emitting Events for ebur128 loudness (similar to the level-Elemenr)

* ebur128display:
  Visualizes EBU-R Levels over a period of time as a configurable Video-Stream

## License

This code is provided under a [MIT license](http://www.opensource.org/licenses/mit-license.php), which basically means "do
with it as you wish, but don't blame us if it doesn't work".

## Usage

Configure and build:

    # For Building
    apt install build-essential meson libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libebur128-dev libcairo-dev

    # For Testing
    apt install gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good

    # Build
    make

The Makefile is just a shortcut to meson & ninja. See the [Meson Quickstart Guide](https://mesonbuild.com/Quick-guide.html)
on how to install the Meson build system and ninja.

You can check if it has been built correctly with:

    make inspect

    make inspect-ebur128
    make inspect-ebur128graph

And Test it as with:

    make run-ebur128
    make run-ebur128graph

## Use in your own App
For an example on how to use the ebur128 Plugin in your own appl see [example.py](examples/example.py). To run it with the correct Plugin-Path, use

    make run-example-py

