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

#ifndef __OPENSPACE_MODULE_SPACE___RENDERABLEINTERPOLATEDCONSTELLATIONLINES___H__
#define __OPENSPACE_MODULE_SPACE___RENDERABLEINTERPOLATEDCONSTELLATIONLINES___H__

#include <modules/space/rendering/renderableconstellationsbase.h>

#include <openspace/properties/misc/triggerproperty.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/properties/scalar/floatproperty.h>
#include <openspace/properties/scalar/uintproperty.h>
#include <openspace/util/distanceconversion.h>
#include <ghoul/opengl/ghoul_gl.h>
#include <ghoul/opengl/uniformcache.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openspace {

/**
 * A specialization of the constellation-lines renderable that supports interpolation
 * between several keyframe sets of vertex positions. Each keyframe is a complete .speck
 * file with the same constellation identifiers and the same number of vertices per
 * constellation. The renderable animates between the keyframes either linearly or with
 * Catmull-Rom spline interpolation.
 */
class RenderableInterpolatedConstellationLines : public RenderableConstellationsBase {
public:
    explicit RenderableInterpolatedConstellationLines(
        const ghoul::Dictionary& dictionary);
    ~RenderableInterpolatedConstellationLines() override = default;

    void initialize() override;
    void initializeGL() override;
    void deinitializeGL() override;

    void render(const RenderData& data, RendererTasks& tasks) override;
    void update(const UpdateData& data) override;

    static openspace::Documentation Documentation();

private:
    struct ConstellationLine {
        bool isEnabled = true;
        std::string name;
        int lineIndex = 0;
        int colorIndex = 0;
        int numV = 0;
        GLuint vao = 0;
        GLuint vbo = 0;
        // verticesPerTimestep[step] is a flat list of numV * 3 floats (xyz per vertex)
        std::vector<std::vector<GLfloat>> verticesPerTimestep;
    };

    struct Interpolation : public PropertyOwner {
        Interpolation();
        FloatProperty value;
        UIntProperty nSteps;

        TriggerProperty goToNextStep;
        TriggerProperty goToPrevStep;
        TriggerProperty interpolateToNextStep;
        TriggerProperty interpolateToPrevStep;
        TriggerProperty interpolateToEnd;
        TriggerProperty interpolateToStart;
        FloatProperty speed;

        BoolProperty useSpline;
    };
    Interpolation _interpolation;

    void loadData();
    void createConstellations();
    void destroyBuffers();
    void rebuildBuffersForCurrentStep();
    void renderConstellations(const glm::dmat4& modelViewMatrix,
        const glm::dmat4& projectionMatrix);

    void selectionPropertyHasChanged() override;

    bool useSplineInterpolation() const;
    float computeCurrentLowerValue() const;
    float computeCurrentUpperValue() const;
    std::pair<size_t, size_t> currentStepIndices() const;

    std::vector<std::filesystem::path> _speckFiles;
    BoolProperty _drawElements;

    std::unique_ptr<ghoul::opengl::ProgramObject> _program = nullptr;
    UniformCache(modelViewTransform, projectionTransform, opacity, color,
        interpolationValue, useSpline) _uniformCache;

    DistanceUnit _constellationUnit = DistanceUnit::Parsec;

    std::unordered_map<int, glm::vec3> _constellationColorMap;
    std::unordered_map<int, ConstellationLine> _renderingConstellationsMap;

    bool _bufferIsDirty = true;
    bool _shouldRecreateBuffers = false;
    float _prevInterpolationValue = 0.f;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_SPACE___RENDERABLEINTERPOLATEDCONSTELLATIONLINES___H__
