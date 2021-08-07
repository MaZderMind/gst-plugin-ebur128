#!/usr/bin/env python3

import gi

gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib

Gst.init(None)

pipeline = Gst.parse_launch("""
    filesrc location=examples/music.mp3 ! mpegaudioparse ! mpg123audiodec ! \
        ebur128 name=ebur128 momentary=true shortterm=true global=true window=5000 range=true sample-peak=true true-peak=true ! \
        autoaudiosink
""")

element = pipeline.get_by_name("ebur128")

def on_message(bus, message):
    if message.type == Gst.MessageType.ELEMENT and message.src == element:
        structure = message.get_structure()

        success, timestamp = structure.get_uint64("timestamp")
        success, momentary_loudness = structure.get_double("momentary")
        success, shortterm_loudness = structure.get_double("shortterm")
        success, global_loudness = structure.get_double("global")
        success, window_loudness = structure.get_double("window")
        success, loudness_range = structure.get_double("range")
        true_peak = structure.get_value("true-peak")
        sample_peak = structure.get_value("true-peak")

        print({
            "timestamp": timestamp,
            "momentary_loudness": momentary_loudness,
            "shortterm_loudness": shortterm_loudness,
            "global_loudness": global_loudness,
            "window_loudness": window_loudness,
            "loudness_range": loudness_range,
            "true_peak": true_peak,
            "sample_peak": sample_peak
        })

bus = pipeline.get_bus()
bus.add_signal_watch()
bus.connect("message", on_message)

pipeline.set_state(Gst.State.PLAYING)

mainloop = GLib.MainLoop()
mainloop.run()
