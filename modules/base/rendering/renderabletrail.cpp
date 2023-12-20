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

#include <modules/base/rendering/renderabletrail.h>

#include <modules/base/basemodule.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/translation.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/misc/profiling.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>
#include <cmath>
#include <optional>

namespace {
#ifdef __APPLE__
    constexpr std::array<const char*, 15> UniformNames = {
        "opacity", "modelViewTransform", "projectionTransform", "color", "useLineFade",
        "lineFade", "vertexSortingMethod", "idOffset", "nVertices", "stride", "pointSize",
        "renderPhase", "useSplitRenderMode", "numberOfUniqueVertices", "floatingOffset"
    };
#else
    constexpr std::array<const char*, 17> UniformNames = {
        "opacity", "modelViewTransform", "projectionTransform", "color", "useLineFade",
        "lineFade", "vertexSortingMethod", "idOffset", "nVertices", "stride", "pointSize",
        "renderPhase", "viewport", "lineWidth", "useSplitRenderMode", "numberOfUniqueVertices", "floatingOffset"
    };
#endif

    // The possible values for the _renderingModes property
    enum RenderingMode {
        RenderingModeLines = 0,
        RenderingModePoints,
        RenderingModeLinesPoints
    };

    // Fragile! Keep in sync with documentation
    const std::map<std::string, RenderingMode> RenderingModeConversion = {
        { "Lines", RenderingModeLines },
        { "Points", RenderingModePoints },
        { "Lines+Points", RenderingModeLinesPoints },
        { "Points+Lines", RenderingModeLinesPoints }
    };

    constexpr openspace::properties::Property::PropertyInfo LineColorInfo = {
        "Color",
        "Color",
        "This value determines the RGB main color for the lines and points of the trail",
        // @VISIBILITY(1.2)
        openspace::properties::Property::Visibility::NoviceUser
    };

    constexpr openspace::properties::Property::PropertyInfo EnableFadeInfo = {
        "EnableFade",
        "Enable line fading of old points",
        "Toggles whether the trail should fade older points out. If this value is "
        "'true', the 'Fade' parameter determines the speed of fading. If this value is "
        "'false', the entire trail is rendered at full opacity and color",
        // @VISIBILITY(1.25)
        openspace::properties::Property::Visibility::NoviceUser
    };

    enum FadingMode {
        LineFade = 0,
        LineFadeStartPointDuration,
        LineFadeEndPointDuration
    };

    constexpr openspace::properties::Property::PropertyInfo LineFadeInfo = {
        "LineFade",
        "Point + Point",
        "The fading factor that is applied to the trail if the 'EnableFade' value is "
        "'true'. If it is 'false', this setting has no effect. --- IN PROGRESS ---",
        // @VISIBILITY(2.5)
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo LineFadeStarPointDurationInfo = {
    "LineFadeStartPointDuration",
    "Startpoint + Range",
    "The fading factor that is applied to the trail if the 'EnableFade' value is "
    "'true'. If it is 'false', this setting has no effect. --- IN PROGRESS ---",
        // @VISIBILITY(2.5)
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo LineFadeEndPointDurationInfo = {
    "LineFadeEndPointDuration",
    "EndPoint + Range",
    "The fading factor that is applied to the trail if the 'EnableFade' value is "
    "'true'. If it is 'false', this setting has no effect. --- IN PROGRESS ---",
        // @VISIBILITY(2.5)
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo LineWidthInfo = {
        "LineWidth",
        "Line Width",
        "This value specifies the line width of the trail if the selected rendering "
        "method includes lines. If the rendering mode is set to Points, this value is "
        "ignored",
        // @VISIBILITY(2.4)
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo PointSizeInfo = {
        "PointSize",
        "Point Size",
        "This value specifies the base size of the points along the line if the selected "
        "rendering method includes points. If the rendering mode is set the Lines, this "
        "value is ignored. If a subsampling of the values is performed, the subsampled "
        "values are half this size",
        // @VISIBILITY(2.4)
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo RenderingModeInfo = {
        "Rendering",
        "Rendering Mode",
        "Determines how the trail should be rendered to the screen. If 'Lines' is "
        "selected, only the line part is visible, if 'Points' is selected, only the "
        "corresponding points (and subpoints) are shown. 'Lines+Points' shows both parts",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    // TEMP GOES HERE =======================

    constexpr openspace::properties::Property::PropertyInfo FadeModeInfo = {
        "FadeMode",
        "Fade Mode",
        "Selects fading mode for trails",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    //=======================================

    struct [[codegen::Dictionary(RenderableTrail)]] Parameters {
        // This object is used to compute locations along the path. Any Translation object
        // can be used here
        ghoul::Dictionary translation
            [[codegen::reference("core_transform_translation")]];

        // [[codegen::verbatim(LineColorInfo.description)]]
        glm::vec3 color [[codegen::color()]];

        // [[codegen::verbatim(EnableFadeInfo.description)]]
        std::optional<bool> enableFade;

        // [[codegen::verbatim(LineFadeInfo.description)]]
        std::optional<glm::vec2> lineFade;

        // [[codegen::verbatim(LineWidthInfo.description)]]
        std::optional<float> lineWidth;

        // [[codegen::verbatim(PointSizeInfo.description)]]
        std::optional<int> pointSize;

        enum class RenderingMode {
            Lines,
            Points,
            LinesPoints [[codegen::key("Lines+Points")]],
            PointsLines [[codegen::key("Lines+Points")]]
        };
        // [[codegen::verbatim(RenderingModeInfo.description)]]
        std::optional<RenderingMode> renderingMode [[codegen::key("Rendering")]];

        // TEMP GOES HERE ===================
        enum class FadeMode {
            PointPoint [[codegen::key("Point+Point")]],
            StartPointDuration [[codegen::key("StartPoint+Range")]],
            DurationEndPoint [[codegen::key("EndPoint+Range")]]
        };
        //std::optional<FadeMode> fadeMode [[codegen::key("FadeMode")]];

        // ==================================
    };
#include "renderabletrail_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation RenderableTrail::Documentation() {
    return codegen::doc<Parameters>("base_renderable_renderabletrail");
}

RenderableTrail::Appearance::Appearance()
    : properties::PropertyOwner({
        "Appearance",
        "Appearance",
        "The appearance of the trail"
    })
    , lineColor(LineColorInfo, glm::vec3(1.f), glm::vec3(0.f), glm::vec3(1.f))
    , useLineFade(EnableFadeInfo, true)
    , lineFade(LineFadeInfo, glm::vec2(0.f, 1.f), glm::vec2(0.0f), glm::vec2(1.0f))
    , lineWidth(LineWidthInfo, 10.f, 1.f, 20.f)
    , pointSize(PointSizeInfo, 1, 1, 64)
    , renderingModes(
          RenderingModeInfo,
          properties::OptionProperty::DisplayType::Dropdown
    )
    , fadingModes(
        FadeModeInfo,
        properties::OptionProperty::DisplayType::Dropdown
    )
    , lineFadeStarPointDuration(LineFadeStarPointDurationInfo, glm::vec2(0.f, 1.f), glm::vec2(0.f), glm::vec2(1.f))
    , lineFadeEndPointDuration(LineFadeEndPointDurationInfo, glm::vec2(1.f, 1.f), glm::vec2(0.f), glm::vec2(1.f))
{
    renderingModes.addOptions({
        { RenderingModeLines, "Lines" },
        { RenderingModePoints, "Points" },
        { RenderingModeLinesPoints, "Lines+Points" }
    });

    // TEMP GOES HERE =================
    fadingModes.addOptions({
        { LineFade, "Point+Point" },
        { LineFadeStartPointDuration, "StartPoint+Range" },
        { LineFadeEndPointDuration, "EndPoint+Range" }
    });
    //=================================

    lineColor.setViewOption(properties::Property::ViewOptions::Color);
    addProperty(lineColor);
    addProperty(lineWidth);
    addProperty(pointSize);
    addProperty(renderingModes);
    addProperty(useLineFade);

    // TEMP
    lineFade.setViewOption(properties::Property::ViewOptions::MinMaxRange);

    addProperty(fadingModes);
    addProperty(lineFade);
    addProperty(lineFadeStarPointDuration);
    addProperty(lineFadeEndPointDuration);
}

RenderableTrail::RenderableTrail(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    setRenderBin(RenderBin::Overlay);
    addProperty(Fadeable::_opacity);

    _translation = Translation::createFromDictionary(
        dictionary.value<ghoul::Dictionary>("Translation")
    );
    addPropertySubOwner(_translation.get());

    _appearance.lineColor = p.color;
    _appearance.useLineFade = p.enableFade.value_or(_appearance.useLineFade);
    _appearance.lineFade = p.lineFade.value_or(_appearance.lineFade);
    _appearance.lineWidth = p.lineWidth.value_or(_appearance.lineWidth);
    _appearance.pointSize = p.pointSize.value_or(_appearance.pointSize);

    if (p.renderingMode.has_value()) {
        switch (*p.renderingMode) {
            case Parameters::RenderingMode::Lines:
                _appearance.renderingModes = RenderingModeLines;
                break;
            case Parameters::RenderingMode::Points:
                _appearance.renderingModes = RenderingModePoints;
                break;
            case Parameters::RenderingMode::LinesPoints:
            case Parameters::RenderingMode::PointsLines:
                _appearance.renderingModes = RenderingModeLinesPoints;
                break;
        }
    }
    else {
        _appearance.renderingModes = RenderingModeLines;
    }

    addPropertySubOwner(_appearance);

}

void RenderableTrail::initializeGL() {
    ZoneScoped;

#ifdef __APPLE__
    _programObject = BaseModule::ProgramObjectManager.request(
        "EphemerisProgram",
        []() -> std::unique_ptr<ghoul::opengl::ProgramObject> {
            return global::renderEngine->buildRenderProgram(
                "EphemerisProgram",
                absPath("${MODULE_BASE}/shaders/renderabletrail_apple_vs.glsl"),
                absPath("${MODULE_BASE}/shaders/renderabletrail_apple_fs.glsl")
            );
        }
    );
#else
    _programObject = BaseModule::ProgramObjectManager.request(
        "EphemerisProgram",
        []() -> std::unique_ptr<ghoul::opengl::ProgramObject> {
            return global::renderEngine->buildRenderProgram(
                "EphemerisProgram",
                absPath("${MODULE_BASE}/shaders/renderabletrail_vs.glsl"),
                absPath("${MODULE_BASE}/shaders/renderabletrail_fs.glsl")
            );
        }
    );
#endif

    ghoul::opengl::updateUniformLocations(*_programObject, _uniformCache, UniformNames);
}

void RenderableTrail::deinitializeGL() {
    BaseModule::ProgramObjectManager.release(
        "EphemerisProgram",
        [](ghoul::opengl::ProgramObject* p) {
            global::renderEngine->removeRenderProgram(p);
        }
    );
    _programObject = nullptr;
}

bool RenderableTrail::isReady() const {
    return _programObject != nullptr;
}

void RenderableTrail::internalRender(bool renderLines, bool renderPoints,
                                     const RenderData& data,
                                     const glm::dmat4& modelTransform,
                                     RenderInformation& info, int nVertices, int ringOffset,
                                     bool useSplitRenderMode, int numberOfUniqueVertices, int floatingOffset)
{
    ZoneScoped;

    // We pass in the model view transformation matrix as double in order to maintain
    // high precision for vertices; especially for the trails, a high vertex precision
    // is necessary as they are usually far away from their reference
    glm::dmat4 modelViewTransform = calcModelViewTransform(data, modelTransform);
    _programObject->setUniform(
        _uniformCache.modelView,
        modelViewTransform * info._localTransform
    );

    const int sorting = [](RenderInformation::VertexSorting s) {
        switch (s) {
            case RenderInformation::VertexSorting::NewestFirst: return 0;
            case RenderInformation::VertexSorting::OldestFirst: return 1;
            case RenderInformation::VertexSorting::NoSorting:   return 2;
            default:                                  throw ghoul::MissingCaseException();
        }
    }(info.sorting);

    // The vertex sorting method is used to tweak the fading along the trajectory
    _programObject->setUniform(_uniformCache.vertexSorting, sorting);

    // This value is subtracted from the vertex id in the case of a potential ring
    // buffer (as used in RenderableTrailOrbit) to keep the first vertex at its
    // brightest
    _programObject->setUniform(_uniformCache.idOffset, ringOffset);

    _programObject->setUniform(_uniformCache.nVertices, nVertices);
    _programObject->setUniform(_uniformCache.useSplitRenderMode, useSplitRenderMode);
    _programObject->setUniform(_uniformCache.numberOfUniqueVertices, numberOfUniqueVertices);
    _programObject->setUniform(_uniformCache.floatingOffset, floatingOffset);

#if !defined(__APPLE__)
    GLint viewport[4];
    global::renderEngine->openglStateCache().viewport(viewport);
    _programObject->setUniform(
        _uniformCache.viewport,
        static_cast<float>(viewport[0]),
        static_cast<float>(viewport[1]),
        static_cast<float>(viewport[2]),
        static_cast<float>(viewport[3])
    );
    _programObject->setUniform(
        _uniformCache.lineWidth,
        std::ceil((2.f * 1.f + _appearance.lineWidth) * std::sqrt(2.f))
    );
#endif // !defined(__APPLE__)

    if (renderPoints) {
        // The stride parameter determines the distance between larger points and
        // smaller ones
        _programObject->setUniform(_uniformCache.stride, info.stride);
        _programObject->setUniform(_uniformCache.pointSize, _appearance.pointSize);
    }

    // Fragile! Keep in sync with fragment shader
    enum RenderPhase {
        RenderPhaseLines = 0,
        RenderPhasePoints
    };

    glBindVertexArray(info._vaoID);
    if (renderLines) {
        _programObject->setUniform(_uniformCache.renderPhase, RenderPhaseLines);
        // Subclasses of this renderer might be using the index array or might now be
        // so we check if there is data available and if there isn't, we use the
        // glDrawArrays draw call; otherwise the glDrawElements
        if (info._iBufferID == 0) {
            glDrawArrays(
                GL_LINE_STRIP,
                info.first,
                info.count
            );
        }
        else {
            glDrawElements(
                GL_LINE_STRIP,
                info.count,
                GL_UNSIGNED_INT,
                reinterpret_cast<void*>(info.first * sizeof(unsigned int))
            );
        }
    }
    if (renderPoints) {
        // Subclasses of this renderer might be using the index array or might now be
        // so we check if there is data available and if there isn't, we use the
        // glDrawArrays draw call; otherwise the glDrawElements
        _programObject->setUniform(_uniformCache.renderPhase, RenderPhasePoints);
        if (info._iBufferID == 0) {
            glDrawArrays(GL_POINTS, info.first, info.count);
        }
        else {
            glDrawElements(
                GL_POINTS,
                info.count,
                GL_UNSIGNED_INT,
                reinterpret_cast<void*>(info.first * sizeof(unsigned int))
            );
        }
    }
}

void RenderableTrail::render(const RenderData& data, RendererTasks&) {
    ZoneScoped;

    _programObject->activate();
    _programObject->setUniform(_uniformCache.opacity, opacity());

    glm::dmat4 modelTransform = calcModelTransform(data);

    _programObject->setUniform(_uniformCache.projection, data.camera.projectionMatrix());

    _programObject->setUniform(_uniformCache.color, _appearance.lineColor);
    _programObject->setUniform(_uniformCache.useLineFade, _appearance.useLineFade);
    if (_appearance.useLineFade) {
        int selection = _appearance.fadingModes;
        // Check which rendering method should be used
        if (selection == LineFade) {
            // use point+point
            _programObject->setUniform(_uniformCache.lineFade, _appearance.lineFade);
        }
        else if (selection == LineFadeStartPointDuration) {
            // use startpoint+duration
            float startPoint = (_appearance.lineFadeStarPointDuration.value())[0];
            float remainingRange = 1.0f - startPoint;
            float delta = remainingRange * _appearance.lineFadeStarPointDuration.value()[1];
            float endPoint = std::min(startPoint + delta, 1.f);
            _programObject->setUniform(_uniformCache.lineFade, glm::vec2(startPoint, endPoint));
        }
        else if (selection == LineFadeEndPointDuration) {
            // use endpoint+duration
            float endPoint = (_appearance.lineFadeEndPointDuration.value())[0];
            float remainingRange = endPoint;
            float delta = remainingRange * (_appearance.lineFadeEndPointDuration.value())[1];
            float startPoint = std::max(endPoint - delta, 0.f);
            _programObject->setUniform(_uniformCache.lineFade, glm::vec2(startPoint, endPoint));
        }
    }

    /*glm::ivec2 resolution = global::renderEngine.renderingResolution();
    _programObject->setUniform(_uniformCache.resolution, resolution);*/

    glDepthMask(false);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    const bool renderLines = (_appearance.renderingModes == RenderingModeLines) ||
                             (_appearance.renderingModes == RenderingModeLinesPoints);

    const bool renderPoints = (_appearance.renderingModes == RenderingModePoints) ||
                              (_appearance.renderingModes == RenderingModeLinesPoints);

    if (renderLines) {
#ifdef __APPLE__
        glLineWidth(1);
#else
        glLineWidth(ceil((2.f * 1.f + _appearance.lineWidth) * std::sqrt(2.f)));
#endif
    }
    if (renderPoints) {
        glEnable(GL_PROGRAM_POINT_SIZE);
    }

    // The combined size of vertices; -1 because we duplicate the penultimate point
    const int totalNumber = _primaryRenderInformation.count +
                            _floatingRenderInformation.count - 1;

    // The primary information might use an index buffer, so we might need to start at an
    // offset
    const int primaryOffset = (_primaryRenderInformation._iBufferID == 0) ?
        0 :
        _primaryRenderInformation.first;

    // Culling
    //const glm::dvec3 trailPosWorld = glm::dvec3(
    //    modelTransform * _primaryRenderInformation._localTransform *
    //    glm::dvec4(0.0, 0.0, 0.0, 1.0)
    //);
    //const double distance = glm::distance(trailPosWorld, data.camera.eyePositionVec3());

    //if (distance > boundingSphere() * DISTANCE_CULLING_RADII) {
    //    // Reset
    //    global::renderEngine->openglStateCache().resetBlendState();
    //    global::renderEngine->openglStateCache().resetDepthState();
    //    return;
    //}
    
    if (_splitTrailRenderMode) {
        // Splits the trail up into three parts for more accurate rendering
        // of renderableTrailTrajectory trails

        internalRender(
            renderLines,
            renderPoints,
            data,
            modelTransform,
            _primaryRenderInformation,
            _primaryRenderInformation.count,
            _primaryRenderInformation.first,
            _splitTrailRenderMode,
            _numberOfUniqueVertices
        );

        int floatingOffset = _primaryRenderInformation.count - 1;
        internalRender(
            renderLines,
            renderPoints,
            data,
            modelTransform,
            _floatingRenderInformation,
            _floatingRenderInformation.count,
            _floatingRenderInformation.first,
            _splitTrailRenderMode,
            _numberOfUniqueVertices,
            floatingOffset
        );

        int offset = (_floatingRenderInformation.count > 0) ? 1 : 0;
        internalRender(
            renderLines,
            renderPoints,
            data,
            modelTransform,
            _secondaryRenderInformation,
            _secondaryRenderInformation.count,
            _secondaryRenderInformation.first,
            _splitTrailRenderMode,
            _numberOfUniqueVertices,
            offset
        );
    }
    else {
        // Render the primary batch of vertices
        internalRender(
            renderLines,
            renderPoints,
            data,
            modelTransform,
            _primaryRenderInformation,
            totalNumber,
            primaryOffset
        );

        // The secondary batch is optional, so we need to check whether we have any data here
        if (_floatingRenderInformation._vaoID != 0 && _floatingRenderInformation.count != 0) {
            internalRender(
                renderLines,
                renderPoints,
                data,
                modelTransform,
                _floatingRenderInformation,
                totalNumber,
                // -1 because we duplicate the penultimate point between the vertices
                -(primaryOffset + _primaryRenderInformation.count - 1)
            );
        }
    }
    

    

    if (renderPoints) {
        glDisable(GL_PROGRAM_POINT_SIZE);
    }

    glBindVertexArray(0);

    // Reset
    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetDepthState();

    _programObject->deactivate();
}

} // namespace openspace
