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

#include <modules/base/rendering/linesegments/renderablelinesegments.h>

#include <modules/base/basemodule.h>
#include <openspace/documentation/documentation.h>
#include <openspace/engine/globals.h>
#include <openspace/properties/misc/optionproperty.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/format.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/dictionary.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/textureunit.h>
#include <algorithm>
#include <filesystem>

namespace {
    using namespace openspace;

    constexpr std::string_view _loggerCat = "RenderableLineSegments";

    constexpr Property::PropertyInfo DrawElementsInfo = {
        "DrawElements",
        "Draw elements",
        "Enables/disables the drawing of the lines.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo LineWidthInfo = {
        "LineWidth",
        "Line width",
        "The width of the lines. The larger the number, the thicker the lines.",
        Property::Visibility::NoviceUser
    };

    constexpr Property::PropertyInfo NumLineSegmentsInfo = {
        "NumberOfLineSegments",
        "Number of line segments",
        "Information about how many line segments are being rendered.",
        Property::Visibility::User
    };

    constexpr Property::PropertyInfo FixedColorInfo = {
        "FixedColor",
        "Fixed color",
        "The color of the lines, when no color map is used.",
        Property::Visibility::NoviceUser
    };

    // This `Renderable` draws arbitrary lines in 3D space based on a CSV data file. Each
    // row in the file is a line vertex, with its position given by the columns specified
    // in the `DataMapping`.
    //
    // The rows are connected into lines in one of two ways. If a `LineIdentifierColumn`
    // is specified, consecutive rows that share the same value in that column are
    // connected into a single continuous polyline. If no such column is specified, every
    // consecutive pair of rows is interpreted as a separate, straight line segment (so
    // the first two rows form one segment, the next two another, and so on).
    //
    // The lines can be drawn with a single fixed color, or be colored based on the
    // values in a data column using a color map.
    struct [[codegen::Dictionary(RenderableLineSegments)]] Parameters {
        // The path to the CSV data file that contains the information about the lines to
        // be rendered.
        std::filesystem::path file;

        // If true (default), the loaded dataset will be cached so that it can be loaded
        // faster at a later time. Note that this means that any updates to the values in
        // the dataset will not lead to changes in the rendering without first removing
        // the cached file. Set it to false to disable caching.
        std::optional<bool> useCaching;

        // A dictionary specifying details on how to load the dataset. Updating the data
        // mapping will lead to a new cached version of the dataset.
        std::optional<ghoul::Dictionary> dataMapping
            [[codegen::reference("core_dataloader_datamapping")]];

        // The name of the data column that is used to group rows into connected
        // polylines. Consecutive rows that share the same value in this column are
        // connected into a single line. If no value is specified, every consecutive pair
        // of rows is interpreted as a separate line segment instead.
        std::optional<std::string> lineIdentifierColumn;

        // [[codegen::verbatim(DrawElementsInfo.description)]]
        std::optional<bool> drawElements;

        // [[codegen::verbatim(LineWidthInfo.description)]]
        std::optional<float> lineWidth [[codegen::greaterequal(1.f)]];

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
        // distances/positions in the data file.
        std::optional<Unit> unit;

        struct ColorSettings {
            // [[codegen::verbatim(FixedColorInfo.description)]]
            std::optional<glm::vec3> fixedColor [[codegen::color()]];

            // Settings related to the choice of color map, parameters, etc.
            std::optional<ghoul::Dictionary> colorMapping
                [[codegen::reference("core_colormappingcomponent")]];
        };
        // Settings related to the coloring of the lines, such as a fixed color or a
        // color map.
        std::optional<ColorSettings> coloring;
    };
} // namespace
#include "renderablelinesegments_codegen.cpp"

namespace openspace {

Documentation RenderableLineSegments::Documentation() {
    return codegen::doc<Parameters>(
        "base_renderable_linesegments",
        Renderable::Documentation()
    );
}

RenderableLineSegments::ColorSettings::ColorSettings(const ghoul::Dictionary& dictionary)
    : PropertyOwner({ "Coloring", "Coloring", "" })
    , fixedColor(FixedColorInfo, glm::vec3(1.f), glm::vec3(0.f), glm::vec3(1.f))
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    fixedColor.setViewOption(Property::ViewOptions::Color);
    addProperty(fixedColor);

    if (p.coloring.has_value()) {
        const Parameters::ColorSettings settings = *p.coloring;
        fixedColor = settings.fixedColor.value_or(fixedColor);

        if (settings.colorMapping.has_value()) {
            colorMapping = std::make_unique<ColorMappingComponent>(
                *settings.colorMapping
            );
            addPropertySubOwner(colorMapping.get());
        }
    }
}

RenderableLineSegments::RenderableLineSegments(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _colorSettings(dictionary)
    , _drawElements(DrawElementsInfo, true)
    , _lineWidth(LineWidthInfo, 1.f, 1.f, 20.f)
    , _nLineSegments(NumLineSegmentsInfo, 0)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    addProperty(Fadeable::_opacity);

    _dataFile = absPath(p.file);
    _hasDataFile = true;

    if (p.dataMapping.has_value()) {
        _dataMapping = dataloader::DataMapping::createFromDictionary(*p.dataMapping);
    }

    if (p.lineIdentifierColumn.has_value()) {
        _lineIdentifierColumn = *p.lineIdentifierColumn;
        _hasLineIdentifierColumn = true;
    }

    _useCaching = p.useCaching.value_or(_useCaching);

    if (p.unit.has_value()) {
        _unit = codegen::map<DistanceUnit>(*p.unit);
    }
    else {
        _unit = DistanceUnit::Meter;
    }

    _drawElements = p.drawElements.value_or(_drawElements);
    addProperty(_drawElements);

    _lineWidth = p.lineWidth.value_or(_lineWidth);
    addProperty(_lineWidth);

    _nLineSegments.setReadOnly(true);
    addProperty(_nLineSegments);

    addPropertySubOwner(_colorSettings);

    if (p.coloring.has_value() && p.coloring->colorMapping.has_value()) {
        _hasColorMapFile = true;

        _colorSettings.colorMapping->dataColumn.onChange(
            [this]() { _dataIsDirty = true; }
        );

        _colorSettings.colorMapping->setRangeFromData.onChange([this]() {
            const int parameterIndex = currentColorParameterIndex();
            if (parameterIndex >= 0) {
                _colorSettings.colorMapping->valueRange =
                    _dataset.findValueRange(parameterIndex);
            }
        });

        _colorSettings.colorMapping->colorMapFile.onChange([this]() {
            _dataIsDirty = true;
            _hasColorMapFile = std::filesystem::exists(
                _colorSettings.colorMapping->colorMapFile.value()
            );
        });
    }
}

void RenderableLineSegments::initialize() {
    if (_hasDataFile) {
        if (_useCaching) {
            _dataset = dataloader::data::loadFileWithCache(_dataFile, _dataMapping);
        }
        else {
            _dataset = dataloader::data::loadFile(_dataFile, _dataMapping);
        }
    }

    if (_hasDataFile && _hasColorMapFile) {
        _colorSettings.colorMapping->initialize(_dataset, _useCaching);
    }

    buildSegmentEndpoints();
}

void RenderableLineSegments::initializeGL() {
    initializeShaders();
    ghoul::opengl::updateUniformLocations(*_program, _uniformCache);

    glCreateVertexArrays(1, &_vao);
    glCreateBuffers(1, &_vbo);
}

void RenderableLineSegments::deinitializeGL() {
    glDeleteVertexArrays(1, &_vao);
    _vao = 0;

    glDeleteBuffers(1, &_vbo);
    _vbo = 0;

    deinitializeShaders();
}

void RenderableLineSegments::initializeShaders() {
    _program = BaseModule::ProgramObjectManager.request(
        "RenderableLineSegments",
        []() {
            return global::renderEngine->buildRenderProgram(
                "RenderableLineSegments",
                absPath("${MODULE_BASE}/shaders/linesegments/linesegments_vs.glsl"),
                absPath("${MODULE_BASE}/shaders/linesegments/linesegments_fs.glsl")
            );
        }
    );
}

void RenderableLineSegments::deinitializeShaders() {
    BaseModule::ProgramObjectManager.release(
        "RenderableLineSegments",
        [](ghoul::opengl::ProgramObject* p) {
            global::renderEngine->removeRenderProgram(p);
        }
    );
    _program = nullptr;
}

void RenderableLineSegments::setExtraUniforms() {}

int RenderableLineSegments::nAttributesPerVertex() const {
    return 3 + (hasColorData() ? 1 : 0);
}

unsigned int RenderableLineSegments::nObjects() const {
    return static_cast<unsigned int>(_dataset.entries.size());
}

glm::dvec3 RenderableLineSegments::transformedPosition(
                                               const dataloader::Dataset::Entry& e) const
{
    return glm::dvec3(e.position) * toMeter(_unit);
}

int RenderableLineSegments::currentColorParameterIndex() const {
    if (!_hasColorMapFile || !_colorSettings.colorMapping) {
        return -1;
    }

    const OptionProperty& property = _colorSettings.colorMapping->dataColumn;
    if (property.options().empty()) {
        return -1;
    }

    return _dataset.index(property.option().description);
}

bool RenderableLineSegments::hasColorData() const {
    return _hasColorMapFile && currentColorParameterIndex() >= 0;
}

void RenderableLineSegments::addVertexData(unsigned int datasetIndex,
                                           std::vector<float>& result,
                                           double& maxRadius) const
{
    const dataloader::Dataset::Entry& e = _dataset.entries[datasetIndex];
    const glm::dvec3 position = transformedPosition(e);
    maxRadius = std::max(maxRadius, glm::length(position));

    for (int j = 0; j < 3; j++) {
        result.push_back(static_cast<float>(position[j]));
    }

    if (hasColorData()) {
        result.push_back(e.data[currentColorParameterIndex()]);
    }
}

int RenderableLineSegments::bufferVertexAttribute(const std::string& name, GLint nValues,
                                                  int attributesPerVertex,
                                                  int offset) const
{
    const GLint attrib = _program->attributeLocation(name);
    if (attrib < 0) {
        return offset + nValues;
    }

    glEnableVertexArrayAttrib(_vao, attrib);
    glVertexAttribPointer(
        attrib,
        nValues,
        GL_FLOAT,
        GL_FALSE,
        attributesPerVertex * sizeof(float),
        reinterpret_cast<void*>(offset * sizeof(float))
    );

    return offset + nValues;
}

void RenderableLineSegments::setVertexAttributePointers() {
    const int stride = nAttributesPerVertex();
    int offset = 0;
    offset = bufferVertexAttribute("in_position", 3, stride, offset);

    if (hasColorData()) {
        offset = bufferVertexAttribute("in_colorValue", 1, stride, offset);
    }
}

void RenderableLineSegments::buildSegmentEndpoints() {
    _segmentEndpoints.clear();

    const unsigned int n = nObjects();
    if (n == 0) {
        return;
    }

    int columnIndex = -1;
    if (_hasLineIdentifierColumn) {
        columnIndex = _dataset.index(_lineIdentifierColumn);
        if (columnIndex < 0) {
            LERROR(std::format(
                "Could not find line identifier column '{}' in the data file. The data "
                "will be interpreted as separate segments instead", _lineIdentifierColumn
            ));
        }
    }

    if (columnIndex >= 0) {
        // Group consecutive rows that share the same identifier into polylines
        unsigned int lineStart = 0;
        for (unsigned int i = 1; i <= n; i++) {
            const bool endOfLine = (i == n) ||
                (_dataset.entries[i].data[columnIndex] !=
                    _dataset.entries[i - 1].data[columnIndex]);

            if (endOfLine) {
                if (i - lineStart < 2) {
                    LWARNING(std::format(
                        "A line with identifier '{}' has fewer than two vertices and "
                        "will not be drawn",
                        _dataset.entries[lineStart].data[columnIndex]
                    ));
                }
                for (unsigned int v = lineStart; v + 1 < i; v++) {
                    _segmentEndpoints.push_back(v);
                    _segmentEndpoints.push_back(v + 1);
                }
                lineStart = i;
            }
        }
    }
    else {
        // No grouping column: every consecutive pair of rows is a separate segment
        if (n % 2 != 0) {
            LWARNING(
                "Found an odd number of line vertices in the data file. The last vertex "
                "will be ignored"
            );
        }
        for (unsigned int i = 0; i + 1 < n; i += 2) {
            _segmentEndpoints.push_back(i);
            _segmentEndpoints.push_back(i + 1);
        }
    }

    _nLineSegments = static_cast<unsigned int>(_segmentEndpoints.size() / 2);
}

std::vector<float> RenderableLineSegments::createDataSlice() {
    std::vector<float> result;
    if (_segmentEndpoints.empty()) {
        return result;
    }
    result.reserve(nAttributesPerVertex() * _segmentEndpoints.size());

    double maxRadius = 0.0;
    for (const unsigned int index : _segmentEndpoints) {
        addVertexData(index, result, maxRadius);
    }

    setBoundingSphere(maxRadius);
    return result;
}

void RenderableLineSegments::updateBufferData() {
    if (!_hasDataFile || _segmentEndpoints.empty()) {
        return;
    }

    const std::vector<float> slice = createDataSlice();

    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glNamedBufferData(
        _vbo,
        slice.size() * sizeof(float),
        slice.data(),
        GL_DYNAMIC_DRAW
    );

    setVertexAttributePointers();

    glBindVertexArray(0);

    _dataIsDirty = false;
}

void RenderableLineSegments::update(const UpdateData&) {
    if (_hasColorMapFile && _colorSettings.colorMapping) {
        _colorSettings.colorMapping->update(_dataset, _useCaching);
    }

    if (_dataIsDirty) {
        updateBufferData();
    }
}

void RenderableLineSegments::render(const RenderData& data, RendererTasks&) {
    if (!_hasDataFile || !_drawElements || _segmentEndpoints.empty()) {
        return;
    }

    _program->activate();

    const glm::dmat4 modelTransform = calcModelTransform(data);
    const glm::dmat4 modelViewTransform = calcModelViewTransform(data, modelTransform);

    _program->setUniform(_uniformCache.modelViewTransform, modelViewTransform);
    _program->setUniform(
        _uniformCache.projectionTransform,
        glm::dmat4(data.camera.projectionMatrix())
    );
    _program->setUniform(_uniformCache.color, _colorSettings.fixedColor);
    _program->setUniform(_uniformCache.opacity, opacity());

    const bool useColorMap = hasColorData() && _colorSettings.colorMapping->enabled &&
        _colorSettings.colorMapping->texture();
    _program->setUniform(_uniformCache.useColorMap, useColorMap);

    ghoul::opengl::TextureUnit colorMapTextureUnit;
    _program->setUniform(_uniformCache.colorMapTexture, colorMapTextureUnit);

    if (useColorMap) {
        colorMapTextureUnit.bind(*_colorSettings.colorMapping->texture());

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

    setExtraUniforms();

    glEnablei(GL_BLEND, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glLineWidth(_lineWidth);

    glBindVertexArray(_vao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(_segmentEndpoints.size()));
    glBindVertexArray(0);

    _program->deactivate();

    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetLineState();
}

} // namespace openspace
