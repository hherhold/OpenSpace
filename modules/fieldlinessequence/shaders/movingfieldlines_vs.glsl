/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2022                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#version __CONTEXT__

// uniforms
uniform vec4 lineColor;
uniform mat4 modelViewProjection;

uniform int colorMethod;
uniform sampler1D colorTable;
uniform vec2 colorTableRange;
// Inputs
layout(location = 0) in vec4 in_position;        // Should be provided in meters
layout(location = 1) in float in_color_scalar;   // The extra value used to color lines
layout(location = 2) in float in_vertex_alpha;   // Vertex alpha based on lifetime

// outs
out vec4 vs_color;
out float vs_depth;
out float debugTopologyColor;

const int colorByQuantity = 1;

vec4 getTransferFunctionColor() {
    // Remap the color scalar to a [0,1] range
    float lookUpVal = (in_color_scalar - colorTableRange.x) /
                            (colorTableRange.y - colorTableRange.x);
    return texture(colorTable, lookUpVal);
}
vec3 topologyColor(float topology)
{
    // Pathline
    vec3 color = vec3(0.4f, 0.4f, 0.4f);    // gray

    // Closed
    float t = step(0.0f, topology);
    color = t * vec3(0.7f, 0.2f, 0.2f) + (1.0f - t) * color; // blue

    // Open
	t = step(1.0f, topology);
	color = t * vec3(0.2f, 0.7f, 0.2f) + (1.0f - t) * color; // green

    // IMF
	t = step(2.0f, topology);
	color = t * vec3(0.2f, 0.2f, 0.7f) + (1.0f - t) * color; // red

    return color;
}

void main() {
    vs_color = lineColor;
    vs_color.xyz = topologyColor(in_position.w);

    if (colorMethod == colorByQuantity) {
        vec4 quantityColor = getTransferFunctionColor();
        vs_color = vec4(quantityColor.xyz, vs_color.a * quantityColor.a);
    }
    vs_color.a = in_vertex_alpha;

    vec4 position_in_meters = vec4(in_position.xyz, 1);
    vec4 positionClipSpace = modelViewProjection * position_in_meters;
    gl_Position = vec4(positionClipSpace.xy, 0, positionClipSpace.w);
    vs_depth = gl_Position.w;
}