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

Configure and build:

    make

TRhe Makefile is just a shortcut to meson & ninja. See <https://mesonbuild.com/Quick-guide.html>
on how to install the Meson build system and ninja.

You can check if it has been built correctly with:

    make inspect

    make inspect-ebur128
    make inspect-ebur128display

And Test it as with:

    make run-ebur128
    make run-ebur128display

[MIT]: http://www.opensource.org/licenses/mit-license.php or COPYING.MIT
