/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2026                                                               *
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

#include "powerscaling/powerscaling_vs.glsl"

layout(location = 0) in vec3 in_position0;
layout(location = 1) in vec3 in_position1;

// Only bound and used when spline interpolation is enabled
layout(location = 2) in vec3 in_positionBefore;
layout(location = 3) in vec3 in_positionAfter;

out Data {
  vec4 positionViewSpace;
  float screenSpaceDepth;
} out_data;

uniform dmat4 modelViewTransform;
uniform dmat4 projectionTransform;
uniform float interpolationValue;
uniform bool useSpline;


vec3 interpolateCatmullRom(float t, vec3 p0, vec3 p1, vec3 p2, vec3 p3) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5 * (
    2.0 * p1 +
    t * (p2 - p0) +
    t2 * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) +
    t3 * (3.0 * p1 - p0 - 3.0 * p2 + p3)
  );
}


void main() {
  vec3 position = mix(in_position0, in_position1, interpolationValue);
  if (useSpline) {
    position = interpolateCatmullRom(
      interpolationValue,
      in_positionBefore,
      in_position0,
      in_position1,
      in_positionAfter
    );
  }

  dvec4 positionViewSpace = modelViewTransform * dvec4(position, 1.0);
  gl_Position = vec4(z_normalization(vec4(projectionTransform * positionViewSpace)));

  out_data.positionViewSpace = vec4(positionViewSpace);
  out_data.screenSpaceDepth = gl_Position.w;
}
