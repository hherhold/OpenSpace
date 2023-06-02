/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
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

#ifndef __OPENSPACE_MODULE_COSMICLIFE___RENDERABLECOSMICPOINTS___H__
#define __OPENSPACE_MODULE_COSMICLIFE___RENDERABLECOSMICPOINTS___H__

#include <openspace/rendering/renderable.h>

#include <openspace/engine/globals.h>
#include <openspace/camera/camera.h>
#include <openspace/navigation/navigationhandler.h>
#include <modules/space/speckloader.h>
#include <openspace/properties/optionproperty.h>
#include <openspace/properties/stringproperty.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/properties/scalar/floatproperty.h>
#include <openspace/properties/vector/vec3property.h>
#include <openspace/util/distanceconversion.h>
#include <ghoul/opengl/ghoul_gl.h>
#include <ghoul/opengl/uniformcache.h>
#include <filesystem>
 // from billboard cloud
#include <openspace/properties/triggerproperty.h>
#include <openspace/properties/vector/ivec2property.h>
#include <openspace/properties/vector/vec2property.h>
#include <functional>
#include <unordered_map>
#include <algorithm>

namespace ghoul::filesystem { class File; }
namespace ghoul::fontrendering { class Font; }

namespace ghoul::opengl {
    class ProgramObject;
    class Texture;
} // namespace ghoul::opengl

namespace openspace {

    namespace documentation { struct Documentation; }

    class RenderableCosmicPoints : public Renderable {
    public:
        explicit RenderableCosmicPoints(const ghoul::Dictionary& dictionary);
        ~RenderableCosmicPoints() = default;

        void initialize() override;
        void initializeGL() override;
        void deinitializeGL() override;

        bool isReady() const override;

        void render(const RenderData& data, RendererTasks& rendererTask) override;
        void update(const UpdateData& data) override;

        static documentation::Documentation Documentation();

    private:
        std::vector<float> createDataSlice(const RenderData& data);
        void renderToTexture(GLuint textureToRenderTo, GLuint textureWidth,
            GLuint textureHeight);
        void renderPoints(const RenderData& data, const glm::dmat4& modelMatrix,
            const glm::dvec3& orthoRight, const glm::dvec3& orthoUp);
        void updateRenderData(const RenderData& data);
        float fadeObjectDependingOnDistance(const RenderData& data, const speck::Dataset::Entry& e);

        glm::dvec3 cameraPos = global::navigationHandler->camera()->positionVec3();

        // bool variables
        bool _hasSpeckFile = false;
        bool _dataIsDirty = true;
        bool _textColorIsDirty = true;
        bool _hasSpriteTexture = false;
        bool _spriteTextureIsDirty = true;
        bool _hasColorMapFile = false;
        bool _isColorMapExact = false;
        bool _hasDatavarSize = false;

        std::vector<float> _result;



        GLuint _pTexture = 0;

        properties::FloatProperty _scaleFactor;
        properties::Vec3Property _pointColor;
        properties::Vec3Property _frameColor;
        properties::StringProperty _spriteTexturePath;
        properties::BoolProperty _useFade;
        properties::FloatProperty _maxThreshold;
        properties::BoolProperty _drawElements;
        properties::BoolProperty _pixelSizeControl;
        properties::OptionProperty _colorOption;
        properties::Vec2Property _optionColorRangeData;
        properties::OptionProperty _datavarSizeOption;
        properties::Vec2Property _billboardMinMaxSize;
        properties::FloatProperty _correctionSizeEndDistance;
        properties::FloatProperty _correctionSizeFactor;
        properties::BoolProperty _useLinearFiltering;
        properties::TriggerProperty _setRangeFromData;
        properties::OptionProperty _renderOption;



        ghoul::opengl::Texture* _spriteTexture = nullptr;
        ghoul::opengl::ProgramObject* _program = nullptr;


        // variables that are sent to the shaders
        UniformCache(
            cameraViewProjectionMatrix, modelMatrix, cameraPos, cameraLookup, renderOption,
            minBillboardSize, maxBillboardSize, correctionSizeEndDistance,
            correctionSizeFactor, color, alphaValue, scaleFactor, up, right,
            screenSize, spriteTexture, hasColormap, enabledRectSizeControl, hasDvarScaling, frameColor, useGamma
        ) _uniformCache;

        // font variable from ghoul library
        std::shared_ptr<ghoul::fontrendering::Font> _font;

        std::optional<std::string> _uniqueSpecies;
        std::vector<float> _cachedDataSlice;

        // String variables
        std::string _speckFile;
        std::string _colorMapFile;
        std::string _colorOptionString;
        std::string _datavarSizeOptionString;

        // distance default unit -- change?
        DistanceUnit _unit = DistanceUnit::Parsec;

        // speck files
        speck::Dataset _dataset;
        speck::Labelset _labelset;
        speck::ColorMap _colorMap;

        // range data, do we need conversion map?
        std::vector<glm::vec2> _colorRangeData;
        std::unordered_map<int, std::string> _optionConversionMap;
        std::unordered_map<int, std::string> _optionConversionSizeMap;

        glm::dmat4 _transformationMatrix = glm::dmat4(1.0);

        GLuint _vao = 0;
        GLuint _vbo = 0;
    };

} // namespace openspace

#endif // __OPENSPACE_MODULE_COSMICLIFE___RENDERABLECOSMICPOINTS___H__
