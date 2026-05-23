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

#include <modules/base/rendering/linesegments/renderableinterpolatedlinesegments.h>

#include <modules/base/basemodule.h>
#include <openspace/documentation/documentation.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scripting/scriptengine.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/format.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/dictionary.h>
#include <ghoul/opengl/programobject.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
    using namespace openspace;

    constexpr std::string_view _loggerCat = "RenderableInterpolatedLineSegments";

    void triggerInterpolation(std::string_view identifier, float v, float d) {
        using namespace openspace;
        const std::string script = std::format(
            "openspace.setPropertyValueSingle(\"{}\", {}, {})", identifier, v, d
        );
        // No syncing, as this was triggered from a property change (which happened based
        // on an already synced script)
        global::scriptEngine->queueScript({
            .code = script,
            .synchronized = ScriptEngine::Script::ShouldBeSynchronized::No,
            .sendToRemote = ScriptEngine::Script::ShouldSendToRemote::No
        });
    }

    constexpr Property::PropertyInfo InterpolationValueInfo = {
        "Value",
        "Value",
        "The value used for interpolation. The max value is set from the number of steps "
        "in the dataset, so a step of one corresponds to one step in the dataset and "
        "values in-between will be determined using interpolation.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo StepsInfo = {
        "NumberOfSteps",
        "Number of steps",
        "The number of steps available in the dataset, including the initial positions.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo JumpToNextInfo = {
        "JumpToNext",
        "Jump to next",
        "Immediately set the interpolation value to correspond to the next set of line "
        "positions compared to the current.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo JumpToPrevInfo = {
        "JumpToPrevious",
        "Jump to previous",
        "Immediately set the interpolation value to correspond to the previous set of "
        "line positions compared to the current.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo InterpolateToNextInfo = {
        "InterpolateToNext",
        "Interpolate to next",
        "Trigger an interpolation to the next set of line positions. The duration of "
        "the interpolation is set based on the Interpolation Speed property.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo InterpolateToPrevInfo = {
        "InterpolateToPrevious",
        "Interpolate to previous",
        "Trigger an interpolation to the previous set of line positions. The duration "
        "of the interpolation is set based on the Interpolation Speed property.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo InterpolateToEndInfo = {
        "InterpolateToEnd",
        "Interpolate to end",
        "Trigger an interpolation all the way to the final set of positions. The "
        "duration of the interpolation is set based on the Interpolation Speed property.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo InterpolateToStartInfo = {
        "InterpolateToStart",
        "Interpolate to start",
        "Trigger an inverted interpolation to the initial set of positions. The duration "
        "of the interpolation is set based on the Interpolation Speed property.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo InterpolationSpeedInfo = {
        "Speed",
        "Interpolation speed",
        "Affects how long the interpolation takes when triggered using one of the "
        "trigger properties. A value of 1 means that a step takes 1 second.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo UseSplineInfo = {
        "UseSplineInterpolation",
        "Use spline interpolation",
        "If true, the lines will be interpolated using a Catmull-Rom spline instead of "
        "linearly. This leads to a smoother transition at the breakpoints, i.e. between "
        "each step.",
        Property::Visibility::AdvancedUser
    };

    // This is a version of the [RenderableLineSegments](#base_renderable_linesegments)
    // where the dataset may contain multiple time steps that can be interpolated
    // between. It supports interpolation of both the line positions and the data values
    // used for color mapping.
    //
    // The dataset should be structured in a way so that the first N rows correspond to
    // the first set of line vertices, the next N rows to the second set of vertices, and
    // so on. The number of vertices per time step (N) must be specified in the asset.
    struct [[codegen::Dictionary(RenderableInterpolatedLineSegments)]] Parameters {
        // The number of line vertices to read per time step from the dataset. Every
        // N:th datapoint will be interpreted as the same vertex, but at a different step
        // in the interpolation.
        int numberOfObjects [[codegen::greaterequal(1)]];

        struct Interpolation {
            // [[codegen::verbatim(InterpolationValueInfo.description)]]
            std::optional<float> value [[codegen::inrange(0.f, 1.f)]];

            // [[codegen::verbatim(InterpolationSpeedInfo.description)]]
            std::optional<float> speed [[codegen::greater(0.f)]];

            // [[codegen::verbatim(UseSplineInfo.description)]]
            std::optional<bool> useSplineInterpolation;
        };
        // Initial settings for the interpolation.
        std::optional<Interpolation> interpolation;
    };
} // namespace
#include "renderableinterpolatedlinesegments_codegen.cpp"

namespace openspace {

Documentation RenderableInterpolatedLineSegments::Documentation() {
    return codegen::doc<Parameters>(
        "base_renderable_interpolatedlinesegments",
        RenderableLineSegments::Documentation()
    );
}

RenderableInterpolatedLineSegments::Interpolation::Interpolation()
    : PropertyOwner({ "Interpolation", "Interpolation", "" })
    , value(InterpolationValueInfo, 0.f, 0.f, 1.f)
    , nSteps(StepsInfo, 1)
    , goToNextStep(JumpToNextInfo)
    , goToPrevStep(JumpToPrevInfo)
    , interpolateToNextStep(InterpolateToNextInfo)
    , interpolateToPrevStep(InterpolateToPrevInfo)
    , interpolateToEnd(InterpolateToEndInfo)
    , interpolateToStart(InterpolateToStartInfo)
    , speed(InterpolationSpeedInfo, 1.f, 0.01f, 100.f)
    , useSpline(UseSplineInfo, false)
{
    addProperty(value);

    interpolateToEnd.onChange([this]() {
        const float remaining = value.maxValue() - value;
        const float duration = remaining / speed;
        triggerInterpolation(value.uri(), value.maxValue(), duration);
    });
    addProperty(interpolateToEnd);

    interpolateToStart.onChange([this]() {
        const float duration = value / speed;
        triggerInterpolation(value.uri(), 0.f, duration);
    });
    addProperty(interpolateToStart);

    interpolateToNextStep.onChange([this]() {
        const float prevValue = std::floor(value);
        const float newValue = std::min(prevValue + 1.f, value.maxValue());
        const float duration = 1.f / speed;
        triggerInterpolation(value.uri(), newValue, duration);
    });
    addProperty(interpolateToNextStep);

    interpolateToPrevStep.onChange([this]() {
        const float prevValue = std::ceil(value);
        const float newValue = std::max(prevValue - 1.f, value.minValue());
        const float duration = 1.f / speed;
        triggerInterpolation(value.uri(), newValue, duration);
    });
    addProperty(interpolateToPrevStep);

    addProperty(speed);

    goToNextStep.onChange([this]() {
        const float prevValue = std::floor(value);
        value = std::min(prevValue + 1.f, value.maxValue());
    });
    addProperty(goToNextStep);

    goToPrevStep.onChange([this]() {
        const float prevValue = std::ceil(value);
        value = std::max(prevValue - 1.f, value.minValue());
    });
    addProperty(goToPrevStep);

    nSteps.setReadOnly(true);
    addProperty(nSteps);

    addProperty(useSpline);
}

RenderableInterpolatedLineSegments::RenderableInterpolatedLineSegments(
                                                      const ghoul::Dictionary& dictionary)
    : RenderableLineSegments(dictionary)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    addPropertySubOwner(_interpolation);

    if (p.interpolation.has_value()) {
        _interpolation.value = p.interpolation->value.value_or(_interpolation.value);
        _interpolation.speed = p.interpolation->speed.value_or(_interpolation.speed);
        _interpolation.useSpline = p.interpolation->useSplineInterpolation.value_or(
            _interpolation.useSpline
        );
    }

    _interpolation.value.onChange([this]() {
        const bool passedAKnot =
            std::ceil(_interpolation.value) != std::ceil(_prevInterpolationValue);
        if (passedAKnot) {
            _dataIsDirty = true;
        }
        _prevInterpolationValue = _interpolation.value;
    });

    _interpolation.useSpline.onChange([this]() { _dataIsDirty = true; });

    _nObjectsInDataset = static_cast<unsigned int>(p.numberOfObjects);
}

void RenderableInterpolatedLineSegments::initialize() {
    RenderableLineSegments::initialize();

    const unsigned int nEntries = static_cast<unsigned int>(_dataset.entries.size());

    // At this point the dataset has been loaded, so we can compute the number of steps
    if (_nObjectsInDataset == 0 || nEntries % _nObjectsInDataset != 0) {
        LERROR(std::format(
            "Mismatch between the number of data entries ({}) and the specified number "
            "of vertices per step ({}). Expected the number of entries to be evenly "
            "divisible by the number of vertices per step", nEntries, _nObjectsInDataset
        ));
    }

    if (_nObjectsInDataset > 0) {
        _interpolation.nSteps = nEntries / _nObjectsInDataset;
    }
    _interpolation.value.setMaxValue(
        static_cast<float>(_interpolation.nSteps) - 1.f
    );
}

void RenderableInterpolatedLineSegments::initializeShaders() {
    _program = BaseModule::ProgramObjectManager.request(
        "RenderableInterpolatedLineSegments",
        []() {
            return global::renderEngine->buildRenderProgram(
                "RenderableInterpolatedLineSegments",
                absPath(
                    "${MODULE_BASE}/shaders/linesegments/linesegments_interpolated_vs.glsl"
                ),
                absPath("${MODULE_BASE}/shaders/linesegments/linesegments_fs.glsl")
            );
        }
    );
}

void RenderableInterpolatedLineSegments::deinitializeShaders() {
    BaseModule::ProgramObjectManager.release(
        "RenderableInterpolatedLineSegments",
        [](ghoul::opengl::ProgramObject* p) {
            global::renderEngine->removeRenderProgram(p);
        }
    );
    _program = nullptr;
}

void RenderableInterpolatedLineSegments::setExtraUniforms() {
    const float t0 = computeCurrentLowerValue();
    const float t = std::clamp(_interpolation.value - t0, 0.f, 1.f);

    _program->setUniform("interpolationValue", t);
    _program->setUniform("useSpline", useSplineInterpolation());
}

bool RenderableInterpolatedLineSegments::useSplineInterpolation() const {
    return _interpolation.useSpline && _interpolation.nSteps > 1;
}

unsigned int RenderableInterpolatedLineSegments::nObjects() const {
    return _nObjectsInDataset;
}

int RenderableInterpolatedLineSegments::nAttributesPerVertex() const {
    // Two positions (xyz) to interpolate between
    int n = 2 * 3;
    if (useSplineInterpolation()) {
        // Two more positions (xyz), before and after
        n += 2 * 3;
    }
    if (hasColorData()) {
        // Two color values to interpolate between
        n += 2;
    }
    return n;
}

void RenderableInterpolatedLineSegments::addVertexData(unsigned int datasetIndex,
                                                       std::vector<float>& result,
                                                       double& maxRadius) const
{
    const auto [firstIndex, secondIndex] = interpolationIndices(datasetIndex);

    const dataloader::Dataset::Entry& e0 = _dataset.entries[firstIndex];
    const dataloader::Dataset::Entry& e1 = _dataset.entries[secondIndex];

    const glm::dvec3 position0 = transformedPosition(e0);
    const glm::dvec3 position1 = transformedPosition(e1);

    maxRadius = std::max(
        maxRadius,
        std::max(glm::length(position0), glm::length(position1))
    );

    for (int j = 0; j < 3; j++) {
        result.push_back(static_cast<float>(position0[j]));
    }
    for (int j = 0; j < 3; j++) {
        result.push_back(static_cast<float>(position1[j]));
    }

    if (useSplineInterpolation()) {
        // Compute the extra positions before and after the interpolated step, while
        // making sure we do not step outside the bounds of the dataset
        const int nObj = static_cast<int>(_nObjectsInDataset);
        const int beforeIndex = std::max(static_cast<int>(firstIndex) - nObj, 0);
        const int maxT = static_cast<int>(_interpolation.value.maxValue()) - 1;
        const int maxAllowedIndex = maxT * nObj + static_cast<int>(datasetIndex);
        const int afterIndex = std::min(
            static_cast<int>(secondIndex) + nObj,
            maxAllowedIndex
        );

        const glm::dvec3 positionBefore =
            transformedPosition(_dataset.entries[beforeIndex]);
        const glm::dvec3 positionAfter =
            transformedPosition(_dataset.entries[afterIndex]);

        for (int j = 0; j < 3; j++) {
            result.push_back(static_cast<float>(positionBefore[j]));
        }
        for (int j = 0; j < 3; j++) {
            result.push_back(static_cast<float>(positionAfter[j]));
        }
    }

    if (hasColorData()) {
        const int colorParamIndex = currentColorParameterIndex();
        result.push_back(e0.data[colorParamIndex]);
        result.push_back(e1.data[colorParamIndex]);
    }
}

void RenderableInterpolatedLineSegments::setVertexAttributePointers() {
    const int stride = nAttributesPerVertex();
    int offset = 0;
    offset = bufferVertexAttribute("in_position0", 3, stride, offset);
    offset = bufferVertexAttribute("in_position1", 3, stride, offset);

    if (useSplineInterpolation()) {
        offset = bufferVertexAttribute("in_positionBefore", 3, stride, offset);
        offset = bufferVertexAttribute("in_positionAfter", 3, stride, offset);
    }

    if (hasColorData()) {
        offset = bufferVertexAttribute("in_colorValue0", 1, stride, offset);
        offset = bufferVertexAttribute("in_colorValue1", 1, stride, offset);
    }
}

bool RenderableInterpolatedLineSegments::isAtKnot() const {
    const float v = _interpolation.value;
    return (v - std::floor(v)) < std::numeric_limits<float>::epsilon();
}

float RenderableInterpolatedLineSegments::computeCurrentLowerValue() const {
    float t0 = std::floor(_interpolation.value);

    if (isAtKnot()) {
        t0 = t0 - 1.f;
    }

    const float maxTValue = _interpolation.value.maxValue();
    const float maxAllowedT0 = std::max(maxTValue - 1.f, 0.f);
    t0 = std::clamp(t0, 0.f, maxAllowedT0);
    return t0;
}

float RenderableInterpolatedLineSegments::computeCurrentUpperValue() const {
    const float t0 = computeCurrentLowerValue();
    const float t1 = std::clamp(t0 + 1.f, 0.f, _interpolation.value.maxValue());
    return t1;
}

std::pair<size_t, size_t>
RenderableInterpolatedLineSegments::interpolationIndices(unsigned int index) const
{
    const float t0 = computeCurrentLowerValue();
    const float t1 = computeCurrentUpperValue();
    const unsigned int t0Index = static_cast<unsigned int>(t0);
    const unsigned int t1Index = static_cast<unsigned int>(t1);

    const size_t lower = size_t(t0Index * _nObjectsInDataset + index);
    const size_t upper = size_t(t1Index * _nObjectsInDataset + index);

    return std::pair(lower, upper);
}

} // namespace openspace
