#!/usr/bin/env python3
import enum
import math

import cairo
import gi

import data

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk


class Scale(enum.Enum):
    ABSOLUT = 1
    RELATIVE = 2


COLOR_BACKGROUND = (0.1, 0.1, 0.1)
COLOR_HEADER_TEXT = (1, 1, 0)
COLOR_SCALE_TEXT = (0, .6, .6)
COLOR_BORDER = (0, .8, 0)
COLOR_SCALE_LINES = (1, 1, 1, .3)

COLOR_GRAPH = (0, 0, 0, .6)

COLOR_TOO_LOUD = (.86, .4, .4)
COLOR_LOUDNESS_OK = (.4, .86, .4)
COLOR_NOT_LOUD_ENOUGH = (.4, .4, .86)

FONT_SIZE = 12
SCALE_FONT_SIZE = 8

GUTTER = 5
SCALE_W = 20
GAUGE_W = 20

SCALE_FROM = +18
SCALE_TO = -36

TARGET = -23
SCALE = Scale.RELATIVE

TIMEFRAME = 10_000_000_000  # 10s
INTERVAL = 100_000_000  # 100ms


def main():
    w = 0
    h = 0

    def on_key(window, event):
        global w, h, SCALE
        if event.keyval in (Gdk.KEY_space, Gdk.KEY_Return,):
            Gtk.main_quit()
        elif event.keyval in (Gdk.KEY_y,):
            SCALE = Scale.RELATIVE if SCALE == Scale.ABSOLUT else Scale.ABSOLUT

            window.queue_draw()
        elif event.keyval in (Gdk.KEY_s,):
            with cairo.ImageSurface(cairo.Format.RGB24, w, h) as surface:
                ctx = cairo.Context(surface)
                draw(ctx, w, h)
                surface.write_to_png('save.png')
                print('saved to save.png')

    win = Gtk.Window()
    win.connect('destroy', lambda w: Gtk.main_quit())
    win.connect('key_press_event', on_key)
    win.set_default_size(640, 480)

    def draw_widget(drawing_area, ctx):
        global w, h
        w = drawing_area.get_allocated_width()
        h = drawing_area.get_allocated_height()
        win.set_title(f"{w}x{h}")
        draw(ctx, w, h)

    drawingarea = Gtk.DrawingArea()
    win.add(drawingarea)
    drawingarea.connect('draw', draw_widget)

    win.show_all()
    print('Press Enter or Space-Bar to Quit, s to save a png, y to toggle rel/abs mode')
    Gtk.main()


def draw(ctx, w, h):
    ctx.set_line_width(1.0)
    ctx.select_font_face('monospace')
    ctx.set_font_size(FONT_SIZE)
    (_, _, _, font_height, _, _) = ctx.text_extents('Z')

    current_data_point = data.DATA[len(data.DATA) - 1]

    # fill with bg-color
    ctx.set_source_rgb(*COLOR_BACKGROUND)
    ctx.rectangle(0, 0, w, h)
    ctx.fill()

    # header
    # header_w = w - GUTTER - GUTTER
    header_h = font_height
    header_x = GUTTER + SCALE_W + GUTTER + 2
    header_y = GUTTER

    ctx.set_source_rgb(*COLOR_HEADER_TEXT)
    ctx.move_to(header_x, header_y + font_height)
    correction = TARGET if SCALE == Scale.RELATIVE else 0
    unit = 'LUFS' if SCALE == Scale.ABSOLUT else 'LU'
    ctx.show_text(f"TARGET: {TARGET} LUFS | " +
                  f"M: {with_sign(round(current_data_point['momentary'] - correction, 2))} {unit} | "
                  f"S: {with_sign(round(current_data_point['shortterm'] - correction, 2))} {unit} | " +
                  f"I: {with_sign(round(current_data_point['global'] - correction, 2))} {unit} | "
                  f"LRA: {with_sign(round(current_data_point['range'], 2))} LU")
    ctx.fill()

    # scale
    scale_w = SCALE_W
    scale_h = h - header_h - GUTTER - GUTTER - GUTTER
    scale_x = GUTTER
    scale_y = GUTTER + header_h + GUTTER

    num_scales = SCALE_FROM + abs(SCALE_TO) + 1
    distance = scale_h / (num_scales + 1)
    print("scale.h=",scale_h," scale_spacing=",distance);

    ctx.select_font_face('monospace', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD)
    ctx.set_font_size(SCALE_FONT_SIZE)
    ctx.set_source_rgb(*COLOR_SCALE_TEXT)

    extends = ctx.text_extents("0")
    show_every = int(max(math.ceil(1 / (distance / extends.height)), 1))

    for scale_index in range(0, num_scales, show_every):
        scale_text = generate_scale_text(scale_index)
        extends = ctx.text_extents(scale_text)

        text_x = scale_x + scale_w - extends.width
        text_y = scale_y + math.ceil(scale_index * distance + distance) + (extends.height / 2 - 1)
        ctx.move_to(text_x, text_y)
        ctx.show_text(scale_text)

    ctx.fill()

    # gauge
    gauge_w = GAUGE_W
    gauge_h = scale_h
    gauge_x = w - gauge_w - GUTTER
    gauge_y = GUTTER + header_h + GUTTER

    # gauge color areas
    paint_color_areas(ctx, distance, gauge_w, gauge_x, gauge_y)

    # data bar
    ctx.set_source_rgba(*COLOR_GRAPH)
    value = current_data_point['shortterm']
    value_relative_to_target = min(max(value - TARGET, SCALE_TO), SCALE_FROM + 1)

    data_point_delta_y = (value_relative_to_target - SCALE_TO) * distance + distance - 2

    ctx.rectangle(
        gauge_x + 1,
        gauge_y + gauge_h - 1,
        gauge_w - 2,
        -data_point_delta_y
    )
    ctx.fill()

    # border
    ctx.set_source_rgb(*COLOR_BORDER)
    ctx.rectangle(gauge_x + .5, gauge_y + .5, gauge_w - 1, gauge_h - 1)
    ctx.stroke()

    # graph
    graph_w = w - GUTTER - scale_w - GUTTER - GUTTER - gauge_w - GUTTER
    graph_h = scale_h
    graph_x = GUTTER + scale_w + GUTTER
    graph_y = GUTTER + header_h + GUTTER

    # graph color areas
    paint_color_areas(ctx, distance, graph_w, graph_x, graph_y)

    # data points
    ctx.set_source_rgba(*COLOR_GRAPH)
    data_point_w = (graph_w - 2) / (TIMEFRAME / INTERVAL)
    data_point_x = graph_x + graph_w - 1
    data_point_zero_y = graph_y + graph_h - 1
    ctx.move_to(data_point_x, data_point_zero_y)
    for point in reversed(data.DATA):
        value = point['shortterm']
        value_relative_to_target = max(value - TARGET, SCALE_TO)

        data_point_delta_y = (value_relative_to_target - SCALE_TO) * distance + distance - 2
        ctx.line_to(data_point_x, data_point_zero_y - data_point_delta_y)

        data_point_x -= data_point_w
        if data_point_x < graph_x:
            break

    data_point_x += data_point_w
    ctx.line_to(data_point_x, data_point_zero_y)
    ctx.line_to(data_point_x + graph_w - 1, data_point_zero_y)
    ctx.fill()

    # border
    ctx.set_source_rgb(*COLOR_BORDER)
    ctx.rectangle(graph_x + .5, graph_y + .5, graph_w - 1, graph_h - 1)
    ctx.stroke()

    # scale lines
    ctx.set_source_rgba(*COLOR_SCALE_LINES)
    for scale_index in range(0, num_scales, show_every):
        gauge_line_y = gauge_y + math.ceil(scale_index * distance + distance)
        ctx.move_to(gauge_x + 1, gauge_line_y + .5)
        ctx.line_to(gauge_x + gauge_w - 1, gauge_line_y + .5)

        graph_line_y = graph_y + math.ceil(scale_index * distance + distance)
        ctx.move_to(graph_x + 1, graph_line_y + .5)
        ctx.line_to(graph_x + graph_w - 1, graph_line_y + .5)

    ctx.stroke()


def paint_color_areas(ctx, distance, w, x, y):
    num_too_loud = abs(SCALE_FROM)
    num_loudness_ok = 2
    num_not_loud_enough = abs(SCALE_TO)

    ctx.set_source_rgb(*COLOR_TOO_LOUD)
    height_too_loud = math.ceil(num_too_loud * distance) - 1
    ctx.rectangle(x + 1, y + 1, w - 2, height_too_loud)
    ctx.fill()

    ctx.set_source_rgb(*COLOR_LOUDNESS_OK)
    height_loudness_ok = math.ceil(num_loudness_ok * distance) - 1
    ctx.rectangle(x + 1, y + 1 + height_too_loud, w - 2, height_loudness_ok)
    ctx.fill()

    ctx.set_source_rgb(*COLOR_NOT_LOUD_ENOUGH)
    height_loud_enough = math.ceil(num_not_loud_enough * distance) - 1
    ctx.rectangle(x + 1, y + 1 + height_too_loud + height_loudness_ok, w - 2, height_loud_enough)
    ctx.fill()
    return num_loudness_ok, num_not_loud_enough, num_too_loud


def with_sign(num):
    sign = ''
    if num < 0:
        sign = '-'
    elif num > 0:
        sign = '+'

    return sign + str(abs(num))


def generate_scale_text(scale_index):
    if SCALE == Scale.RELATIVE:
        scale_num = SCALE_FROM - scale_index
    else:
        scale_num = SCALE_FROM - scale_index + TARGET

    return with_sign(scale_num)


if __name__ == '__main__':
    main()
