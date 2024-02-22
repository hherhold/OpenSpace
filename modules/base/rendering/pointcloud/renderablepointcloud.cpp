/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2024                                                               *
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

#include <modules/base/rendering/pointcloud/renderablepointcloud.h>

#include <modules/base/basemodule.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/util/updatestructures.h>
#include <openspace/rendering/renderengine.h>
#include <ghoul/filesystem/file.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/glm.h>
#include <ghoul/io/texture/texturereader.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/crc32.h>
#include <ghoul/misc/templatefactory.h>
#include <ghoul/misc/profiling.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/texture.h>
#include <ghoul/opengl/textureconversion.h>
#include <ghoul/opengl/textureunit.h>
#include <glm/gtx/string_cast.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <optional>
#include <string>

namespace {
    constexpr std::string_view _loggerCat = "RenderablePointCloud";

    constexpr std::array<const char*, 30> UniformNames = {
        "cameraViewMatrix", "projectionMatrix", "modelMatrix", "cameraPosition",
        "cameraLookUp", "renderOption", "maxAngularSize", "color", "opacity",
        "scaleExponent", "scaleFactor", "up", "right", "fadeInValue", "hasSpriteTexture",
        "spriteTexture", "useColorMap", "colorMapTexture", "cmapRangeMin", "cmapRangeMax",
        "nanColor", "useNanColor", "hideOutsideRange", "enableMaxSizeControl",
        "aboveRangeColor", "useAboveRangeColor", "belowRangeColor", "useBelowRangeColor",
        "hasDvarScaling", "dvarScaleFactor"
    };

    enum RenderOption {
        ViewDirection = 0,
        PositionNormal
    };

    constexpr openspace::properties::Property::PropertyInfo SpriteTextureInfo = {
        "Texture",
        "Point Sprite Texture",
        "The path to the texture that should be used as the point sprite. Note that if "
        "the MultiTexture option is set in the asset, this value will be ignored.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo UseSpriteTextureInfo = {
        "UseTexture",
        "Use Texture",
        "If true, use the provided sprite texture to render the point. If false, draw "
        "the points using the default point shape.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo PointColorInfo = {
        "FixedColor",
        "Fixed Color",
        "This value is used to define the color of the points when no color map is"
        "used",
        openspace::properties::Property::Visibility::NoviceUser
    };

    constexpr openspace::properties::Property::PropertyInfo DrawElementsInfo = {
        "DrawElements",
        "Draw Elements",
        "Enables/Disables the drawing of the points",
        // @VISIBILITY(1.25)
        openspace::properties::Property::Visibility::NoviceUser
    };

    static const openspace::properties::PropertyOwner::PropertyOwnerInfo LabelsInfo = {
        "Labels",
        "Labels",
        "The labels for the points. If no label file is provided, the labels will be "
        "created to match the points in the data file. For a CSV file, you should then "
        "specify which column is the 'Name' column in the data mapping. For SPECK files "
        "the labels are created from the comment at the end of each line"
    };

    constexpr openspace::properties::Property::PropertyInfo RenderOptionInfo = {
        "RenderOption",
        "Render Option",
        "Option wether the point billboards should face the camera or not. Used for "
        "non-linear display environments such as fisheye.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo FadeInDistancesInfo = {
        "FadeInDistances",
        "Fade-In Start and End Distances",
        "These values determine the initial and final distances from the origin of "
        "the dataset at which the points will start and end fading-in. The distances "
        "are specified in the same unit as the points, that is, the one provodied as the "
        "Unit, or meters. With normal fading the points are fully visible once the "
        "camera is outside this range and fully invisible when inside the range. With "
        "inverted fading the situation is the opposite: the points are visible inside "
        "hen closer than the min value of the range and invisible when further away.",
        // @VISIBILITY(3.25)
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo EnableDistanceFadeInfo = {
        "Enabled",
        "Enable Distance-based Fading",
        "Enables/disables the Fade-in effect based on camera distance. Automatically set "
        "to true if FadeInDistances are specified in the asset.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo InvertFadeInfo = {
        "Invert",
        "Invert",
        "This property can be used the invert the fading so that the points are "
        "invisible when the camera is further away than the max fade distance "
        "and fully visible when it is closer than the min distance.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo UseAdditiveBlendingInfo = {
        "UseAdditiveBlending",
        "Use Additive Blending",
        "If true (default), the color of points rendered on top of each other is "
        "blended additively, resulting in a brighter color where points overlap. "
        "If false, no such blending will take place and the color of the point "
        "will not be modified by blending. Note that this may lead to weird behaviors "
        "when the points are rendered with transparency.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo NumShownDataPointsInfo = {
        "NumberOfDataPoints",
        "Number of Shown Data Points",
        "This read only property includes information about how many points are being "
        "rendered.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo ScaleExponentInfo = {
        "ScaleExponent",
        "Scale Exponent",
        "This value is used as in exponential scaling to set the absolute size of the "
        "point. In general, the larger distance the dataset covers, the larger this "
        "value should be. If not included, it is computed based on the maximum "
        "positional component of the data points. This is useful for showing the "
        "dataset at all, but you will likely want to change it to something that looks "
        "good.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo ScaleFactorInfo = {
        "ScaleFactor",
        "Scale Factor",
        "This value is used as a multiplicative factor to adjust the size of the points, "
        "after the exponential scaling and any max size control effects. Simply just "
        "increases or decreases the visual size of the points.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo UseMaxSizeControlInfo = {
        "EnableMaxSizeControl",
        "Enable Max Size Control",
        "If true, the Max Size property will be used as an upper limit for the size of "
        "the point. This reduces the size of the points when approaching them, so that "
        "they stick to a maximum visual size depending on the Max Size value.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo MaxSizeInfo = {
        "MaxSize",
        "Max Size",
        "This value controls the maximum allowed size for the points, when the max size "
        "control feature is enabled. This limits the visual size of the points based on "
        "the distance to the camera. The larger the value, the larger the points are "
        "allowed to become. In the background, the computations are made by limiting the "
        "size to a certain angle based on the field of view of the camera. So a value of "
        "1 limits the point size to take up a maximum of one degree of the view space.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    // A RenderablePointCloud can be used to render point-based datasets in 3D space,
    // optionally including color mapping, a sprite texture and labels. There are several
    // properties that affect the visuals of the points, such as settings for scaling,
    // fading, sprite texture, color mapping and whether the colors of overlapping points
    // should be blended additively or not.
    //
    // The point size depends on a few different things:
    //
    // - At the core, scaling is done based on an exponential value, the 'ScaleExponent'.
    //   A relatively small change to this value will lead to a large change in size.
    //   When no exponent is set, one will be created based on the coordinates in the
    //   dataset. The points will be visible, but may be appeared as too large or small.
    //   One option is to not specify the exponent when loading the dataset for the the,
    //   first time, to make sure the points are visual, and then adapt the value
    //   interactively when OpenSpace is running until you find a value that you find
    //   suitable.
    //
    // - There is also an option to limit the size of the points based on a given max
    //   size value.
    //
    // - To easily change the visual size of the points, the multiplicative 'ScaleFactor'
    //   may be used. A value of 2 makes the points twice as large, visually, compared
    //   to 1.
    //
    // See example files in data/assets/examples/pointcloud for some concrete examples of
    // point clouds with different settings.
    struct [[codegen::Dictionary(RenderablePointCloud)]] Parameters {
        // The path to the data file that contains information about the point to be
        // rendered. Can be either a CSV or SPECK file
        std::optional<std::filesystem::path> file;

        // If true (default), the loaded dataset will be cached so that it can be loaded
        // faster at a later time. This does however mean that any updates to the values
        // in the dataset will not lead to changes in the rendering without first removing
        // the cached file. Set it to false to disable caching. This can be useful for
        // example when working on importing a new dataset
        std::optional<bool> useCaching;

        // A dictionary specifying details on how to load the dataset. Updating the data
        // mapping will lead to a new cached version of the dataset
        std::optional<ghoul::Dictionary> dataMapping
            [[codegen::reference("dataloader_datamapping")]];

        // [[codegen::verbatim(SpriteTextureInfo.description)]]
        std::optional<std::string> texture;

        struct MultiTexture {
            // The folder where the textures are located
            std::filesystem::path folder [[codegen::directory()]];

            // TODO: THIS will be required for CSV files!
            std::optional<std::filesystem::path> textureMappingFile;
        };
        // Settings related to mapping multiple different textures to the points in the
        // dataset. Note that this requires that texture information has been included
        // in the dataset, either in the SPECK file directly or as part of the data
        // mapping for a CSV file.
        std::optional<MultiTexture> multiTexture;

        // [[codegen::verbatim(UseSpriteTextureInfo.description)]]
        std::optional<bool> useTexture;

        // [[codegen::verbatim(DrawElementsInfo.description)]]
        std::optional<bool> drawElements;

        enum class [[codegen::map(RenderOption)]] RenderOption {
            ViewDirection [[codegen::key("Camera View Direction")]],
            PositionNormal [[codegen::key("Camera Position Normal")]]
        };
        // [[codegen::verbatim(RenderOptionInfo.description)]]
        std::optional<RenderOption> renderOption;

        // [[codegen::verbatim(UseAdditiveBlendingInfo.description)]]
        std::optional<bool> useAdditiveBlending;

        enum class [[codegen::map(openspace::DistanceUnit)]] Unit {
            Meter [[codegen::key("m")]],
            Kilometer [[codegen::key("Km")]],
            Parsec [[codegen::key("pc")]],
            Kiloparsec [[codegen::key("Kpc")]],
            Megaparsec [[codegen::key("Mpc")]],
            Gigaparsec [[codegen::key("Gpc")]],
            Gigalightyear [[codegen::key("Gly")]]
        };
        // The unit used for all distances. Should match the unit of any
        // distances/positions in the data files
        std::optional<Unit> unit;

        // [[codegen::verbatim(LabelsInfo.description)]]
        std::optional<ghoul::Dictionary> labels
            [[codegen::reference("labelscomponent")]];

        struct SizeSettings {
            // Settings related to scaling the points based on data
            std::optional<ghoul::Dictionary> sizeMapping
                [[codegen::reference("base_sizemappingcomponent")]];

            // [[codegen::verbatim(ScaleExponentInfo.description)]]
            std::optional<float> scaleExponent;

            // [[codegen::verbatim(ScaleFactorInfo.description)]]
            std::optional<float> scaleFactor;

            // [[codegen::verbatim(UseMaxSizeControlInfo.description)]]
            std::optional<bool> enableMaxSizeControl;

            // [[codegen::verbatim(MaxSizeInfo.description)]]
            std::optional<float> maxSize;
        };
        // Settings related to the scale of the points, whether they should limit to
        // a certain max size, etc.
        std::optional<SizeSettings> sizeSettings;

        struct ColorSettings {
            // [[codegen::verbatim(PointColorInfo.description)]]
            std::optional<glm::vec3> fixedColor [[codegen::color()]];

            // Settings related to the choice of color map, parameters, etc.
            std::optional<ghoul::Dictionary> colorMapping
                [[codegen::reference("colormappingcomponent")]];
        };
        // Settings related to the coloring of the points, such as a fixed color,
        // color map, etc.
        std::optional<ColorSettings> coloring;

        struct Fading {
            // [[codegen::verbatim(EnableDistanceFadeInfo.description)]]
            std::optional<bool> enabled;

            // [[codegen::verbatim(FadeInDistancesInfo.description)]]
            std::optional<glm::dvec2> fadeInDistances;

            // [[codegen::verbatim(InvertFadeInfo.description)]]
            std::optional<bool> invert;
        };
        // Settings related to fading based on camera distance. Can be used to either
        // fade away or fade in the points when reaching a certain distance from the
        // origin of the dataset
        std::optional<Fading> fading;

        // Transformation matrix to be applied to the position of each object
        std::optional<glm::dmat4x4> transformationMatrix;
    };

#include "renderablepointcloud_codegen.cpp"
}  // namespace

namespace openspace {

documentation::Documentation RenderablePointCloud::Documentation() {
    return codegen::doc<Parameters>("base_renderablepointcloud");
}

RenderablePointCloud::SizeSettings::SizeSettings(const ghoul::Dictionary& dictionary)
    : properties::PropertyOwner({ "Sizing", "Sizing", ""})
    , scaleExponent(ScaleExponentInfo, 1.f, 0.f, 25.f)
    , scaleFactor(ScaleFactorInfo, 1.f, 0.f, 50.f)
    , useMaxSizeControl(UseMaxSizeControlInfo, false)
    , maxAngularSize(MaxSizeInfo, 1.f, 0.f, 45.f)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    if (p.sizeSettings.has_value()) {
        const Parameters::SizeSettings settings = *p.sizeSettings;

        scaleFactor = settings.scaleFactor.value_or(scaleFactor);
        scaleExponent = settings.scaleExponent.value_or(scaleExponent);
        useMaxSizeControl = settings.enableMaxSizeControl.value_or(useMaxSizeControl);
        maxAngularSize = settings.maxSize.value_or(maxAngularSize);

        if (settings.sizeMapping.has_value()) {
            sizeMapping = std::make_unique<SizeMappingComponent>(
                *settings.sizeMapping
            );
            addPropertySubOwner(sizeMapping.get());
        }
    }

    addProperty(scaleFactor);
    addProperty(scaleExponent);
    addProperty(useMaxSizeControl);
    addProperty(maxAngularSize);
}

RenderablePointCloud::ColorSettings::ColorSettings(const ghoul::Dictionary& dictionary)
    : properties::PropertyOwner({ "Coloring", "Coloring", "" })
    , pointColor(PointColorInfo, glm::vec3(1.f), glm::vec3(0.f), glm::vec3(1.f))
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    if (p.coloring.has_value()) {
        const Parameters::ColorSettings settings = *p.coloring;
        pointColor = settings.fixedColor.value_or(pointColor);

        if (settings.colorMapping.has_value()) {
            colorMapping = std::make_unique<ColorMappingComponent>(
                *settings.colorMapping
           );
           addPropertySubOwner(colorMapping.get());
        }
    }
    pointColor.setViewOption(properties::Property::ViewOptions::Color);
    addProperty(pointColor);
}

RenderablePointCloud::Fading::Fading(const ghoul::Dictionary& dictionary)
    : properties::PropertyOwner({ "Fading", "Fading", "" })
    , enabled(EnableDistanceFadeInfo, false)
    , fadeInDistances(
        FadeInDistancesInfo,
        glm::vec2(0.f),
        glm::vec2(0.f),
        glm::vec2(100.f)
    )
    , invert(InvertFadeInfo, false)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    if (p.fading.has_value()) {
        const Parameters::Fading f = *p.fading;

        if (f.fadeInDistances.has_value()) {
            fadeInDistances = *f.fadeInDistances;
            // Set the allowed max value based of that which was entered. Just to give
            // useful values for the slider
            fadeInDistances.setMaxValue(10.f * glm::vec2(fadeInDistances.value().y));
        }

        enabled = f.enabled.value_or(f.fadeInDistances.has_value());
        invert = f.invert.value_or(invert);
    }

    addProperty(enabled);
    fadeInDistances.setViewOption(properties::Property::ViewOptions::MinMaxRange);
    addProperty(fadeInDistances);
    addProperty(invert);
}

RenderablePointCloud::RenderablePointCloud(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _spriteTexturePath(SpriteTextureInfo)
    , _useSpriteTexture(UseSpriteTextureInfo, true)
    , _drawElements(DrawElementsInfo, true)
    , _useAdditiveBlending(UseAdditiveBlendingInfo, true)
    , _renderOption(RenderOptionInfo, properties::OptionProperty::DisplayType::Dropdown)
    , _nDataPoints(NumShownDataPointsInfo, 0)
    , _fading(dictionary)
    , _colorSettings(dictionary)
    , _sizeSettings(dictionary)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    addProperty(Fadeable::_opacity);

    if (p.file.has_value()) {
        _hasDataFile = true;
        _dataFile = absPath(*p.file).string();
    }

    if (p.dataMapping.has_value()) {
        _dataMapping = dataloader::DataMapping::createFromDictionary(*p.dataMapping);
    }

    _drawElements = p.drawElements.value_or(_drawElements);
    addProperty(_drawElements);

    _renderOption.addOption(RenderOption::ViewDirection, "Camera View Direction");
    _renderOption.addOption(RenderOption::PositionNormal, "Camera Position Normal");

    if (p.renderOption.has_value()) {
        _renderOption = codegen::map<RenderOption>(*p.renderOption);
    }
    else {
        _renderOption = RenderOption::ViewDirection;
    }
    addProperty(_renderOption);

    _useAdditiveBlending = p.useAdditiveBlending.value_or(_useAdditiveBlending);
    addProperty(_useAdditiveBlending);

    if (p.unit.has_value()) {
        _unit = codegen::map<DistanceUnit>(*p.unit);
    }
    else {
        _unit = DistanceUnit::Meter;
    }

    _useSpriteTexture = p.useTexture.value_or(_useSpriteTexture);
    addProperty(_useSpriteTexture);

    // Read texture information. Multi-texture is prioritized over texture
    if (p.multiTexture.has_value()) {
        _textureMode = TextureInputMode::Multi;
        _hasSpriteTexture = true;
        _texturesDirectory = absPath((*p.multiTexture).folder).string();

        if (p.texture.has_value()) {
            LWARNING(
                "Both a MultiTexture and Texture was set in the asset. The "
                "MultiTexture has priority and any single texture will be ignored"
            );
        }
    }
    else if (p.texture.has_value()) {
        _textureMode = TextureInputMode::Single;
        _hasSpriteTexture = true;
        _spriteTexturePath = absPath(*p.texture).string();
        _spriteTexturePath.onChange([this]() { _spriteTextureIsDirty = true; });
        addProperty(_spriteTexturePath);
    }

    _transformationMatrix = p.transformationMatrix.value_or(_transformationMatrix);

    if (p.sizeSettings.has_value() && p.sizeSettings->sizeMapping.has_value()) {
        _sizeSettings.sizeMapping->parameterOption.onChange(
            [this]() { _dataIsDirty = true; }
        );
        _hasDatavarSize = true;
    }

    addPropertySubOwner(_sizeSettings);
    addPropertySubOwner(_colorSettings);

    if (p.fading.has_value()) {
        addPropertySubOwner(_fading);
    }

    if (p.coloring.has_value() && (*p.coloring).colorMapping.has_value()) {
        _hasColorMapFile = true;

        _colorSettings.colorMapping->dataColumn.onChange(
            [this]() { _dataIsDirty = true; }
        );

        _colorSettings.colorMapping->setRangeFromData.onChange([this]() {
            int parameterIndex = currentColorParameterIndex();
            _colorSettings.colorMapping->valueRange = _dataset.findValueRange(
                parameterIndex
            );
        });

        _colorSettings.colorMapping->colorMapFile.onChange([this]() {
            _dataIsDirty = true;
            _hasColorMapFile = std::filesystem::exists(
                _colorSettings.colorMapping->colorMapFile.value()
            );
        });
    }

    if (_hasDataFile) {
        bool useCaching = p.useCaching.value_or(true);
        if (useCaching) {
            _dataset = dataloader::data::loadFileWithCache(_dataFile, _dataMapping);
        }
        else {
            _dataset = dataloader::data::loadFile(_dataFile, _dataMapping);
        }
        _nDataPoints = static_cast<unsigned int>(_dataset.entries.size());

        // If no scale exponent was specified, compute one that will at least show the
        // points based on the scale of the positions in the dataset
        if (!p.sizeSettings.has_value() || !p.sizeSettings->scaleExponent.has_value()) {
            double dist = _dataset.maxPositionComponent * toMeter(_unit);
            if (dist > 0.0) {
                float exponent = static_cast<float>(std::log10(dist));
                // Reduce the actually used exponent a little bit, as just using the
                // logarithm as is leads to very large points
                _sizeSettings.scaleExponent = 0.9f * exponent;
            }
        }
    }

    if (p.labels.has_value()) {
        if (!p.labels->hasKey("File") && _hasDataFile) {
            // Load the labelset from the dataset if no file was included
            _labels = std::make_unique<LabelsComponent>(*p.labels, _dataset, _unit);
        }
        else {
            _labels = std::make_unique<LabelsComponent>(*p.labels);
        }

        _hasLabels = true;
        addPropertySubOwner(_labels.get());
        // Fading of the labels should also depend on the fading of the renderable
        _labels->setParentFadeable(this);
    }

    _nDataPoints.setReadOnly(true);
    addProperty(_nDataPoints);
}

bool RenderablePointCloud::isReady() const {
    bool isReady = _program;

    // If we have labels, they also need to be loaded
    if (_hasLabels) {
        isReady = isReady && _labels->isReady();
    }
    return isReady;
}

void RenderablePointCloud::initialize() {
    ZoneScoped;

    if (_hasDataFile && _hasColorMapFile) {
        _colorSettings.colorMapping->initialize(_dataset);
    }

    if (_hasLabels) {
        _labels->initialize();
    }
}

void RenderablePointCloud::initializeGL() {
    ZoneScoped;

    initializeShadersAndGlExtras();

    ghoul::opengl::updateUniformLocations(*_program, _uniformCache, UniformNames);

    switch (_textureMode) {
        case TextureInputMode::Single:
            initializeSingleTexture();
            break;
        case TextureInputMode::Multi:
            initializeMultiTextures();
            break;
        case TextureInputMode::Other:
            initializeCustomTexture();
            break;
        default:
            break;
    }
}

void RenderablePointCloud::deinitializeGL() {
    glDeleteBuffers(1, &_vbo);
    _vbo = 0;
    glDeleteVertexArrays(1, &_vao);
    _vao = 0;

    deinitializeShaders();

    // Unload texture arrays from GPU memory
    for (const TextureArrayInfo& arrayInfo : _textureArrays) {
        glDeleteTextures(1, &arrayInfo.renderId);
    }
}

void RenderablePointCloud::initializeShadersAndGlExtras() {
    _program = BaseModule::ProgramObjectManager.request(
        "RenderablePointCloud",
        []() {
            return global::renderEngine->buildRenderProgram(
                "RenderablePointCloud",
                absPath("${MODULE_BASE}/shaders/pointcloud/billboardpoint_vs.glsl"),
                absPath("${MODULE_BASE}/shaders/pointcloud/billboardpoint_fs.glsl"),
                absPath("${MODULE_BASE}/shaders/pointcloud/billboardpoint_gs.glsl")
            );
        }
    );
}

void RenderablePointCloud::deinitializeShaders() {
    BaseModule::ProgramObjectManager.release(
        "RenderablePointCloud",
        [](ghoul::opengl::ProgramObject* p) {
            global::renderEngine->removeRenderProgram(p);
        }
    );
    _program = nullptr;
}

void RenderablePointCloud::initializeCustomTexture() {}

void RenderablePointCloud::initializeSingleTexture() {
    if (!_hasSpriteTexture || _spriteTexturePath.value().empty()) {
        return;
    }

    std::filesystem::path p = absPath(_spriteTexturePath);

    if (!std::filesystem::is_regular_file(p)) {
        throw ghoul::RuntimeError(fmt::format(
            "Could not find image file '{}'", _spriteTexturePath.value()
        ));
    }

    // Single tectures may be updated => make sure to clear the data structures
    // before updating them
    clearTextureDataStructures();

    loadTexture(p, 0);

    generateArrayTextures();

    _spriteTextureIsDirty = false;
}

void RenderablePointCloud::initializeMultiTextures() {
    for (const dataloader::Dataset::Texture& tex : _dataset.textures) {
        std::filesystem::path fullPath = _texturesDirectory / tex.file;
        // TODO: Remove this change to png extension... (Requires updating the dataset)
        std::filesystem::path pngPath = fullPath;
        pngPath.replace_extension(".png");

        std::filesystem::path path;
        if (std::filesystem::is_regular_file(fullPath)) {
            path = fullPath;
        }
        else if (std::filesystem::is_regular_file(pngPath)) {
            path = pngPath;
        }
        else {
            // We can't really recover from this as it would crash during rendering anyway
            throw ghoul::RuntimeError(fmt::format(
                "Could not find image file '{}'", path
            ));
        }
        loadTexture(path, tex.index);
    }

    generateArrayTextures();
}

void RenderablePointCloud::clearTextureDataStructures() {
    _textures.clear();
    _textureMapByFormat.clear();
    // Unload texture arrays from GPU memory
    for (const TextureArrayInfo& arrayInfo : _textureArrays) {
        glDeleteTextures(1, &arrayInfo.renderId);
    }
    _textureArrays.clear();
    _textureIdToArrayMap.clear();
}

void RenderablePointCloud::loadTexture(const std::filesystem::path& path, int index) {
    if (path.empty()) {
        return;
    }

    // @TODO: Make sure we don't load the same texture twice... That we use the texture manager somehow..?
    std::unique_ptr<ghoul::opengl::Texture> t =
        ghoul::io::TextureReader::ref().loadTexture(path.string(), 2);

    TextureColorMode colorMode; // TODO: Make possible to set from asset
    if (t->numberOfChannels() == 1) {
        // TODO: OBS!  This results in only red colors in the shader... Remove?
        colorMode = TextureColorMode::Greyscale;
    }
    else if (t->numberOfChannels() > 3) {
        colorMode = TextureColorMode::Transparency;
    }
    else {
        colorMode = TextureColorMode::OpaqueColor;
    }

    if (t) {
        LINFOC("RenderablePlanesCloud", fmt::format("Loaded texture {}", path));
        // Do not upload the loaded texture to the GPU, we just want it to hold the data.
        // However, convert textures make sure they all use the same format.
        ghoul::opengl::Texture::Format targetFormat = glFormatFromColorMode(colorMode);
        convertTextureFormat(*t, targetFormat);
    }
    else {
        throw ghoul::RuntimeError(fmt::format(
            "Could not find image file {}", path
        ));
    }

    glm::uvec2 res = glm::uvec2(t->width(), t->height());
    TextureFormat format = {
        .resolution = res,
        .colorMode = colorMode
    };
    _textureMapByFormat[format].push_back(index);

    _textures.insert(std::pair(index, std::move(t)));
}

void RenderablePointCloud::initAndAllocateTextureArray(unsigned int textureId,
                                                       glm::uvec2 resolution,
                                                       size_t nLayers,
                                                       TextureColorMode colorMode)
{
    _textureArrays.push_back({ .renderId = textureId });

    gl::GLenum internalFormat = internalGlFormatFromColorMode(colorMode);

    // Create storage for the texture
    glTexStorage3D(
        GL_TEXTURE_2D_ARRAY,
        1, // No mipmaps
        internalFormat,
        resolution.x,
        resolution.y,
        static_cast<gl::GLsizei>(nLayers)
    );

    // @TODO: This function should probably be used instead, as glTexStorage is only
    // available in OpenGl 4.2 and above
    //glTexImage3D(
    //    GL_TEXTURE_2D_ARRAY,
    //    1, // No mipmaps
    //    internalFormat,
    //    0,
    //    GL_RGBA8, // internal format @TODO
    //    resolution.x,
    //    resolution.y,
    //    static_cast<gl::GLsizei>(nLayers)
    //    static_cast<gl::GLsizei>(nLayers),
    //    0,
    //    GL_RGBA, // format @TODO
    //    GL_UNSIGNED_BYTE,
    //    nullptr
    //);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void RenderablePointCloud::fillAndUploadTextureLayer(unsigned int arrayIndex,
                                                     unsigned int layer,
                                                     unsigned int textureId,
                                                     glm::uvec2 resolution,
                                                     TextureColorMode colorMode,
                                                     const void* pixelData)
{
    gl::GLenum format = gl::GLenum(glFormatFromColorMode(colorMode));

    glTexSubImage3D(
        GL_TEXTURE_2D_ARRAY,
        0, // Mipmap number
        0, // xoffset
        0, // yoffset
        gl::GLint(layer), // zoffset
        gl::GLsizei(resolution.x), // width
        gl::GLsizei(resolution.y), // height
        1, // depth
        format,
        GL_UNSIGNED_BYTE, // type
        pixelData
    );

    // Keept track of the layer for this id, so we can use it when generating vertex data
    _textureIdToArrayMap[textureId] = { .arrayId = arrayIndex, .layer = layer };
}

void RenderablePointCloud::generateArrayTextures() {
    _textureArrays.reserve(_textureMapByFormat.size());

    using Entry = std::pair<TextureFormat, std::vector<int>>;
    unsigned int arrayIndex = 0;
    for (const Entry& e : _textureMapByFormat) {
        glm::uvec2 res = e.first.resolution;
        TextureColorMode colorMode = e.first.colorMode;
        size_t nLayers = e.second.size();

        // And and create storage for texture (bind the texture for writing)
        // Generate an array texture
        unsigned int id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D_ARRAY, id);

        initAndAllocateTextureArray(id, res, nLayers, colorMode);

        // Fill that storage with the data from the individual textures
        unsigned int layer = 0;
        for (const int& texId : e.second) {
            const ghoul::opengl::Texture* texture = _textures[texId].get();
            fillAndUploadTextureLayer(arrayIndex, layer, texId, res, colorMode, texture->pixelData());
            layer++;
        }

        // @TODO: Potentially use GL_MAX_ARRAY_TEXTURE_LAYERS to split up an array if it
        // contains too many layers

        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        arrayIndex++;
    }
}

float RenderablePointCloud::computeDistanceFadeValue(const RenderData& data) const {
    if (!_fading.enabled) {
        return 1.f;
    }

    float fadeValue = 1.f;
    glm::dmat4 invModelMatrix = glm::inverse(calcModelTransform(data));

    glm::dvec3 cameraPosModelSpace = glm::dvec3(
        invModelMatrix * glm::dvec4(data.camera.positionVec3(), 1.0)
    );

    float distCamera = static_cast<float>(
        glm::length(cameraPosModelSpace) / toMeter(_unit)
    );

    const glm::vec2 fadeRange = _fading.fadeInDistances;
    const float fadeRangeWidth = (fadeRange.y - fadeRange.x);
    float funcValue = (distCamera - fadeRange.x) / fadeRangeWidth;
    funcValue = glm::clamp(funcValue, 0.f, 1.f);

    if (_fading.invert) {
        funcValue = 1.f - funcValue;
    }

    return fadeValue * funcValue;
}

void RenderablePointCloud::setExtraUniforms() {}

void RenderablePointCloud::renderBillboards(const RenderData& data,
                                            const glm::dmat4& modelMatrix,
                                            const glm::dvec3& orthoRight,
                                            const glm::dvec3& orthoUp,
                                            float fadeInVariable)
{
    if (!_hasDataFile || _dataset.entries.empty()) {
        return;
    }

    glEnablei(GL_BLEND, 0);

    if (_useAdditiveBlending) {
        glDepthMask(false);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }
    else {
        // Normal blending, with transparency
        glDepthMask(true);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    _program->activate();

    _program->setUniform(_uniformCache.cameraPos, data.camera.positionVec3());
    _program->setUniform(
        _uniformCache.cameraLookup,
        glm::vec3(data.camera.lookUpVectorWorldSpace())
    );
    _program->setUniform(_uniformCache.renderOption, _renderOption.value());
    _program->setUniform(_uniformCache.modelMatrix, modelMatrix);

    _program->setUniform(
        _uniformCache.cameraViewMatrix,
        data.camera.combinedViewMatrix()
    );

    _program->setUniform(
        _uniformCache.projectionMatrix,
        glm::dmat4(data.camera.projectionMatrix())
    );

    _program->setUniform(_uniformCache.up, glm::vec3(orthoUp));
    _program->setUniform(_uniformCache.right, glm::vec3(orthoRight));
    _program->setUniform(_uniformCache.fadeInValue, fadeInVariable);

    _program->setUniform(_uniformCache.renderOption, _renderOption.value());
    _program->setUniform(_uniformCache.opacity, opacity());

    _program->setUniform(_uniformCache.scaleExponent, _sizeSettings.scaleExponent);
    _program->setUniform(_uniformCache.scaleFactor, _sizeSettings.scaleFactor);
    _program->setUniform(
        _uniformCache.enableMaxSizeControl,
        _sizeSettings.useMaxSizeControl
    );
    _program->setUniform(_uniformCache.maxAngularSize, _sizeSettings.maxAngularSize);

    bool useSizeMapping = _hasDatavarSize && _sizeSettings.sizeMapping &&
        _sizeSettings.sizeMapping->enabled;

    _program->setUniform(_uniformCache.hasDvarScaling, useSizeMapping);

    if (useSizeMapping) {
        _program->setUniform(
            _uniformCache.dvarScaleFactor,
            _sizeSettings.sizeMapping->scaleFactor
        );
    }

    _program->setUniform(_uniformCache.color, _colorSettings.pointColor);

    bool useColorMap = _hasColorMapFile && _colorSettings.colorMapping->enabled &&
        _colorSettings.colorMapping->texture();

    _program->setUniform(_uniformCache.useColormap, useColorMap);

    ghoul::opengl::TextureUnit colorMapTextureUnit;
    _program->setUniform(_uniformCache.colorMapTexture, colorMapTextureUnit);

    if (useColorMap) {
        colorMapTextureUnit.activate();
        _colorSettings.colorMapping->texture()->bind();

        const glm::vec2 range = _colorSettings.colorMapping->valueRange;
        _program->setUniform(_uniformCache.cmapRangeMin, range.x);
        _program->setUniform(_uniformCache.cmapRangeMax, range.y);
        _program->setUniform(
            _uniformCache.hideOutsideRange,
            _colorSettings.colorMapping->hideOutsideRange
        );

        _program->setUniform(
            _uniformCache.nanColor,
            _colorSettings.colorMapping->nanColor
        );
        _program->setUniform(
            _uniformCache.useNanColor,
            _colorSettings.colorMapping->useNanColor
        );

        _program->setUniform(
            _uniformCache.aboveRangeColor,
            _colorSettings.colorMapping->aboveRangeColor
        );
        _program->setUniform(
            _uniformCache.useAboveRangeColor,
            _colorSettings.colorMapping->useAboveRangeColor
        );

        _program->setUniform(
            _uniformCache.belowRangeColor,
            _colorSettings.colorMapping->belowRangeColor
        );
        _program->setUniform(
            _uniformCache.useBelowRangeColor,
            _colorSettings.colorMapping->useBelowRangeColor
        );
    }

    bool useTexture = _hasSpriteTexture && _useSpriteTexture;
    _program->setUniform(_uniformCache.hasSpriteTexture, useTexture);

    ghoul::opengl::TextureUnit spriteTextureUnit;
    _program->setUniform(_uniformCache.spriteTexture, spriteTextureUnit);

    setExtraUniforms();

    glBindVertexArray(_vao);

    if (useTexture && !_textureArrays.empty()) {
        spriteTextureUnit.activate();
        for (const TextureArrayInfo& arrayInfo : _textureArrays) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, arrayInfo.renderId);
            glDrawArrays(
                GL_POINTS,
                arrayInfo.startOffset,
                static_cast<GLsizei>(arrayInfo.nPoints)
            );
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
    else {
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(_nDataPoints));
    }

    glBindVertexArray(0);
    _program->deactivate();

    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetDepthState();
}

void RenderablePointCloud::render(const RenderData& data, RendererTasks&) {
    float fadeInVar = computeDistanceFadeValue(data);

    if (fadeInVar < 0.01f) {
        return;
    }

    glm::dmat4 modelMatrix = calcModelTransform(data);

    glm::dvec3 cameraViewDirectionWorld = -data.camera.viewDirectionWorldSpace();
    glm::dvec3 cameraUpDirectionWorld = data.camera.lookUpVectorWorldSpace();
    glm::dvec3 orthoRight = glm::normalize(
        glm::cross(cameraUpDirectionWorld, cameraViewDirectionWorld)
    );
    if (orthoRight == glm::dvec3(0.0)) {
        glm::dvec3 otherVector = glm::vec3(
            cameraUpDirectionWorld.y,
            cameraUpDirectionWorld.x,
            cameraUpDirectionWorld.z
        );
        orthoRight = glm::normalize(glm::cross(otherVector, cameraViewDirectionWorld));
    }
    glm::dvec3 orthoUp = glm::normalize(glm::cross(cameraViewDirectionWorld, orthoRight));

    if (_hasDataFile && _drawElements) {
        renderBillboards(data, modelMatrix, orthoRight, orthoUp, fadeInVar);
    }

    if (_hasLabels) {
        glm::dmat4 modelViewProjectionMatrix =
            calcModelViewProjectionTransform(data, modelMatrix);

        _labels->render(data, modelViewProjectionMatrix, orthoRight, orthoUp, fadeInVar);
    }
}

void RenderablePointCloud::preUpdate() {}

void RenderablePointCloud::update(const UpdateData&) {
    ZoneScoped;

    preUpdate();

    if (_hasColorMapFile) {
        _colorSettings.colorMapping->update(_dataset);
    }

    if (_dataIsDirty) {
        updateBufferData();
    }

    if ((_textureMode == TextureInputMode::Single) && _spriteTextureIsDirty) {
        updateSpriteTexture();
    }
}

glm::dvec3 RenderablePointCloud::transformedPosition(
                                                const dataloader::Dataset::Entry& e) const
{
    const double unitMeter = toMeter(_unit);
    glm::dvec4 position = glm::dvec4(glm::dvec3(e.position) * unitMeter, 1.0);
    return glm::dvec3(_transformationMatrix * position);
}

int RenderablePointCloud::nAttributesPerPoint() const {
    int n = 3; // position
    n += _hasColorMapFile ? 1 : 0;
    n += _hasDatavarSize ? 1 : 0;
    n += _hasSpriteTexture ? 1 : 0; // texture id
    return n;
}

int RenderablePointCloud::bufferVertexAttribute(const std::string& name, GLint nValues,
                                                 int nAttributesPerPoint, int offset) const
{
    GLint attrib = _program->attributeLocation(name);
    glEnableVertexAttribArray(attrib);
    glVertexAttribPointer(
        attrib,
        nValues,
        GL_FLOAT,
        GL_FALSE,
        nAttributesPerPoint * sizeof(float),
        (offset > 0) ? reinterpret_cast<void*>(offset * sizeof(float)) : nullptr
    );

    return offset + nValues;
}

void RenderablePointCloud::updateBufferData() {
    if (!_hasDataFile || _dataset.entries.empty()) {
        return;
    }

    ZoneScopedN("Data dirty");
    TracyGpuZone("Data dirty");
    LDEBUG("Regenerating data");

    std::vector<float> slice = createDataSlice();

    int size = static_cast<int>(slice.size());

    if (_vao == 0) {
        glGenVertexArrays(1, &_vao);
        LDEBUG(fmt::format("Generating Vertex Array id '{}'", _vao));
    }
    if (_vbo == 0) {
        glGenBuffers(1, &_vbo);
        LDEBUG(fmt::format("Generating Vertex Buffer Object id '{}'", _vbo));
    }

    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, size * sizeof(float), slice.data(), GL_STATIC_DRAW);

    const int attibutesPerPoint = nAttributesPerPoint();
    int offset = 0;

    offset = bufferVertexAttribute("in_position", 3, attibutesPerPoint, offset);

    if (_hasColorMapFile) {
        offset = bufferVertexAttribute("in_colorParameter", 1, attibutesPerPoint, offset);
    }

    if (_hasDatavarSize) {
        offset = bufferVertexAttribute("in_scalingParameter", 1, attibutesPerPoint, offset);
    }

    if (_hasSpriteTexture) {
        offset = bufferVertexAttribute("in_textureLayer", 1, attibutesPerPoint, offset);
    }

    glBindVertexArray(0);

    _dataIsDirty = false;
}

void RenderablePointCloud::updateSpriteTexture() {
    bool shouldUpdate = _hasSpriteTexture && _spriteTextureIsDirty &&
        !_spriteTexturePath.value().empty();

    if (!shouldUpdate) {
        return;
    }

    ZoneScopedN("Sprite texture");
    TracyGpuZone("Sprite texture");

    initializeSingleTexture();
}

int RenderablePointCloud::currentColorParameterIndex() const {
    const properties::OptionProperty& property =
        _colorSettings.colorMapping->dataColumn;

    if (!_hasColorMapFile || property.options().empty()) {
        return 0;
    }

    return _dataset.index(property.option().description);
}

int RenderablePointCloud::currentSizeParameterIndex() const {
    const properties::OptionProperty& property =
        _sizeSettings.sizeMapping->parameterOption;

    if (!_hasDatavarSize || property.options().empty()) {
        return 0;
    }

    return _dataset.index(property.option().description);
}

void RenderablePointCloud::addPositionDataForPoint(unsigned int index,
                                                   std::vector<float> &result,
                                                   double& maxRadius) const
{
    const dataloader::Dataset::Entry& e = _dataset.entries[index];
    glm::dvec3 position = transformedPosition(e);
    const double r = glm::length(position);

    // Add values to result
    for (int j = 0; j < 3; ++j) {
        result.push_back(static_cast<float>(position[j]));
    }

    maxRadius = std::max(maxRadius, r);
}

void RenderablePointCloud::addColorAndSizeDataForPoint(unsigned int index,
                                                       std::vector<float>& result) const
{
    const dataloader::Dataset::Entry& e = _dataset.entries[index];

    // Colors
    int colorParamIndex = currentColorParameterIndex();
    if (_hasColorMapFile && colorParamIndex > -1) {
        result.push_back(e.data[colorParamIndex]);
    }

    // Size data
    int sizeParamIndex = currentSizeParameterIndex();
    if (_hasDatavarSize && sizeParamIndex > -1) {
        // @TODO: Consider more detailed control over the scaling. Currently the value
        // is multiplied with the value as is. Should have similar mapping properties
        // as the color mapping
        result.push_back(e.data[sizeParamIndex]);
    }
}

std::vector<float> RenderablePointCloud::createDataSlice() {
    ZoneScoped;

    if (_dataset.entries.empty()) {
        return std::vector<float>();
    }

    // Whgat datavar is the texture, if any
    int textureIdIndex = _dataset.textureDataIndex;

    double maxRadius = 0.0;

    // One sub-array per texture array, since each of these will correspond to a separate
    // draw call. We need at least one sub result array
    std::vector<std::vector<float>> subResults = std::vector<std::vector<float>>(
        (_textureArrays.size() > 0) ? _textureArrays.size() : 1
    );

    // Reserve enough space for all points in each for now
    for (std::vector<float>& subres : subResults) {
        subres.reserve(nAttributesPerPoint() * _dataset.entries.size());
    }

    for (unsigned int i = 0; i < _nDataPoints; i++) {
        const dataloader::Dataset::Entry& e = _dataset.entries[i];

        unsigned int subresultIndex = 0;
        float textureLayer = 0.f;

        bool useMultiTexture = (_textureMode == TextureInputMode::Multi) &&
            (textureIdIndex > -1);

        if (_hasSpriteTexture && useMultiTexture) {
            int texId = static_cast<int>(e.data[textureIdIndex]);
            textureLayer = static_cast<float>(
                _textureIdToArrayMap[texId].layer
            );
            subresultIndex = _textureIdToArrayMap[texId].arrayId;
        }

        std::vector<float>& subArrayToUse = subResults[subresultIndex];

        // Add position, color and size data (subclasses may compute these differently)
        addPositionDataForPoint(i, subArrayToUse, maxRadius);
        addColorAndSizeDataForPoint(i, subArrayToUse);

        // Texture layer
        if (_hasSpriteTexture) {
            subArrayToUse.push_back(static_cast<float>(textureLayer));
        }
    }

    for (std::vector<float>& subres : subResults) {
        subres.shrink_to_fit();
    }

    // Combine subresults, which should be in same order as texture arrays
    std::vector<float> result;
    result.reserve(nAttributesPerPoint() * _dataset.entries.size());
    size_t vertexCount = 0;
    for (size_t i = 0; i < subResults.size(); ++i) {
        result.insert(result.end(), subResults[i].begin(), subResults[i].end());
        int nVertices = static_cast<int>(subResults[i].size()) / nAttributesPerPoint();
        if (_textureArrays.size() > 0) {
            _textureArrays[i].nPoints = nVertices;
            _textureArrays[i].startOffset = static_cast<GLint>(vertexCount);
        }
        vertexCount += nVertices;
    }

    setBoundingSphere(maxRadius);
    return result;
}


gl::GLenum RenderablePointCloud::internalGlFormatFromColorMode(
                                                              TextureColorMode mode) const
{
    // TODO: investigate compressed formats
    switch (mode) {
        case TextureColorMode::Transparency:
            return GL_RGBA8;
        case TextureColorMode::OpaqueColor:
            return GL_RGB8;
        case TextureColorMode::Greyscale:
            // TODO: OBS!  This results in only red colors in the shader...
            return GL_R8;
        default:
            return GL_RGB8;
    }
}

ghoul::opengl::Texture::Format RenderablePointCloud::glFormatFromColorMode(
                                                              TextureColorMode mode) const
{
    switch (mode) {
        case TextureColorMode::Transparency:
            return ghoul::opengl::Texture::Format::RGBA;
        case TextureColorMode::OpaqueColor:
            return ghoul::opengl::Texture::Format::RGB;
        case TextureColorMode::Greyscale:
            return ghoul::opengl::Texture::Format::Red;
        default:
            return ghoul::opengl::Texture::Format::RGB;
    }
}

} // namespace openspace
