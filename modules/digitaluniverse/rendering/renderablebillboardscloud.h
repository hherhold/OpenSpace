/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2023                                                               *
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

#ifndef __OPENSPACE_MODULE_DIGITALUNIVERSE___RENDERABLEBILLBOARDSCLOUD___H__
#define __OPENSPACE_MODULE_DIGITALUNIVERSE___RENDERABLEBILLBOARDSCLOUD___H__

#include <openspace/rendering/renderable.h>

#include <modules/space/labelscomponent.h>
#include <openspace/properties/optionproperty.h>
#include <openspace/properties/stringproperty.h>
#include <openspace/properties/triggerproperty.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/properties/scalar/floatproperty.h>
#include <openspace/properties/vector/ivec2property.h>
#include <openspace/properties/vector/vec2property.h>
#include <openspace/properties/vector/vec3property.h>
#include <openspace/util/distanceconversion.h>
#include <ghoul/opengl/ghoul_gl.h>
#include <ghoul/opengl/uniformcache.h>
#include <functional>
#include <unordered_map>

namespace ghoul::filesystem { class File; }
namespace ghoul::opengl {
    class ProgramObject;
    class Texture;
} // namespace ghoul::opengl

namespace openspace {

namespace documentation { struct Documentation; }

class RenderableBillboardsCloud : public Renderable {
public:
    explicit RenderableBillboardsCloud(const ghoul::Dictionary& dictionary);
    ~RenderableBillboardsCloud() override = default;

    void initialize() override;
    void initializeGL() override;
    void deinitializeGL() override;

    bool isReady() const override;

    void render(const RenderData& data, RendererTasks& rendererTask) override;
    void update(const UpdateData& data) override;

    static documentation::Documentation Documentation();

private:
    int nAttributesPerPoint() const;
    void updateBufferData();
    void updateSpriteTexture();

    std::vector<float> createDataSlice();
    void createPolygonTexture();
    void renderToTexture(GLuint textureToRenderTo, GLuint textureWidth,
        GLuint textureHeight);
    void loadPolygonGeometryForRendering();
    void renderPolygonGeometry(GLuint vao);
    void renderBillboards(const RenderData& data, const glm::dmat4& modelMatrix,
        const glm::dvec3& orthoRight, const glm::dvec3& orthoUp, float fadeInVariable);

    bool _dataIsDirty = true;
    bool _spriteTextureIsDirty = true;

    bool _hasSpriteTexture = false;
    bool _hasSpeckFile = false;
    bool _hasColorMapFile = false;
    bool _hasDatavarSize = false;
    bool _hasPolygon = false;
    bool _hasLabels = false;

    int _polygonSides = 0;

    GLuint _pTexture = 0;

    struct SizeSettings : properties::PropertyOwner {
        SizeSettings(const ghoul::Dictionary& dictionary);

        properties::FloatProperty scaleFactor;

        properties::BoolProperty pixelSizeControl;
        properties::Vec2Property billboardMinMaxSize;
        properties::FloatProperty correctionSizeEndDistance;
        properties::FloatProperty correctionSizeFactor;
    } _sizeSettings;

    struct SizeFromData : properties::PropertyOwner {
        SizeFromData(const ghoul::Dictionary& dictionary);

        //properties::BoolProperty useSizeFromData; // TODO: add
        properties::OptionProperty datavarSizeOption;
    } _sizeFromData;

    struct ColorMapSettings : properties::PropertyOwner {
        ColorMapSettings(const ghoul::Dictionary& dictionary);

        properties::BoolProperty enabled;
        properties::OptionProperty colorOption;

        properties::StringProperty colorMapFile;

        properties::TriggerProperty setRangeFromData;
        properties::Vec2Property optionColorRangeData;
        properties::BoolProperty useLinearFiltering;

        properties::BoolProperty isColorMapExact;

        std::vector<glm::vec2> colorRangeData;
    } _colorMapSettings;

    properties::Vec3Property _pointColor;
    properties::StringProperty _spriteTexturePath;
    properties::BoolProperty _drawElements;
    properties::Vec2Property _fadeInDistances;
    properties::BoolProperty _disableFadeInDistance;
    properties::OptionProperty _renderOption;

    ghoul::opengl::Texture* _polygonTexture = nullptr;
    ghoul::opengl::Texture* _spriteTexture = nullptr;
    ghoul::opengl::ProgramObject* _program = nullptr;
    ghoul::opengl::ProgramObject* _renderToPolygonProgram = nullptr;

    UniformCache(
        cameraViewProjectionMatrix, modelMatrix, cameraPos, cameraLookup, renderOption,
        minBillboardSize, maxBillboardSize, correctionSizeEndDistance,
        correctionSizeFactor, color, alphaValue, scaleFactor, up, right, fadeInValue,
        screenSize, spriteTexture, useColormap, enabledRectSizeControl, hasDvarScaling
    ) _uniformCache;

    std::string _speckFile;

    DistanceUnit _unit = DistanceUnit::Parsec;

    speck::Dataset _dataset;
    speck::ColorMap _colorMap;

    // Everything related to the labels is handled by LabelsComponent
    std::unique_ptr<LabelsComponent> _labels;

    glm::dmat4 _transformationMatrix = glm::dmat4(1.0);

    GLuint _vao = 0;
    GLuint _vbo = 0;

    // For polygons
    GLuint _polygonVao = 0;
    GLuint _polygonVbo = 0;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_DIGITALUNIVERSE___RENDERABLEBILLBOARDSCLOUD___H__
