#!/usr/bin/env python3
import timeit

import cairo

from example import draw

SIZES = [
    (640, 480),
    (800, 600),
    (1024, 768),
    (1920, 1080)
]
REPEATS = 1000

for w, h in SIZES:
    print(f'{w}x{h}')
    with cairo.ImageSurface(cairo.Format.RGB24, w, h) as surface:
        ctx = cairo.Context(surface)
        exec_time = timeit.timeit(lambda: draw(ctx, w, h), number=REPEATS) / REPEATS
        print(f'{w}x{h}: {round(exec_time * 1000, 2)}ms = {round(1 / exec_time, 1)}fps')
