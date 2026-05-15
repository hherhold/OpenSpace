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

#include <modules/space/rendering/renderableinterpolatedconstellationlines.h>

#include <openspace/documentation/documentation.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scripting/scriptengine.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/format.h>
#include <ghoul/glm.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/dictionary.h>
#include <ghoul/misc/exception.h>
#include <ghoul/misc/stringhelper.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>
#include <scn/scan.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace {
    using namespace openspace;

    constexpr std::string_view _loggerCat = "RenderableInterpolatedConstellationLines";

    constexpr Property::PropertyInfo FilesInfo = {
        "Files",
        "Constellation data files (one per timestep)",
        "An ordered list of SPECK files, one for each interpolation timestep. Every "
        "file must contain the same set of constellation identifiers, and each "
        "constellation must have the same number of vertices in every file.",
        Property::Visibility::AdvancedUser
    };

    constexpr Property::PropertyInfo DrawElementsInfo = {
        "DrawElements",
        "Draw elements",
        "Enables/Disables the drawing of the constellations.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo UnitInfo = {
        "Unit",
        "Unit",
        "The distance unit used for the constellation lines data.",
        Property::Visibility::AdvancedUser
    };

    constexpr Property::PropertyInfo ColorsInfo = {
        "Colors",
        "Constellation colors",
        "A list of colors to use for the constellations. A data file may include several "
        "groups of constellations, where each group can have a distinct color. The index "
        "for the color parameter for each constellation in the data file corresponds to "
        "the order of the colors in this list.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo InterpolationValueInfo = {
        "Value",
        "Value",
        "The value used for interpolation. The max value is set from the number of "
        "timestep files provided, so a step of one corresponds to advancing by one "
        "timestep and values in-between will be determined using interpolation.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo StepsInfo = {
        "NumberOfSteps",
        "Number of steps",
        "The number of timestep files loaded, including the initial positions.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo JumpToNextInfo = {
        "JumpToNext",
        "Jump to next",
        "Immediately set the interpolation value to correspond to the next set of "
        "constellation positions compared to the current.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo JumpToPrevInfo = {
        "JumpToPrevious",
        "Jump to previous",
        "Immediately set the interpolation value to correspond to the previous set of "
        "constellation positions compared to the current.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo InterpolateToNextInfo = {
        "InterpolateToNext",
        "Interpolate to next",
        "Trigger an interpolation to the next set of constellation positions. The "
        "duration of the interpolation is set based on the Interpolation Speed property.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo InterpolateToPrevInfo = {
        "InterpolateToPrevious",
        "Interpolate to previous",
        "Trigger an interpolation to the previous set of constellation positions. The "
        "duration of the interpolation is set based on the Interpolation Speed property.",
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
        "Trigger an inverted interpolation to the initial set of positions. The "
        "duration of the interpolation is set based on the Interpolation Speed property.",
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
        "If true, the constellation vertices will be interpolated using a Catmull-Rom "
        "spline instead of linearly. This leads to a smoother transition at the "
        "breakpoints, i.e. between each timestep.",
        Property::Visibility::AdvancedUser
    };

    void triggerInterpolation(std::string_view identifier, float v, float d) {
        std::string script = std::format(
            "openspace.setPropertyValueSingle(\"{}\", {}, {})",
            identifier, v, d
        );
        global::scriptEngine->queueScript({
            .code = script,
            .synchronized = ScriptEngine::Script::ShouldBeSynchronized::No,
            .sendToRemote = ScriptEngine::Script::ShouldSendToRemote::No
        });
    }

    // Draws constellation lines that are interpolated between several keyframe sets of
    // vertex positions. Each keyframe is supplied as its own SPECK file (in the `Files`
    // list, in the order they should be interpolated through). All files must declare the
    // same constellations (matching `id` strings) and each constellation must have the
    // same number of vertices in every file.
    //
    // The animation is driven by the `Interpolation.Value` property, which ranges from 0
    // (first keyframe) up to N-1 (last keyframe, where N is the number of files). The
    // helper trigger properties (`InterpolateToNext`, `InterpolateToEnd`, etc.) wrap this
    // value in convenient one-click actions.
    //
    // Each constellation is given an abbreviation that acts as the identifier of the
    // constellation. These abbreviations can be mapped to full names in the optional
    // `NamesFile`. The names in this file are then the ones that will show in the user
    // interface.
    struct [[codegen::Dictionary(RenderableInterpolatedConstellationLines)]] Parameters {
        // [[codegen::verbatim(FilesInfo.description)]]
        std::vector<std::filesystem::path> files;

        // [[codegen::verbatim(DrawElementsInfo.description)]]
        std::optional<bool> drawElements;

        enum class [[codegen::map(openspace::DistanceUnit)]] Unit {
            Meter [[codegen::key("m")]],
            Kilometer [[codegen::key("Km")]],
            Parsec [[codegen::key("pc")]],
            Kiloparsec [[codegen::key("Kpc")]],
            Megaparsec [[codegen::key("Mpc")]],
            Gigaparsec [[codegen::key("Gpc")]],
            Gigalightyear [[codegen::key("Gly")]]
        };
        // [[codegen::verbatim(UnitInfo.description)]]
        std::optional<Unit> unit;

        // [[codegen::verbatim(ColorsInfo.description)]]
        std::optional<std::vector<glm::vec3>> colors [[codegen::color()]];

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
#include "renderableinterpolatedconstellationlines_codegen.cpp"

namespace openspace {

Documentation RenderableInterpolatedConstellationLines::Documentation() {
    return codegen::doc<Parameters>(
        "space_renderable_interpolatedconstellationlines",
        RenderableConstellationsBase::Documentation()
    );
}

RenderableInterpolatedConstellationLines::Interpolation::Interpolation()
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

RenderableInterpolatedConstellationLines::RenderableInterpolatedConstellationLines(
                                                      const ghoul::Dictionary& dictionary)
    : RenderableConstellationsBase(dictionary)
    , _drawElements(DrawElementsInfo, true)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _speckFiles = p.files;
    if (_speckFiles.size() < 2) {
        throw ghoul::RuntimeError(
            "RenderableInterpolatedConstellationLines requires at least two timestep "
            "files in the 'Files' list"
        );
    }

    _drawElements = p.drawElements.value_or(_drawElements);
    addProperty(_drawElements);

    if (p.unit.has_value()) {
        _constellationUnit = codegen::map<DistanceUnit>(*p.unit);
    }
    else {
        _constellationUnit = DistanceUnit::Meter;
    }

    if (p.colors.has_value()) {
        const std::vector<glm::vec3>& ops = *p.colors;
        for (size_t i = 0; i < ops.size(); i++) {
            _constellationColorMap.insert({ static_cast<int>(i) + 1, ops[i] });
        }
    }

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
            _bufferIsDirty = true;
        }
        _prevInterpolationValue = _interpolation.value;
    });

    _interpolation.useSpline.onChange([this]() {
        _shouldRecreateBuffers = true;
        _bufferIsDirty = true;
    });
}

void RenderableInterpolatedConstellationLines::selectionPropertyHasChanged() {
    using ConstellationKeyValuePair = std::pair<const int, ConstellationLine>;

    if (!_selection.hasSelected()) {
        for (ConstellationKeyValuePair& pair : _renderingConstellationsMap) {
            pair.second.isEnabled = true;
        }

        if (_hasLabels) {
            for (dataloader::Labelset::Entry& e : _labels->labelSet().entries) {
                e.isEnabled = true;
            }
        }
    }
    else {
        for (ConstellationKeyValuePair& pair : _renderingConstellationsMap) {
            const bool isSelected = _selection.isSelected(pair.second.name);
            pair.second.isEnabled = isSelected;

            if (_hasLabels) {
                for (dataloader::Labelset::Entry& e : _labels->labelSet().entries) {
                    if (constellationFullName(e.identifier) == pair.second.name) {
                        e.isEnabled = isSelected;
                        break;
                    }
                }
            }
        }
    }
}

void RenderableInterpolatedConstellationLines::initialize() {
    RenderableConstellationsBase::initialize();

    loadData();

    _interpolation.nSteps = static_cast<unsigned int>(_speckFiles.size());
    _interpolation.value.setMaxValue(
        static_cast<float>(_interpolation.nSteps - 1)
    );

    if (!_assetSelection.empty()) {
        const std::vector<std::string> options = _selection.options();
        std::set<std::string> selectedConstellations;

        for (const std::string& s : _assetSelection) {
            auto it = std::find(options.cbegin(), options.cend(), s);
            if (it == options.cend()) {
                it = std::find(
                    options.cbegin(),
                    options.cend(),
                    constellationFullName(s)
                );

                if (it == options.cend()) {
                    LWARNING(std::format(
                        "Option '{}' not found in list of constellations", s
                    ));
                }
                else {
                    selectedConstellations.insert(constellationFullName(s));
                }
            }
            else {
                selectedConstellations.insert(s);
            }
        }
        _selection = selectedConstellations;
    }
}

void RenderableInterpolatedConstellationLines::initializeGL() {
    _program = global::renderEngine->buildRenderProgram(
        "RenderableInterpolatedConstellationLines",
        absPath("${MODULE_SPACE}/shaders/constellationlines_interpolated_vs.glsl"),
        absPath("${MODULE_SPACE}/shaders/constellationlines_fs.glsl")
    );

    ghoul::opengl::updateUniformLocations(*_program, _uniformCache);

    createConstellations();
    rebuildBuffersForCurrentStep();
    _bufferIsDirty = false;
    _shouldRecreateBuffers = false;
}

void RenderableInterpolatedConstellationLines::destroyBuffers() {
    using ConstellationKeyValuePair = std::pair<const int, ConstellationLine>;
    for (ConstellationKeyValuePair& pair : _renderingConstellationsMap) {
        if (pair.second.vao != 0) {
            glDeleteVertexArrays(1, &pair.second.vao);
            pair.second.vao = 0;
        }
        if (pair.second.vbo != 0) {
            glDeleteBuffers(1, &pair.second.vbo);
            pair.second.vbo = 0;
        }
    }
}

void RenderableInterpolatedConstellationLines::deinitializeGL() {
    destroyBuffers();

    if (_program) {
        global::renderEngine->removeRenderProgram(_program.get());
        _program = nullptr;
    }
}

void RenderableInterpolatedConstellationLines::renderConstellations(
                                                       const glm::dmat4& modelViewMatrix,
                                                       const glm::dmat4& projectionMatrix)
{
    glEnablei(GL_BLEND, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(false);
    glEnable(GL_DEPTH_TEST);

    _program->activate();

    _program->setUniform(_uniformCache.modelViewTransform, modelViewMatrix);
    _program->setUniform(_uniformCache.projectionTransform, projectionMatrix);
    _program->setUniform(_uniformCache.opacity, opacity());

    const float t0 = computeCurrentLowerValue();
    const float t = std::clamp(_interpolation.value - t0, 0.f, 1.f);
    _program->setUniform(_uniformCache.interpolationValue, t);
    _program->setUniform(_uniformCache.useSpline, useSplineInterpolation());

    using ConstellationKeyValuePair = std::pair<const int, ConstellationLine>;
    for (const ConstellationKeyValuePair& pair : _renderingConstellationsMap) {
        if (!pair.second.isEnabled) {
            continue;
        }

        _program->setUniform(
            _uniformCache.color,
            _constellationColorMap[pair.second.colorIndex]
        );

        glBindVertexArray(pair.second.vao);
        glLineWidth(_lineWidth);
        glDrawArrays(GL_LINE_STRIP, 0, pair.second.numV);
        global::renderEngine->openglStateCache().resetLineState();
    }

    glBindVertexArray(0);
    _program->deactivate();

    global::renderEngine->openglStateCache().resetDepthState();
    global::renderEngine->openglStateCache().resetBlendState();
}

void RenderableInterpolatedConstellationLines::render(const RenderData& data,
                                                      RendererTasks& tasks)
{
    const glm::dmat4 modelViewTransform = calcModelViewTransform(data);
    const glm::dmat4 projectionTransform = data.camera.projectionMatrix();

    if (_drawElements) {
        renderConstellations(modelViewTransform, projectionTransform);
    }

    RenderableConstellationsBase::render(data, tasks);
}

void RenderableInterpolatedConstellationLines::update(const UpdateData&) {
    if (_shouldRecreateBuffers) {
        destroyBuffers();
        createConstellations();
        _shouldRecreateBuffers = false;
        _bufferIsDirty = true;
    }

    if (_bufferIsDirty) {
        rebuildBuffersForCurrentStep();
        _bufferIsDirty = false;
    }

    if (_program->isDirty()) {
        _program->rebuildFromFile();
        ghoul::opengl::updateUniformLocations(*_program, _uniformCache);
    }
}

void RenderableInterpolatedConstellationLines::loadData() {
    const float scale = static_cast<float>(toMeter(_constellationUnit));
    double maxRadius = 0.0;

    _renderingConstellationsMap.clear();

    for (size_t stepIndex = 0; stepIndex < _speckFiles.size(); stepIndex++) {
        const std::filesystem::path& rawPath = _speckFiles[stepIndex];
        const std::filesystem::path fileName = absPath(rawPath.string());

        LINFO(std::format(
            "Loading Speck file '{}' (timestep {})", fileName, stepIndex
        ));
        std::ifstream file(fileName);
        if (!file.good()) {
            throw ghoul::RuntimeError(std::format(
                "Failed to open Speck file '{}'", fileName
            ));
        }

        int lineIndex = 0;

        std::string line;
        while (true) {
            ghoul::getline(file, line);

            if (file.eof()) {
                break;
            }

            if (!line.empty() && line.back() == '\r') {
                line = line.substr(0, line.length() - 1);
            }

            if (line.empty() || line[0] == '#') {
                continue;
            }

            if (!line.contains("mesh")) {
                continue;
            }

            std::stringstream str(line);

            int meshColorIndex = 0;
            std::string dummy;
            str >> dummy; // mesh command
            dummy.clear();
            str >> dummy; // color index command
            while (dummy != "{") {
                if (dummy == "-c") {
                    str >> meshColorIndex;
                }
                else {
                    LWARNING(std::format(
                        "Unknown command '{}' found in constellation file '{}'",
                        dummy, fileName
                    ));
                }
                dummy.clear();
                str >> dummy;
            }

            ghoul::getline(file, line);

            std::stringstream id(line);
            std::string identifier;
            id >> dummy; // id command
            dummy.clear();
            ghoul::getline(id, identifier);
            ghoul::trimWhitespace(identifier);
            const std::string fullName = constellationFullName(identifier);

            ghoul::getline(file, line);
            std::stringstream dim(line);
            int numV = 0;
            dim >> numV;

            std::vector<GLfloat> vertices;
            vertices.reserve(numV * 3);

            for (int l = 0; l < numV; l++) {
                ghoul::getline(file, line);
                if (line.substr(0, 1) == "}") {
                    break;
                }

                glm::vec3 pos = glm::vec3(0.f);
                auto reading = scn::scan<float, float, float>(line, "{} {} {}");
                if (reading) {
                    std::tie(pos.x, pos.y, pos.z) = reading->values();
                    pos *= scale;
                    vertices.push_back(pos.x);
                    vertices.push_back(pos.y);
                    vertices.push_back(pos.z);
                }
                else {
                    LERROR(std::format(
                        "Failed reading position on line {} of mesh '{}' in file '{}'. "
                        "Stopped reading constellation data", l, identifier, fileName
                    ));
                }

                const double r = glm::length(glm::dvec3(pos));
                maxRadius = std::max(maxRadius, r);
            }

            ghoul::getline(file, line);
            if (line.substr(0, 1) != "}") {
                throw ghoul::RuntimeError(std::format(
                    "Error parsing constellation '{}' in file '{}'", identifier, fileName
                ));
            }

            if (stepIndex == 0) {
                ConstellationLine constellationLine;
                constellationLine.lineIndex = lineIndex++;
                constellationLine.colorIndex = meshColorIndex;
                constellationLine.name = fullName;
                constellationLine.numV = numV;
                constellationLine.verticesPerTimestep.resize(_speckFiles.size());
                constellationLine.verticesPerTimestep[0] = std::move(vertices);

                _renderingConstellationsMap.insert({
                    constellationLine.lineIndex, std::move(constellationLine)
                });
            }
            else {
                // Find the existing constellation by name (matches what timestep 0
                // produced). Note: lineIndex from the first file is the canonical key,
                // so we look up by name rather than by lineIndex (the order of meshes
                // in subsequent files does not have to match).
                auto it = std::find_if(
                    _renderingConstellationsMap.begin(),
                    _renderingConstellationsMap.end(),
                    [&fullName](const auto& kv) { return kv.second.name == fullName; }
                );
                if (it == _renderingConstellationsMap.end()) {
                    throw ghoul::RuntimeError(std::format(
                        "Constellation '{}' appears in '{}' but was not present in the "
                        "first timestep file. All timestep files must declare the same "
                        "set of constellations.", identifier, fileName
                    ));
                }

                if (numV != it->second.numV) {
                    throw ghoul::RuntimeError(std::format(
                        "Constellation '{}' has {} vertices in '{}' but {} vertices in "
                        "the first timestep file. The vertex count for each "
                        "constellation must be the same across all files.",
                        identifier, numV, fileName, it->second.numV
                    ));
                }

                it->second.verticesPerTimestep[stepIndex] = std::move(vertices);
            }
        }

        // After the first file, validate that every constellation seen at timestep 0
        // also got data for this timestep — an empty vertex vector for this stepIndex
        // means the constellation was missing from this file.
        if (stepIndex == 0) {
            continue;
        }
        for (const auto& kv : _renderingConstellationsMap) {
            if (kv.second.verticesPerTimestep[stepIndex].empty()) {
                throw ghoul::RuntimeError(std::format(
                    "Constellation '{}' is missing from '{}'. All timestep files must "
                    "declare the same set of constellations.", kv.second.name, fileName
                ));
            }
        }
    }

    setBoundingSphere(maxRadius);
}

void RenderableInterpolatedConstellationLines::createConstellations() {
    LDEBUG("Creating constellations");

    const bool spline = useSplineInterpolation();
    const int floatsPerVertex = spline ? 12 : 6;
    const GLsizei stride = floatsPerVertex * sizeof(GLfloat);

    using ConstellationKeyValuePair = std::pair<const int, ConstellationLine>;
    for (ConstellationKeyValuePair& p : _renderingConstellationsMap) {
        glCreateBuffers(1, &p.second.vbo);
        glCreateVertexArrays(1, &p.second.vao);
        glVertexArrayVertexBuffer(p.second.vao, 0, p.second.vbo, 0, stride);

        // in_position0
        glEnableVertexArrayAttrib(p.second.vao, 0);
        glVertexArrayAttribFormat(p.second.vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(p.second.vao, 0, 0);

        // in_position1
        glEnableVertexArrayAttrib(p.second.vao, 1);
        glVertexArrayAttribFormat(
            p.second.vao, 1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat)
        );
        glVertexArrayAttribBinding(p.second.vao, 1, 0);

        if (spline) {
            // in_positionBefore
            glEnableVertexArrayAttrib(p.second.vao, 2);
            glVertexArrayAttribFormat(
                p.second.vao, 2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat)
            );
            glVertexArrayAttribBinding(p.second.vao, 2, 0);

            // in_positionAfter
            glEnableVertexArrayAttrib(p.second.vao, 3);
            glVertexArrayAttribFormat(
                p.second.vao, 3, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat)
            );
            glVertexArrayAttribBinding(p.second.vao, 3, 0);
        }
    }
}

void RenderableInterpolatedConstellationLines::rebuildBuffersForCurrentStep() {
    const auto [lower, upper] = currentStepIndices();
    const bool spline = useSplineInterpolation();
    const int floatsPerVertex = spline ? 12 : 6;

    const size_t maxStep = _speckFiles.size() - 1;
    const size_t beforeIdx = (lower > 0) ? lower - 1 : lower;
    const size_t afterIdx = std::min(upper + 1, maxStep);

    using ConstellationKeyValuePair = std::pair<const int, ConstellationLine>;
    for (ConstellationKeyValuePair& pair : _renderingConstellationsMap) {
        ConstellationLine& c = pair.second;

        const std::vector<GLfloat>& v0 = c.verticesPerTimestep[lower];
        const std::vector<GLfloat>& v1 = c.verticesPerTimestep[upper];
        const std::vector<GLfloat>& vBefore = c.verticesPerTimestep[beforeIdx];
        const std::vector<GLfloat>& vAfter = c.verticesPerTimestep[afterIdx];

        std::vector<GLfloat> interleaved(
            static_cast<size_t>(floatsPerVertex) * c.numV
        );
        for (int i = 0; i < c.numV; i++) {
            GLfloat* out = interleaved.data() + i * floatsPerVertex;
            const int src = i * 3;
            out[0] = v0[src + 0]; out[1] = v0[src + 1]; out[2] = v0[src + 2];
            out[3] = v1[src + 0]; out[4] = v1[src + 1]; out[5] = v1[src + 2];
            if (spline) {
                out[6] = vBefore[src + 0];
                out[7] = vBefore[src + 1];
                out[8] = vBefore[src + 2];
                out[9]  = vAfter[src + 0];
                out[10] = vAfter[src + 1];
                out[11] = vAfter[src + 2];
            }
        }

        glNamedBufferData(
            c.vbo,
            interleaved.size() * sizeof(GLfloat),
            interleaved.data(),
            GL_DYNAMIC_DRAW
        );
    }
}

bool RenderableInterpolatedConstellationLines::useSplineInterpolation() const {
    return _interpolation.useSpline && _interpolation.nSteps > 1;
}

float RenderableInterpolatedConstellationLines::computeCurrentLowerValue() const {
    const float v = _interpolation.value;
    const bool atKnot =
        (v - std::floor(v)) < std::numeric_limits<float>::epsilon();

    float t0 = std::floor(v);
    if (atKnot) {
        t0 = t0 - 1.f;
    }

    const float maxTValue = _interpolation.value.maxValue();
    const float maxAllowedT0 = std::max(maxTValue - 1.f, 0.f);
    t0 = std::clamp(t0, 0.f, maxAllowedT0);
    return t0;
}

float RenderableInterpolatedConstellationLines::computeCurrentUpperValue() const {
    const float t0 = computeCurrentLowerValue();
    return std::clamp(t0 + 1.f, 0.f, _interpolation.value.maxValue());
}

std::pair<size_t, size_t>
RenderableInterpolatedConstellationLines::currentStepIndices() const
{
    const float t0 = computeCurrentLowerValue();
    const float t1 = computeCurrentUpperValue();
    return std::pair(
        static_cast<size_t>(t0),
        static_cast<size_t>(t1)
    );
}

} // namespace openspace
