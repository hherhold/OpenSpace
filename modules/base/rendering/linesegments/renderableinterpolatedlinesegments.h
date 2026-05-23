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

#ifndef __OPENSPACE_MODULE_BASE___RENDERABLEINTERPOLATEDLINESEGMENTS___H__
#define __OPENSPACE_MODULE_BASE___RENDERABLEINTERPOLATEDLINESEGMENTS___H__

#include <modules/base/rendering/linesegments/renderablelinesegments.h>

#include <openspace/properties/misc/triggerproperty.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/properties/scalar/floatproperty.h>
#include <openspace/properties/scalar/uintproperty.h>
#include <utility>

namespace openspace {

/**
 * A specialization of the RenderableLineSegments that supports interpolating the lines
 * between multiple sets of vertex positions (time steps). The data file should contain
 * the line vertices for the first time step first, then the second time step, and so on.
 */
class RenderableInterpolatedLineSegments : public RenderableLineSegments {
public:
    explicit RenderableInterpolatedLineSegments(const ghoul::Dictionary& dictionary);
    ~RenderableInterpolatedLineSegments() override = default;

    void initialize() override;

    static openspace::Documentation Documentation();

protected:
    void initializeShaders() override;
    void deinitializeShaders() override;
    void setExtraUniforms() override;

    int nAttributesPerVertex() const override;
    unsigned int nObjects() const override;

    void addVertexData(unsigned int datasetIndex, std::vector<float>& result,
        double& maxRadius) const override;
    void setVertexAttributePointers() override;

private:
    bool useSplineInterpolation() const;
    bool isAtKnot() const;
    float computeCurrentLowerValue() const;
    float computeCurrentUpperValue() const;
    std::pair<size_t, size_t> interpolationIndices(unsigned int index) const;

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

    float _prevInterpolationValue = 0.f;
    unsigned int _nObjectsInDataset = 0;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_BASE___RENDERABLEINTERPOLATEDLINESEGMENTS___H__
