#!/usr/bin/env python3

import random
import os.path

from PIL import Image


PREFIX_FILENAME = os.path.realpath(
    os.path.dirname(os.path.realpath(__file__)) + '/../../src/images/external/conf')

PREFIX_GENERATED = os.path.realpath(
    os.path.dirname(os.path.realpath(__file__)) + '/../../src/app')

PREFIX_INCLUDE = 'images/external/conf'

TEMPLATE_C = """/*
 * This file was automatically generated by {file}.
 */

#ifdef NSEC_FLAVOR_CONF

#include "gosecure_animation_sequences.h"

{includes}

const uint8_t gosecure_sequence_label_length = {symbols1_length};
const uint8_t gosecure_sequence_inter_1_length = {symbols21_length};
const uint8_t gosecure_sequence_inter_2_length = {symbols22_length};
const uint8_t gosecure_sequence_inter_3_length = {symbols23_length};
const uint8_t gosecure_sequence_pan_length = {symbols3_length};

const struct gosecure_sequence_frame gosecure_sequence_label[] = {{
{symbols1}
}};

const struct gosecure_sequence_frame gosecure_sequence_inter_1[] = {{
{symbols21}
}};

const struct gosecure_sequence_frame gosecure_sequence_inter_2[] = {{
{symbols22}
}};

const struct gosecure_sequence_frame gosecure_sequence_inter_3[] = {{
{symbols23}
}};

const struct gosecure_sequence_frame gosecure_sequence_pan[] = {{
{symbols3}
}};

#endif
"""

TEMPLATE_H = """/*
 * This file was automatically generated by {file}.
 */

#ifndef gosecure_animation_sequences_h
#define gosecure_animation_sequences_h
#include <bitmap.h>

struct gosecure_sequence_frame {{
    const struct bitmap_ext * bitmap;
    uint8_t x;
    uint8_t y;
}};

const struct gosecure_sequence_frame gosecure_sequence_label[{symbols1_length}];
const struct gosecure_sequence_frame gosecure_sequence_inter_1[{symbols21_length}];
const struct gosecure_sequence_frame gosecure_sequence_inter_2[{symbols22_length}];
const struct gosecure_sequence_frame gosecure_sequence_inter_3[{symbols23_length}];
const struct gosecure_sequence_frame gosecure_sequence_pan[{symbols3_length}];

const uint8_t gosecure_sequence_label_length;
const uint8_t gosecure_sequence_inter_1_length;
const uint8_t gosecure_sequence_inter_2_length;
const uint8_t gosecure_sequence_inter_3_length;
const uint8_t gosecure_sequence_pan_length;

#endif
"""


def cut_inter_transaction(image, name_template, crop_size=10):
    includes = []
    symbols = []

    for block in prepare_inter_transition(image, crop_size):
        filename, include, symbol = format_output_name(name_template, block[2])

        box = (block[0] * crop_size,
               block[1] * crop_size,
               block[0] * crop_size + crop_size,
               block[1] * crop_size + crop_size)

        image.crop(box).save(filename)

        includes.append(include)
        symbols.append((symbol, block[0] * crop_size, block[1] * crop_size))

    return includes, symbols


def cut_label_transaction(image, name_template, crop_size=8):
    offset_x = 50
    offset_y = 42

    includes = []
    symbols = []

    for block in prepare_label_transaction(image, crop_size):
        filename, include, symbol = format_output_name(name_template, block[2])

        box = (block[0] * crop_size,
               block[1] * crop_size,
               block[0] * crop_size + crop_size,
               block[1] * crop_size + crop_size)

        image.crop(box).save(filename)

        includes.append(include)
        symbols.append((symbol, block[0] * crop_size + offset_x, block[1] * crop_size + offset_y))

    return includes, symbols


def cut_pan_transaction(image, name_template, slice_width=8):
    includes = []
    symbols = []

    for i in range(0, image.width // slice_width):
        filename, include, symbol = format_output_name(name_template, i)

        box = (i * slice_width, 0, i * slice_width + slice_width, image.height)
        image.crop(box).save(filename)

        includes.append(include)
        symbols.append((symbol, i * slice_width, 0))

    return includes, symbols


def format_symbols(symbols):
    for i, symbol in enumerate(symbols):
        yield '\t{{&{}, {}, {}}}{}'.format(
            symbol[0], symbol[1], symbol[2], ',' if i < len(symbols) - 1 else '')


def format_output_name(template, i):
    templated = template.format(i)

    return ('{}/{}.png'.format(PREFIX_FILENAME, templated),
            '{}/{}_bitmap.h'.format(PREFIX_INCLUDE, templated),
            '{}_bitmap'.format(templated))


def prepare_inter_transition(image, crop_size):
    blocks = []

    k = 0
    for j in range(0, image.height // crop_size):
        for i in range(0, image.width // crop_size):
            blocks.append((i, j, k))
            k += 1

        temp = blocks[min(0, j - 32):]
        random.shuffle(temp)
        blocks = blocks[:min(0, j - 32)] + temp

    return blocks


def prepare_label_transaction(image, crop_size):
    blocks = []

    k = 0
    for i in range(0, image.width // crop_size):
        for j in range(0, image.height // crop_size):
            blocks.append((i, j, k))
            k += 1

    random.shuffle(blocks)

    return blocks


if __name__ == '__main__':
    random.seed(1001)

    includes1, symbols1 = cut_label_transaction(
        Image.open('./initial_front.png'), 'gosecure_1_front_{:03d}')

    includes21, symbols21 = cut_inter_transaction(
        Image.open('./inter_1.png'), 'gosecure_2_1_{:03d}')

    includes22, symbols22 = cut_inter_transaction(
        Image.open('./inter_2.png'), 'gosecure_2_2_{:03d}')

    includes23, symbols23 = cut_inter_transaction(
        Image.open('./inter_3.png'), 'gosecure_2_3_{:03d}')

    includes3, symbols3 = cut_pan_transaction(
        Image.open('./inter_3.png'), 'gosecure_3_{:03d}')

    includes = map(
        lambda i: '#include "{}"'.format(i),
        includes1 + includes21 + includes22 + includes23 + includes3)

    variables = {
        'file': os.path.realpath(__file__),

        'includes': '\n'.join(includes),

        'symbols1': '\n'.join(format_symbols(symbols1)),
        'symbols1_length': len(symbols1),

        'symbols21': '\n'.join(format_symbols(symbols21)),
        'symbols21_length': len(symbols21),

        'symbols22': '\n'.join(format_symbols(symbols22)),
        'symbols22_length': len(symbols22),

        'symbols23': '\n'.join(format_symbols(symbols23)),
        'symbols23_length': len(symbols23),

        'symbols3': '\n'.join(format_symbols(symbols3)),
        'symbols3_length': len(symbols3),
    }

    output_c = TEMPLATE_C.format(**variables)
    output_h = TEMPLATE_H.format(**variables)

    with open('{}/gosecure_animation_sequences.c'.format(PREFIX_GENERATED), 'w') as output:
        output.write(output_c)

    with open('{}/gosecure_animation_sequences.h'.format(PREFIX_GENERATED), 'w') as output:
        output.write(output_h)
