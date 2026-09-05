#version 450

// Font data and lookup adapted from Electronic Arts' ShaderToHuman project.
// Copyright (c) 2024-2025 Electronic Arts Inc. BSD-3-Clause.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 output_color;

layout(std430, binding = 0) readonly buffer text_data {
    uint character_count;
    uint characters[];
} text;

const uint mini_font[] = uint[](
    0x00306c6cu, 0x30003860u, 0x18600000u, 0x00000006u, 0x00786c6cu, 0x7cc66c60u, 0x30306630u, 0x0000000cu,
    0x00786cfeu, 0xc0cc38c0u, 0x60183c30u, 0x00000018u, 0x0030006cu, 0x78187600u, 0x6018fffcu, 0x00fc0030u,
    0x003000feu, 0x0c30dc00u, 0x60183c30u, 0x00000060u, 0x0000006cu, 0xf866cc00u, 0x30306630u, 0x300030c0u,
    0x0030006cu, 0x30c67600u, 0x18600000u, 0x30003080u, 0x00000000u, 0x00000000u, 0x00000000u, 0x60000000u,
    0x7c307878u, 0x1cfc38fcu, 0x78780000u, 0x18006078u, 0xc670ccccu, 0x3cc060ccu, 0xcccc3030u, 0x300030ccu,
    0xce300c0cu, 0x6cf8c00cu, 0xcccc3030u, 0x60fc180cu, 0xde303838u, 0xcc0cf818u, 0x787c0000u, 0xc0000c18u,
    0xf630600cu, 0xfe0ccc30u, 0xcc0c0000u, 0x60001830u, 0xe630ccccu, 0x0ccccc30u, 0xcc183030u, 0x30fc3000u,
    0x7cfcfc78u, 0x1e787830u, 0x78703030u, 0x18006030u, 0x00000000u, 0x00000000u, 0x00000060u, 0x00000000u,
    0x7c30fc3cu, 0xf8fefe3cu, 0xcc781ee6u, 0xf0c6c638u, 0xc6786666u, 0x6c626266u, 0xcc300c66u, 0x60eee66cu,
    0xdecc66c0u, 0x666868c0u, 0xcc300c6cu, 0x60fef6c6u, 0xdecc7cc0u, 0x667878c0u, 0xfc300c78u, 0x60fedec6u,
    0xdefc66c0u, 0x666868ceu, 0xcc30cc6cu, 0x62d6cec6u, 0xc0cc6666u, 0x6c626066u, 0xcc30cc66u, 0x66c6c66cu,
    0x78ccfc3cu, 0xf8fef03eu, 0xcc7878e6u, 0xfec6c638u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0xfc78fc78u, 0xfcccccc6u, 0xc6ccfe78u, 0xc0781000u, 0x66cc66ccu, 0xb4ccccc6u, 0xc6ccc660u, 0x60183800u,
    0x66cc66e0u, 0x30ccccc6u, 0x6ccc8c60u, 0x30186c00u, 0x7ccc7c70u, 0x30ccccd6u, 0x38781860u, 0x1818c600u,
    0x60dc6c1cu, 0x30ccccfeu, 0x38303260u, 0x0c180000u, 0x607866ccu, 0x30cc78eeu, 0x6c306660u, 0x06180000u,
    0xf01ce678u, 0x78fc30c6u, 0xc678fe78u, 0x02780000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x000000ffu,
    0x3000e000u, 0x1c003800u, 0xe0300ce0u, 0x70000000u, 0x30006000u, 0x0c006c00u, 0x60000060u, 0x30000000u,
    0x18786078u, 0x0c786076u, 0x6c700c66u, 0x30ccf878u, 0x000c7cccu, 0x7cccf0ccu, 0x76300c6cu, 0x30feccccu,
    0x007c66c0u, 0xccfc60ccu, 0x66300c78u, 0x30feccccu, 0x00cc66ccu, 0xccc0607cu, 0x6630cc6cu, 0x30d6ccccu,
    0x0076dc78u, 0x7678f00cu, 0xe678cce6u, 0x78c6cc78u, 0x00000000u, 0x000000f8u, 0x00007800u, 0x00000000u,
    0x00000000u, 0x10000000u, 0x0000001cu, 0x18e076ffu, 0x00000000u, 0x30000000u, 0x00000030u, 0x1830dcffu,
    0xdc76dc7cu, 0x7cccccc6u, 0xc6ccfc30u, 0x183000ffu, 0x66cc76c0u, 0x30ccccd6u, 0x6ccc98e0u, 0x001c00ffu,
    0x66cc6678u, 0x30ccccfeu, 0x38cc3030u, 0x183000ffu, 0x7c7c600cu, 0x34cc78feu, 0x6c7c6430u, 0x183000ffu,
    0x600cf0f8u, 0x1876306cu, 0xc60cfc1cu, 0x18e000ffu, 0xf01e0000u, 0x00000000u, 0x00f80000u, 0x000000ffu
);

bool font_lookup(uint ascii, ivec2 pixel_position) {
    if (uint(pixel_position.x) >= 8u || uint(pixel_position.y) >= 8u || ascii <= 32u || ascii > 127u) {
        return false;
    }
    const uint character = ascii - 32u;
    const uvec2 character_position = uvec2(character % 16u, character / 16u);
    const uvec2 pixel = uvec2(character_position.x * 8u + uint(pixel_position.x), character_position.y * 8u + uint(pixel_position.y));
    const uint word = pixel.x / 32u + pixel.y * 4u;
    const uint bit = pixel.x & 0x1fu;
    return ((mini_font[word] >> (31u - bit)) & 1u) != 0u;
}

void main() {
    const ivec2 screen_pixel = ivec2(gl_FragCoord.xy);
    const ivec2 text_pixel = screen_pixel - ivec2(24, 24);
    const int column = text_pixel.x / 16;
    const int row = text_pixel.y / 16;
    if (text_pixel.x < 0 || text_pixel.y < 0 || column < 0 || column >= 60 || row < 0) {
        output_color = vec4(0.02, 0.03, 0.05, 1.0);
        return;
    }

    uint character_index = 0u;
    int current_row = 0;
    int current_column = 0;
    while (character_index < text.character_count && (current_row < row || current_column < column)) {
        if (text.characters[character_index] == 10u) {
            current_row++;
            current_column = 0;
        } else {
            current_column++;
            if (current_column >= 60) {
                current_row++;
                current_column = 0;
            }
        }
        character_index++;
    }

    uint ascii = 32u;
    if (character_index < text.character_count && current_row == row && current_column == column) {
        ascii = text.characters[character_index];
    }
    const ivec2 glyph_pixel = ivec2((text_pixel.x % 16) / 2, (text_pixel.y % 16) / 2);
    output_color = font_lookup(ascii, glyph_pixel) ? vec4(0.35, 0.95, 0.75, 1.0) : vec4(0.02, 0.03, 0.05, 1.0);
}
