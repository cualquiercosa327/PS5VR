#!/usr/bin/env python3
# PS5VR
# Copyright (C) 2026 Husam Osman
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the frame generation pipelines (engine/shaders/agc/fg_*.pipe).

8K60 sources decode at 30 fps on the PS5 while the headset renders. These
passes make the picture between two decoded frames, so the headset still
shows a new picture every 60 Hz slot:

  fg_down       a 4x4 box of luma -> one texel (RGB or luma in, luma out);
                run twice per frame: 1/4 size, then 1/16
  fg_me_coarse  block matching at 1/16 size: each texel is a 4x4 block of the
                newer frame, searched +-8 texels in the older one
  fg_me_refine  the same at 1/4 size, +-2 texels around the coarse vector (and
                its neighbours' and zero), so 4x4 blocks of 1/4 size = 16x16
                pixels get a vector
  fg_interp     the picture halfway: the older frame half a vector back, the
                newer half a vector on; where those disagree (something
                appeared or was hidden) it falls back to a plain blend

Vectors are older -> newer displacement, in the texels of the image they were
measured on; .z holds the match error. All passes share the upscaler's vertex
stage and resource mapping (one fragment table of combined textures). Compile
with build_agc_pipes.py (amdllpc, gfx1013).
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "engine", "shaders", "agc")

RGBA8 = "VK_FORMAT_R8G8B8A8_UNORM"
RGBA16F = "VK_FORMAT_R16G16B16A16_SFLOAT"

VS = r"""#version 450

layout(set = 0, binding = 0, std140) uniform PassConstants {
    vec4 uUv;       /* xy = source UV origin, zw = source UV extent */
} pass;

layout(location = 0) out vec2 vUV;

void main() {
    vec2 p = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    vUV = pass.uUv.xy + vec2(p.x, 1.0 - p.y) * pass.uUv.zw;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""


def fs(samplers, body):
    decl = "\n".join(f"layout(set = 1, binding = {i}) uniform sampler2D uIn{i};"
                     for i in range(samplers))
    return f"""#version 450

{decl}

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 out_color;

float luma(vec3 c) {{ return dot(c, vec3(0.2126, 0.7152, 0.0722)); }}

{body}
"""


DOWN = fs(1, r"""
void main() {
    /* four bilinear taps, each the mean of 2x2 texels: a 4x4 box */
    vec2 t = 1.0 / vec2(textureSize(uIn0, 0));
    float l = luma(texture(uIn0, vUV + vec2(-t.x, -t.y)).rgb) + luma(texture(uIn0, vUV + vec2(t.x, -t.y)).rgb) +
              luma(texture(uIn0, vUV + vec2(-t.x, t.y)).rgb) + luma(texture(uIn0, vUV + vec2(t.x, t.y)).rgb);
    l *= 0.25;
    out_color = vec4(l, l, l, 1.0);
}
""")

SAD = r"""
/* Sum of absolute differences of the 4x4 block at b in the newer frame (uIn1)
 * against the older (uIn0) displaced by -d. */
float sad(ivec2 b, ivec2 d, ivec2 lim) {
    float s = 0.0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
            ivec2 p = b + ivec2(i, j);
            ivec2 q = clamp(p - d, ivec2(0), lim);
            s += abs(texelFetch(uIn1, p, 0).r - texelFetch(uIn0, q, 0).r);
        }
    return s;
}
"""

ME_COARSE = fs(2, SAD + r"""
void main() {
    ivec2 size = textureSize(uIn1, 0);
    ivec2 lim = size - 1;
    ivec2 b = min(ivec2(vUV * vec2(size / 4)) * 4, size - 4);
    ivec2 best = ivec2(0);
    float bs = sad(b, ivec2(0), lim);
    for (int y = -8; y <= 8; y++)
        for (int x = -8; x <= 8; x++) {
            /* a little bias towards no motion keeps flat areas still */
            float s = sad(b, ivec2(x, y), lim) + 0.004 * float(abs(x) + abs(y));
            if (s < bs) { bs = s; best = ivec2(x, y); }
        }
    out_color = vec4(vec2(best), bs, 1.0);
}
""")

ME_REFINE = fs(3, SAD + r"""
void main() {
    ivec2 size = textureSize(uIn1, 0);       /* the 1/4 image */
    ivec2 lim = size - 1;
    ivec2 grid = size / 4;
    ivec2 g = ivec2(vUV * vec2(grid));
    ivec2 b = min(g * 4, size - 4);
    ivec2 cs = textureSize(uIn2, 0);         /* the coarse vectors (1/16 image, 4x4 blocks) */
    ivec2 cg = clamp(g / 4, ivec2(0), cs - 1);
    ivec2 best = ivec2(0);
    float bs = sad(b, ivec2(0), lim);
    /* the neighbours' vectors too, so a block on an edge can take the right one */
    for (int ny = -1; ny <= 1; ny++)
        for (int nx = -1; nx <= 1; nx++) {
            ivec2 c = ivec2(round(texelFetch(uIn2, clamp(cg + ivec2(nx, ny), ivec2(0), cs - 1), 0).xy * 4.0));
            int r = (nx == 0 && ny == 0) ? 2 : 0;
            for (int y = -r; y <= r; y++)
                for (int x = -r; x <= r; x++) {
                    ivec2 d = c + ivec2(x, y);
                    float s = sad(b, d, lim) + 0.004 * float(abs(d.x) + abs(d.y));
                    if (s < bs) { bs = s; best = d; }
                }
        }
    out_color = vec4(vec2(best), bs, 1.0);
}
""")

INTERP = fs(3, r"""
void main() {
    vec2 full = vec2(textureSize(uIn0, 0));
    /* vectors are in 1/4-image texels: 4 source pixels each */
    vec2 mv = texture(uIn2, vUV).xy * 4.0 / full;
    vec4 a = texture(uIn0, vUV - 0.5 * mv);
    vec4 b = texture(uIn1, vUV + 0.5 * mv);
    vec4 warped = 0.5 * (a + b);
    vec4 plain = 0.5 * (texture(uIn0, vUV) + texture(uIn1, vUV));
    float err = abs(luma(a.rgb) - luma(b.rgb));
    out_color = vec4(mix(warped.rgb, plain.rgb, smoothstep(0.05, 0.15, err)), 1.0);
}
""")


def resource_mapping(samplers):
    lines = [
        "userDataNode[0].visibility = 2",
        "userDataNode[0].type = DescriptorTableVaPtr",
        "userDataNode[0].offsetInDwords = 0",
        "userDataNode[0].sizeInDwords = 1",
        "userDataNode[0].next[0].type = DescriptorConstBuffer",
        "userDataNode[0].next[0].offsetInDwords = 0",
        "userDataNode[0].next[0].sizeInDwords = 4",
        "userDataNode[0].next[0].set = 0",
        "userDataNode[0].next[0].binding = 0",
        "userDataNode[1].visibility = 64",
        "userDataNode[1].type = DescriptorTableVaPtr",
        "userDataNode[1].offsetInDwords = 0",
        "userDataNode[1].sizeInDwords = 1",
    ]
    for i in range(samplers):
        lines += [
            f"userDataNode[1].next[{i}].type = DescriptorCombinedTexture",
            f"userDataNode[1].next[{i}].offsetInDwords = {i * 12}",
            f"userDataNode[1].next[{i}].sizeInDwords = 12",
            f"userDataNode[1].next[{i}].set = 1",
            f"userDataNode[1].next[{i}].binding = {i}",
        ]
    return "\n".join(lines)


def write_pipe(name, frag, samplers, fmt, comment):
    text = f"""; {name} - {comment}
; Generated by tools/shaders/gen_framegen_pipes.py. Edit the generator, not this file.

[Version]
version = 65

[VsGlsl]
{VS}
[VsInfo]
entryPoint = main

[FsGlsl]
{frag}
[FsInfo]
entryPoint = main

[ResourceMapping]
{resource_mapping(samplers)}

[GraphicsPipelineState]
topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
nggState.enableNgg = 1
nggState.enableGsUse = 0
colorBuffer[0].format = {fmt}
colorBuffer[0].channelWriteMask = 15
colorBuffer[0].blendEnable = 0
"""
    path = os.path.join(OUT, f"{name}.pipe")
    with open(path, "w", newline="\n") as f:
        f.write(text)
    print(f"  wrote {name}.pipe")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    write_pipe("fg_down", DOWN, 1, RGBA16F, "4x4 luma box downscale")
    write_pipe("fg_me_coarse", ME_COARSE, 2, RGBA16F, "block matching, 1/16 size, +-8")
    write_pipe("fg_me_refine", ME_REFINE, 3, RGBA16F, "block matching, 1/4 size, +-2 around coarse")
    write_pipe("fg_interp", INTERP, 3, RGBA8, "the picture halfway between two frames")
