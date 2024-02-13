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

#include <modules/base/rendering/renderabletube.h>

#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/helper.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/lightsource.h>
#include <openspace/util/time.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/textureunit.h>
#include <ghoul/opengl/texture.h>
#include <glm/gtx/projection.hpp>
#include <optional>

using json = nlohmann::json;

namespace {
    constexpr std::string_view _loggerCat = "RenderableTube";
    constexpr int8_t CurrentMajorVersion = 0;
    constexpr int8_t CurrentMinorVersion = 1;
    constexpr std::array<const char*, 23> UniformNames = {
        "modelViewTransform", "projectionTransform", "normalTransform", "opacity",
        "color", "nanColor", "useNanColor", "aboveRangeColor", "useAboveRangeColor",
        "belowRangeColor", "useBelowRangeColor", "useColorMap", "colorMapTexture",
        "cmapRangeMin", "cmapRangeMax", "hideOutsideRange", "performShading",
        "nLightSources", "lightDirectionsViewSpace", "lightIntensities",
        "ambientIntensity", "diffuseIntensity", "specularIntensity"
    };

    constexpr openspace::properties::Property::PropertyInfo TransferFunctionInfo = {
        "TransferFunctionPath",
        "Transfer Function Path",
        "Specifies the transfer function file path",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo EnableFaceCullingInfo = {
        "EnableFaceCulling",
        "Enable Face Culling",
        "Enable OpenGL automatic face culling optimization",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo ShadingEnabledInfo = {
        "PerformShading",
        "Perform Shading",
        "This value determines whether shading should be applied to the tube",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo AmbientIntensityInfo = {
        "AmbientIntensity",
        "Ambient Intensity",
        "A multiplier for ambient lighting for the shading of the tube",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo DiffuseIntensityInfo = {
        "DiffuseIntensity",
        "Diffuse Intensity",
        "A multiplier for diffuse lighting for the shading of the tube",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo SpecularIntensityInfo = {
        "SpecularIntensity",
        "Specular Intensity",
        "A multiplier for specular lighting for the shading of the tube",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo LightSourcesInfo = {
        "LightSources",
        "Light Sources",
        "A list of light sources that this tube should accept light from",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo TubeColorInfo = {
        "FixedColor",
        "Fixed Color",
        "This value is used to define the color of the tube when no color map is"
        "used",
        openspace::properties::Property::Visibility::NoviceUser
    };

    constexpr openspace::properties::Property::PropertyInfo AddEdgesInfo = {
        "AddEdges",
        "Add Edges",
        "This value determines whether a bottom and top should b eadded to the tube",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo DrawWireframeInfo = {
        "DrawWireframe",
        "Wireframe",
        "If true, draw the wire frame of the tube",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo WireLineWidthInfo = {
        "WireLineWidth",
        "Wire Line Width",
        "The line width to use when the tube is rendered as a wireframe",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo UseSmoothNormalsInfo = {
        "UseSmoothNormals",
        "Use Smooth Normals",
        "If ture, the tube is shaded using smooth normals. If false, every triangle "
        "get its own normal, which can lead to harder shadows",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo ShowAllTubeInfo = {
        "ShowAllTube",
        "Show all the tube",
        "If ture, only the part of the tube that corresponds to the current time is "
        "shown. If false, the whole tube is shown.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    struct [[codegen::Dictionary(RenderableTube)]] Parameters {
        // The input file with data for the tube
        std::string file;

        // [[codegen::verbatim(EnableFaceCullingInfo.description)]]
        std::optional<bool> enableFaceCulling;

        // [[codegen::verbatim(ShadingEnabledInfo.description)]]
        std::optional<bool> performShading;

        // [[codegen::verbatim(AmbientIntensityInfo.description)]]
        std::optional<float> ambientIntensity [[codegen::inrange(0.f, 1.f)]];

        // [[codegen::verbatim(DiffuseIntensityInfo.description)]]
        std::optional<float> diffuseIntensity [[codegen::inrange(0.f, 1.f)]];

        // [[codegen::verbatim(SpecularIntensityInfo.description)]]
        std::optional<float> specularIntensity [[codegen::inrange(0.f, 1.f)]];

        // [[codegen::verbatim(LightSourcesInfo.description)]]
        std::optional<std::vector<ghoul::Dictionary>> lightSources
            [[codegen::reference("core_light_source")]];

        struct ColorSettings {
            // [[codegen::verbatim(TubeColorInfo.description)]]
            std::optional<glm::vec3> fixedColor [[codegen::color()]];

            // Settings related to the choice of color map, parameters, etc.
            std::optional<ghoul::Dictionary> colorMapping
                [[codegen::reference("colormappingcomponent")]];
        };
        // Settings related to the coloring of the points, such as a fixed color,
        // color map, etc.
        std::optional<ColorSettings> coloring;

        // [[codegen::verbatim(AddEdgesInfo.description)]]
        std::optional<bool> addEdges;

        // [[codegen::verbatim(DrawWireframeInfo.description)]]
        std::optional<bool> drawWireframe;

        // [[codegen::verbatim(WireLineWidthInfo.description)]]
        std::optional<float> wireLineWidth;

        // [[codegen::verbatim(UseSmoothNormalsInfo.description)]]
        std::optional<bool> useSmoothNormals;

        // [[codegen::verbatim(ShowAllTubeInfo.description)]]
        std::optional<bool> showAllTube;
    };
#include "renderabletube_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation RenderableTube::Documentation() {
    return codegen::doc<Parameters>("base_renderable_tube");
}

RenderableTube::Shading::Shading()
    : properties::PropertyOwner({ "Shading" })
    , enabled(ShadingEnabledInfo, true)
    , ambientIntensity(AmbientIntensityInfo, 0.2f, 0.f, 1.f)
    , diffuseIntensity(DiffuseIntensityInfo, 1.f, 0.f, 1.f)
    , specularIntensity(SpecularIntensityInfo, 1.f, 0.f, 1.f)
{
    addProperty(enabled);
    addProperty(ambientIntensity);
    addProperty(diffuseIntensity);
    addProperty(specularIntensity);
}

RenderableTube::ColorSettings::ColorSettings(const ghoul::Dictionary& dictionary)
    : properties::PropertyOwner({ "Coloring", "Coloring", "" })
    , tubeColor(TubeColorInfo, glm::vec3(1.f), glm::vec3(0.f), glm::vec3(1.f))
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    if (p.coloring.has_value()) {
        const Parameters::ColorSettings settings = *p.coloring;
        tubeColor = settings.fixedColor.value_or(tubeColor);

        if (settings.colorMapping.has_value()) {
            colorMapping = std::make_unique<ColorMappingComponent>(
                *settings.colorMapping
            );
            addPropertySubOwner(colorMapping.get());
        }
    }
    tubeColor.setViewOption(properties::Property::ViewOptions::Color);
    addProperty(tubeColor);
}

RenderableTube::RenderableTube(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _enableFaceCulling(EnableFaceCullingInfo, true)
    , _lightSourcePropertyOwner({ "LightSources", "Light Sources" })
    , _colorSettings(dictionary)
    , _addEdges(AddEdgesInfo, true)
    , _drawWireframe(DrawWireframeInfo, false)
    , _wireLineWidth(WireLineWidthInfo, 1.f, 1.f, 10.f)
    , _useSmoothNormals(UseSmoothNormalsInfo, true)
    , _showAllTube(ShowAllTubeInfo, false)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _dataFile = p.file;

    _enableFaceCulling = p.enableFaceCulling.value_or(_enableFaceCulling);
    addProperty(_enableFaceCulling);

    _shading.enabled = p.performShading.value_or(_shading.enabled);
    _shading.ambientIntensity = p.ambientIntensity.value_or(_shading.ambientIntensity);
    _shading.diffuseIntensity = p.diffuseIntensity.value_or(_shading.diffuseIntensity);
    _shading.specularIntensity = p.specularIntensity.value_or(_shading.specularIntensity);
    addPropertySubOwner(_shading);

    if (p.lightSources.has_value()) {
        std::vector<ghoul::Dictionary> lightsources = *p.lightSources;

        for (const ghoul::Dictionary& lsDictionary : lightsources) {
            std::unique_ptr<LightSource> lightSource =
                LightSource::createFromDictionary(lsDictionary);
            _lightSourcePropertyOwner.addPropertySubOwner(lightSource.get());
            _lightSources.push_back(std::move(lightSource));
        }
    }

    if (p.coloring.has_value() && (*p.coloring).colorMapping.has_value()) {
        _hasColorMapFile = true;

        _colorSettings.colorMapping->dataColumn.onChange(
            [this]() { _tubeIsDirty = true; }
        );

        _colorSettings.colorMapping->setRangeFromData.onChange([this]() {
            int parameterIndex = currentColorParameterIndex();
            _colorSettings.colorMapping->valueRange = _colorDataset.findValueRange(
                parameterIndex
            );
        });
    }
    addPropertySubOwner(_colorSettings);

    _addEdges.onChange([this]() { _tubeIsDirty = true; });
    _addEdges = p.addEdges.value_or(_addEdges);
    addProperty(_addEdges);

    _drawWireframe = p.drawWireframe.value_or(_drawWireframe);
    addProperty(_drawWireframe);

    _wireLineWidth = p.wireLineWidth.value_or(_wireLineWidth);
    addProperty(_wireLineWidth);

    _useSmoothNormals.onChange([this]() { _tubeIsDirty = true; });
    _useSmoothNormals = p.useSmoothNormals.value_or(_useSmoothNormals);
    addProperty(_useSmoothNormals);

    _showAllTube = p.showAllTube.value_or(_showAllTube);
    addProperty(_showAllTube);

    addProperty(Fadeable::_opacity);
}

bool RenderableTube::isReady() const {
    return _shader != nullptr;
}

void RenderableTube::initialize() {
    readDataFile();
    updateTube();
    updateBufferData();

    if (_hasColorMapFile) {
        _colorSettings.colorMapping->initialize(_colorDataset);
    }

    for (const std::unique_ptr<LightSource>& ls : _lightSources) {
        ls->initialize();
    }
}

void RenderableTube::initializeGL() {
    _shader = global::renderEngine->buildRenderProgram(
        "TubeProgram",
        absPath("${MODULE_BASE}/shaders/tube_vs.glsl"),
        absPath("${MODULE_BASE}/shaders/tube_fs.glsl")
    );
    ghoul::opengl::updateUniformLocations(*_shader, _uniformCache, UniformNames);

    if (_hasColorMapFile) {
        _colorSettings.colorMapping->initializeTexture();
    }

    glGenVertexArrays(1, &_vaoId);
    glGenBuffers(1, &_vboId);
    glGenBuffers(1, &_iboId);

    glBindVertexArray(_vaoId);

    updateBufferData();

    glEnableVertexAttribArray(0);
    glVertexAttribLPointer(0, 3, GL_DOUBLE, sizeof(PolygonVertex), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PolygonVertex),
        reinterpret_cast<const GLvoid*>(offsetof(PolygonVertex, normal))
    );

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PolygonVertex),
        reinterpret_cast<const GLvoid*>(offsetof(PolygonVertex, value))
    );

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PolygonVertex),
        reinterpret_cast<const GLvoid*>(offsetof(PolygonVertex, tex))
    );

    // Ending
    glGenVertexArrays(1, &_vaoIdEnding);
    glGenBuffers(1, &_vboIdEnding);
    glGenBuffers(1, &_iboIdEnding);

    glBindVertexArray(_vaoIdEnding);

    glBindBuffer(GL_ARRAY_BUFFER, _vboIdEnding);
    glBufferData(
        GL_ARRAY_BUFFER,
        _verticiesEnding.size() * sizeof(PolygonVertex),
        _verticiesEnding.data(),
        GL_STREAM_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _iboIdEnding);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        _indiciesEnding.size() * sizeof(unsigned int),
        _indiciesEnding.data(),
        GL_STREAM_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribLPointer(0, 3, GL_DOUBLE, sizeof(PolygonVertex), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PolygonVertex),
        reinterpret_cast<const GLvoid*>(offsetof(PolygonVertex, normal))
    );

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PolygonVertex),
        reinterpret_cast<const GLvoid*>(offsetof(PolygonVertex, value))
    );

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(PolygonVertex),
        reinterpret_cast<const GLvoid*>(offsetof(PolygonVertex, tex))
    );

    glBindVertexArray(0);
}

void RenderableTube::deinitializeGL() {
    global::renderEngine->removeRenderProgram(_shader.get());
    _shader = nullptr;

    glDeleteVertexArrays(1, &_vaoId);
    _vaoId = 0;

    glDeleteBuffers(1, &_vboId);
    _vboId = 0;

    glDeleteBuffers(1, &_iboId);
    _iboId = 0;

    // Ending
    glDeleteVertexArrays(1, &_vaoIdEnding);
    _vaoIdEnding = 0;

    glDeleteBuffers(1, &_vboIdEnding);
    _vboIdEnding = 0;

    glDeleteBuffers(1, &_iboIdEnding);
    _iboIdEnding = 0;
}

int RenderableTube::currentColorParameterIndex() const {
    const properties::OptionProperty& property =
        _colorSettings.colorMapping->dataColumn;

    if (!_hasColorMapFile || property.options().empty()) {
        return 0;
    }

    return _colorDataset.index(property.option().description);
}

void RenderableTube::readDataFile() {
    std::filesystem::path file = absPath(_dataFile);
    if (!std::filesystem::is_regular_file(file)) {
        LWARNING(fmt::format("The data file '{}' could not be found", file));
        return;
    }

    // Open file
    std::ifstream fileStream(file);
    if (!fileStream.good()) {
        LERROR(fmt::format("Failed to open data file '{}'", file));
        return;
    }

    // Read the entire file into a string
    constexpr size_t readSize = std::size_t(4096);
    fileStream.exceptions(std::ios_base::badbit);

    std::string data;
    std::string buf = std::string(readSize, '\0');
    while (fileStream.read(buf.data(), readSize)) {
        data.append(buf, 0, fileStream.gcount());
    }
    data.append(buf, 0, fileStream.gcount());
    fileStream.close();

    // Convert to a json object
    json jsonData = json::parse(data);

    // Check version
    bool foundVersion = false;
    if (auto version = jsonData.find("version"); version != jsonData.end()) {
        auto major = version->find("major");
        auto minor = version->find("minor");

        if (major != version->end() && minor != version->end()) {
            foundVersion = true;
            if (*major != CurrentMajorVersion || *minor != CurrentMinorVersion) {
                LWARNING(fmt::format(
                    "Unknown data version '{}.{}' found. The currently supported version "
                    "is {}.{}", major->dump(), minor->dump(), CurrentMajorVersion,
                    CurrentMinorVersion
                ));
            }
        }
    }

    if (!foundVersion) {
        LWARNING("Could not find version information, version might not be supported");
    }

    // Find polygons
    auto polygons = jsonData.find("polygons");
    if (polygons == jsonData.end() || polygons->size() < 1) {
        LERROR("Could not find any polygon in the data");
        return;
    }

    // Loop throught json object to fill the datastructure for the polygons
    bool isFirstPlygonAndPoint = true;
    for (auto it = polygons->begin(); it < polygons->end(); ++it) {
        TimePolygon timePolygon;

        // Timestamp
        auto time = it->find("time");
        if (time == it->end()) {
            LERROR("Could not find time for polygon in data");
            return;
        }
        std::string timeString = time->dump();
        timeString.erase(
            std::remove(timeString.begin(), timeString.end(), '\"'),
            timeString.end()
        );
        timePolygon.timestamp = Time::convertTime(timeString);

        // Center
        auto centerPt = it->find("center");
        if (centerPt == it->end()) {
            LERROR("Could not find center for polygon in data");
            return;
        }
        double x, y, z;
        centerPt->at("x").get_to(x);
        centerPt->at("y").get_to(y);
        centerPt->at("z").get_to(z);
        timePolygon.center = glm::dvec3(x, y, z);

        // Points
        auto points = it->find("points");
        if (points == it->end() || points->size() < 1) {
            LERROR("Could not find points for polygon in data");
            return;
        }
        for (auto pt = points->begin(); pt < points->end(); ++pt) {
            TimePolygonPoint timePolygonPoint;

            // Coordinates
            auto px = pt->find("x");
            auto py = pt->find("y");
            auto pz = pt->find("z");

            if (px == pt->end() || py == pt->end() || pz == pt->end()) {
                LERROR("Could not find coordinate component for polygon in data");
                return;
            }

            double x, y, z;
            pt->at("x").get_to(x);
            pt->at("y").get_to(y);
            pt->at("z").get_to(z);
            timePolygonPoint.coordinate = glm::dvec3(x, y, z);

            // Data values (optional)
            auto colorData = pt->find("data");
            if (colorData != pt->end() && _hasColorMapFile) {
                int colorDataIndex = 0;
                dataloader::Dataset::Entry entry;
                for (auto dt : colorData->items()) {
                    if (isFirstPlygonAndPoint) {
                        _colorDataset.variables.push_back({ .index = colorDataIndex++, .name = dt.key() });
                    }
                    entry.data.push_back(dt.value());
                }

                _colorDataset.entries.push_back(entry);
                if (isFirstPlygonAndPoint) {
                    isFirstPlygonAndPoint = false;
                }
            }

            // Texture coordinates (optional)
            auto pu = pt->find("u");
            auto pv = pt->find("v");
            if (pu != pt->end() && pv != pt->end()) {
                float u, v;
                pt->at("u").get_to(u);
                pt->at("v").get_to(v);
                timePolygonPoint.tex = glm::vec2(u, v);
            }

            timePolygon.points.push_back(timePolygonPoint);
        }
        _data.push_back(timePolygon);
    }
}

void RenderableTube::updateTube() {
    // Tube needs at least two polygons
    const size_t nPolygons = _data.size();
    if (nPolygons < 2) {
        LERROR("Tube is empty");
        _nPolygons = 0;
        return;
    }
    else {
        _nPolygons = nPolygons;
    }

    // Polygon needs at least 3 sides
    // NOTE: assumes all polygons have the same number of points
    const size_t nPoints = _data.front().points.size();
    if (nPoints < 3) {
        LERROR("Polygons need at least 3 edges");
        _nPoints = 0;
        return;
    }
    else {
        _nPoints = nPoints;
    }

    _verticies.clear();
    _indicies.clear();

    if (_useSmoothNormals) {
        createSmoothTube();
    }
    else {
        createLowPolyTube();
    }
}

void RenderableTube::createSmoothTube() {
    // Verticies
    int pointCounter = 0;

    // Get the selected color parameter
    int colorParamIndex = currentColorParameterIndex();

    // Calciulate the normals for the top and bottom
    glm::dvec3 bottomCenter = _data.front().center;
    glm::dvec3 topCenter = _data.back().center;
    glm::dvec3 bottomNormal = bottomCenter - _data[1].center;
    glm::dvec3 topNormal = topCenter - _data[_data.size() - 2].center;

    // Add the bottom
    if (_addEdges) {
        addBottom(pointCounter, bottomCenter, bottomNormal, colorParamIndex);
    }

    // Add all the polygons that will create the sides of the tube
    for (unsigned int polyIndex = 0; polyIndex < _nPolygons; ++polyIndex) {
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            bool isLast = polyIndex == _nPolygons - 1;

            PolygonVertex sidePoint;
            sidePoint.position[0] = _data[polyIndex].points[pointIndex].coordinate.x;
            sidePoint.position[1] = _data[polyIndex].points[pointIndex].coordinate.y;
            sidePoint.position[2] = _data[polyIndex].points[pointIndex].coordinate.z;

            // Texture coordinate
            sidePoint.tex[0] = _data[polyIndex].points[pointIndex].tex.x;
            sidePoint.tex[1] = _data[polyIndex].points[pointIndex].tex.y;

            // Calculate normal
            glm::dvec3 centerLine = isLast ?
                _data[polyIndex - 1].center - _data[polyIndex].center :
                _data[polyIndex].center - _data[polyIndex + 1].center;
            glm::dvec3 normal = _data[polyIndex].points[pointIndex].coordinate -
                glm::proj(_data[polyIndex].points[pointIndex].coordinate, centerLine) -
                centerLine;

            sidePoint.normal[0] = normal.x;
            sidePoint.normal[1] = normal.y;
            sidePoint.normal[2] = normal.z;

            if (_hasColorMapFile) {
                sidePoint.value = _colorDataset.entries[pointCounter++].data[colorParamIndex];
            }

            _verticies.push_back(sidePoint);
        }
    }

    // Add the top
    if (_addEdges) {
        addTop(pointCounter - _nPoints, topCenter, topNormal, colorParamIndex);
    }

    // Indicies
    unsigned int firstSideIndex = _addEdges ? _nPoints + 1 : 0;

    // Add Indices for bottom
    if (_addEdges) {
        unsigned int bottomCenterIndex = 0;
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            unsigned int vIndex = pointIndex + 1;
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = bottomCenterIndex;
            unsigned int v1 = vIndex;
            unsigned int v2 = isLast ? v0 + 1 : v1 + 1;

            _indicies.push_back(v0);
            _indicies.push_back(v1);
            _indicies.push_back(v2);
        }
    }

    // Indices for side triangles
    for (unsigned int polyIndex = 0; polyIndex < _nPolygons - 1; ++polyIndex) {
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            unsigned int vIndex = firstSideIndex + pointIndex + polyIndex * _nPoints;
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = vIndex;
            unsigned int v1 = v0 + _nPoints;
            unsigned int v2 = isLast ? v0 + 1 : v1 + 1;
            unsigned int v3 = isLast ? v0 + 1 - _nPoints : v0 + 1;

            // 2 triangles per sector
            _indicies.push_back(v0);
            _indicies.push_back(v1);
            _indicies.push_back(v2);

            _indicies.push_back(v0);
            _indicies.push_back(v2);
            _indicies.push_back(v3);
        }
    }

    // Add Indices for top
    if (_addEdges) {
        unsigned int topCenterIndex = _verticies.size() - 1;
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            unsigned int vIndex = topCenterIndex - pointIndex - 1;
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = topCenterIndex;
            unsigned int v1 = vIndex;
            unsigned int v2 = isLast ? v0 - 1 : v1 - 1;

            _indicies.push_back(v0);
            _indicies.push_back(v1);
            _indicies.push_back(v2);
        }
    }
}

void RenderableTube::createLowPolyTube() {
    // Verticies
    int pointCounter = 0;

    // Get the selected color parameter
    int colorParamIndex = currentColorParameterIndex();

    // Calciulate the normals for the top and bottom
    glm::dvec3 bottomCenter = _data.front().center;
    glm::dvec3 topCenter = _data.back().center;
    glm::dvec3 bottomNormal = bottomCenter - _data[1].center;
    glm::dvec3 topNormal = topCenter - _data[_data.size() - 2].center;

    // Add the bottom
    if (_addEdges) {
        addBottom(pointCounter, bottomCenter, bottomNormal, colorParamIndex);
    }

    // Add all the polygons that will create the sides of the tube
    for (unsigned int polyIndex = 0; polyIndex < _nPolygons - 1; ++polyIndex) {
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            TimePolygon currentTimePolygon = _data[polyIndex];
            TimePolygon nextTimePolygon = _data[polyIndex + 1];
            bool isLast = pointIndex == _nPoints - 1;

            // Identify all the points that are included in this section
            TimePolygonPoint v0 = currentTimePolygon.points[pointIndex];
            TimePolygonPoint v1 = nextTimePolygon.points[pointIndex];
            TimePolygonPoint v2 = isLast ?
                nextTimePolygon.points[pointIndex + 1 - _nPoints] :
                nextTimePolygon.points[pointIndex + 1];
            TimePolygonPoint v3 = isLast ?
                currentTimePolygon.points[pointIndex + 1 - _nPoints] :
                currentTimePolygon.points[pointIndex + 1];

            // Calculate normal of this section of the tube
            glm::dvec3 toNextPoly = glm::normalize(v1.coordinate - v0.coordinate);
            glm::dvec3 toNextPoint = glm::normalize(v3.coordinate - v0.coordinate);
            glm::dvec3 normal = glm::cross(toNextPoint, toNextPoly);

            // Create the Verticies for all points in this section
            PolygonVertex sidePointTriangleV0, sidePointTriangleV1, sidePointTriangleV2,
                sidePointTriangleV3;

            // Position
            sidePointTriangleV0.position[0] = v0.coordinate.x;
            sidePointTriangleV0.position[1] = v0.coordinate.y;
            sidePointTriangleV0.position[2] = v0.coordinate.z;

            sidePointTriangleV1.position[0] = v1.coordinate.x;
            sidePointTriangleV1.position[1] = v1.coordinate.y;
            sidePointTriangleV1.position[2] = v1.coordinate.z;

            sidePointTriangleV2.position[0] = v2.coordinate.x;
            sidePointTriangleV2.position[1] = v2.coordinate.y;
            sidePointTriangleV2.position[2] = v2.coordinate.z;

            sidePointTriangleV3.position[0] = v3.coordinate.x;
            sidePointTriangleV3.position[1] = v3.coordinate.y;
            sidePointTriangleV3.position[2] = v3.coordinate.z;

            // Texture coordinate
            sidePointTriangleV0.tex[0] = v0.tex.x;
            sidePointTriangleV0.tex[1] = v0.tex.y;

            sidePointTriangleV1.tex[0] = v1.tex.x;
            sidePointTriangleV1.tex[1] = v1.tex.y;

            sidePointTriangleV2.tex[0] = v2.tex.x;
            sidePointTriangleV2.tex[1] = v2.tex.y;

            sidePointTriangleV3.tex[0] = v3.tex.x;
            sidePointTriangleV3.tex[1] = v3.tex.y;

            if (_hasColorMapFile) {
                // Value
                sidePointTriangleV0.value =
                    _colorDataset.entries[pointCounter].data[colorParamIndex];
                sidePointTriangleV1.value =
                    _colorDataset.entries[pointCounter + _nPoints].data[colorParamIndex];
                sidePointTriangleV2.value =
                    _colorDataset.entries[isLast ? pointCounter + 1 : pointCounter + _nPoints + 1].data[colorParamIndex];
                sidePointTriangleV3.value =
                    _colorDataset.entries[isLast ? pointCounter + 1 - _nPoints : pointCounter + 1].data[colorParamIndex];
                ++pointCounter;
            }

            // Normal
            sidePointTriangleV0.normal[0] = normal.x;
            sidePointTriangleV0.normal[1] = normal.y;
            sidePointTriangleV0.normal[2] = normal.z;

            sidePointTriangleV1.normal[0] = normal.x;
            sidePointTriangleV1.normal[1] = normal.y;
            sidePointTriangleV1.normal[2] = normal.z;

            sidePointTriangleV2.normal[0] = normal.x;
            sidePointTriangleV2.normal[1] = normal.y;
            sidePointTriangleV2.normal[2] = normal.z;

            sidePointTriangleV3.normal[0] = normal.x;
            sidePointTriangleV3.normal[1] = normal.y;
            sidePointTriangleV3.normal[2] = normal.z;

            // Add all points to the list
            _verticies.push_back(sidePointTriangleV0);
            _verticies.push_back(sidePointTriangleV1);
            _verticies.push_back(sidePointTriangleV2);
            _verticies.push_back(sidePointTriangleV3);
        }
    }

    // Add the top
    if (_addEdges) {
        addTop(pointCounter, topCenter, topNormal, colorParamIndex);
    }

    // Indicies
    unsigned int nPointsPerSection = 4;
    unsigned int vIndex = _addEdges ? _nPoints + 1 : 0;

    // Add Indices for bottom
    if (_addEdges) {
        unsigned int bottomCenterIndex = 0;
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            unsigned int vIndex = pointIndex + 1;
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = bottomCenterIndex;
            unsigned int v1 = vIndex;
            unsigned int v2 = isLast ? v0 + 1 : vIndex + 1;

            _indicies.push_back(v0);
            _indicies.push_back(v1);
            _indicies.push_back(v2);
        }
    }

    // Indices for side triangles
    for (unsigned int polyIndex = 0; polyIndex < _nPolygons - 1; ++polyIndex) {
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = vIndex;
            unsigned int v1 = v0 + 1;
            unsigned int v2 = v1 + 1;
            unsigned int v3 = v2 + 1;

            // 2 triangles per sector
            _indicies.push_back(v0);
            _indicies.push_back(v1);
            _indicies.push_back(v2);

            _indicies.push_back(v0);
            _indicies.push_back(v2);
            _indicies.push_back(v3);

            vIndex += nPointsPerSection;
        }
    }

    // Add Indices for top
    if (_addEdges) {
        unsigned int topCenterIndex = _verticies.size() - 1;
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            unsigned int vIndex = topCenterIndex - pointIndex - 1;
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = topCenterIndex;
            unsigned int v1 = vIndex;
            unsigned int v2 = isLast ? v0 - 1 : vIndex - 1;

            _indicies.push_back(v0);
            _indicies.push_back(v1);
            _indicies.push_back(v2);
        }
    }
}

void RenderableTube::addBottom(int pointCounter, const glm::dvec3& bottomCenter,
                               const glm::dvec3& bottomNormal, int colorParamIndex)
{
    // Calculate the transfer function value for the center point of the bottom
    // Get the color for selected color parameter
    float bottomCenterValue = 0.f;
    if (_hasColorMapFile) {
        int loopPointCounter = pointCounter;
        for (size_t pointIndex = 0; pointIndex < _data.front().points.size(); ++pointIndex) {
            bottomCenterValue += _colorDataset.entries[loopPointCounter++].data[colorParamIndex];
        }
        bottomCenterValue /= _nPoints;
    }

    // Calculate texture coordinate
    glm::vec2 bottomCenterTex = glm::vec2(0.f);
    for (const TimePolygonPoint& timePolygonPoint : _data.front().points) {
        bottomCenterTex += timePolygonPoint.tex;
    }
    bottomCenterTex /= _nPoints;

    // Add the bottom's center point
    PolygonVertex bottomCenterPoint;
    bottomCenterPoint.position[0] = bottomCenter.x;
    bottomCenterPoint.position[1] = bottomCenter.y;
    bottomCenterPoint.position[2] = bottomCenter.z;

    bottomCenterPoint.tex[0] = bottomCenterTex.x;
    bottomCenterPoint.tex[1] = bottomCenterTex.y;

    bottomCenterPoint.normal[0] = bottomNormal.x;
    bottomCenterPoint.normal[1] = bottomNormal.y;
    bottomCenterPoint.normal[2] = bottomNormal.z;

    if (_hasColorMapFile) {
        bottomCenterPoint.value = bottomCenterValue;
    }
    _verticies.push_back(bottomCenterPoint);

    // Add the bottom's sides with proper normals
    // This will ensure a hard shadow on the tube edge
    for (const TimePolygonPoint& timePolygonPoint : _data.front().points) {
        PolygonVertex bottomSidePoint;
        bottomSidePoint.position[0] = timePolygonPoint.coordinate.x;
        bottomSidePoint.position[1] = timePolygonPoint.coordinate.y;
        bottomSidePoint.position[2] = timePolygonPoint.coordinate.z;

        bottomSidePoint.tex[0] = timePolygonPoint.tex.x;
        bottomSidePoint.tex[1] = timePolygonPoint.tex.y;

        bottomSidePoint.normal[0] = bottomNormal.x;
        bottomSidePoint.normal[1] = bottomNormal.y;
        bottomSidePoint.normal[2] = bottomNormal.z;

        if (_hasColorMapFile) {
            bottomSidePoint.value = _colorDataset.entries[pointCounter++].data[colorParamIndex];
        }
        _verticies.push_back(bottomSidePoint);
    }
}

void RenderableTube::addTop(int pointCounter, const glm::dvec3& topCenter,
                            const glm::dvec3& topNormal, int colorParamIndex)
{
    // Calculate the transfer function value for the center point of the top
    // Get the color for selected color parameter
    float topCenterValue = 0.f;
    if (_hasColorMapFile) {
        int loopPointCounter = pointCounter;
        for (const TimePolygonPoint& timePolygonPoint : _data.back().points) {
            topCenterValue += _colorDataset.entries[loopPointCounter++].data[colorParamIndex];
        }
        topCenterValue /= _nPoints;
    }

    // Calculate texture coordinate
    glm::vec2 topCenterTex = glm::vec2(0.f);
    for (const TimePolygonPoint& timePolygonPoint : _data.back().points) {
        topCenterTex += timePolygonPoint.tex;
    }
    topCenterTex /= _nPoints;

    // Add the top's sides with proper normals
    // This will ensure a hard shadow on the tube edge
    for (const TimePolygonPoint& timePolygonPoint : _data.back().points) {
        PolygonVertex topSidePoint;
        topSidePoint.position[0] = timePolygonPoint.coordinate.x;
        topSidePoint.position[1] = timePolygonPoint.coordinate.y;
        topSidePoint.position[2] = timePolygonPoint.coordinate.z;

        topSidePoint.tex[0] = timePolygonPoint.tex.x;
        topSidePoint.tex[1] = timePolygonPoint.tex.y;

        topSidePoint.normal[0] = topNormal.x;
        topSidePoint.normal[1] = topNormal.y;
        topSidePoint.normal[2] = topNormal.z;

        if (_hasColorMapFile) {
            topSidePoint.value = _colorDataset.entries[pointCounter++].data[colorParamIndex];
        }
        _verticies.push_back(topSidePoint);
    }

    // Add the top's center point
    PolygonVertex topCenterPoint;
    topCenterPoint.position[0] = topCenter.x;
    topCenterPoint.position[1] = topCenter.y;
    topCenterPoint.position[2] = topCenter.z;

    topCenterPoint.tex[0] = topCenterTex.x;
    topCenterPoint.tex[1] = topCenterTex.y;

    topCenterPoint.normal[0] = topNormal.x;
    topCenterPoint.normal[1] = topNormal.y;
    topCenterPoint.normal[2] = topNormal.z;

    if (_hasColorMapFile) {
        topCenterPoint.value = topCenterValue;
    }
    _verticies.push_back(topCenterPoint);
}

void RenderableTube::createSmoothEnding(double now) {
    // Get the selected color parameter
    int colorParamIndex = currentColorParameterIndex();

    // Verticies
    // Add the points of the polygon before now
    size_t polyIndex = _lastPolygonBeforeNow;
    for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
        PolygonVertex sidePoint;
        TimePolygonPoint sidePointData = _data[polyIndex].points[pointIndex];
        sidePoint.position[0] = sidePointData.coordinate.x;
        sidePoint.position[1] = sidePointData.coordinate.y;
        sidePoint.position[2] = sidePointData.coordinate.z;

        // Texture coordinate
        sidePoint.tex[0] = sidePointData.tex.x;
        sidePoint.tex[1] = sidePointData.tex.y;

        // Calculate normal
        glm::dvec3 centerLine = _data[polyIndex].center - _data[polyIndex + 1].center;
        glm::dvec3 normal = _data[polyIndex].points[pointIndex].coordinate -
            glm::proj(_data[polyIndex].points[pointIndex].coordinate, centerLine) -
            centerLine;

        sidePoint.normal[0] = normal.x;
        sidePoint.normal[1] = normal.y;
        sidePoint.normal[2] = normal.z;

        if (_hasColorMapFile) {
            sidePoint.value =
                _colorDataset.entries[polyIndex * _nPoints + pointIndex].data[colorParamIndex];
        }

        _verticiesEnding.push_back(sidePoint);
    }

    // Interpolate to find the values for the current polygon
    double prevTime = _data[_lastPolygonBeforeNow].timestamp;
    double nextTime = _data[_firstPolygonAfterNow].timestamp;
    double t = (now - prevTime) / (nextTime - prevTime);

    // Add the points for the current polygon
    polyIndex = _firstPolygonAfterNow;
    for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
        bool isLast = polyIndex == _nPolygons - 1;

        glm::dvec3 prevPolyPointPos = _data[_lastPolygonBeforeNow].points[pointIndex].coordinate;
        glm::dvec3 nextPolyPointPos = _data[_firstPolygonAfterNow].points[pointIndex].coordinate;
        glm::dvec3 currPolyPointPos = t * nextPolyPointPos + (1.0 - t) * prevPolyPointPos;

        PolygonVertex sidePoint;
        sidePoint.position[0] = currPolyPointPos.x;
        sidePoint.position[1] = currPolyPointPos.y;
        sidePoint.position[2] = currPolyPointPos.z;

        // Texture coordinate??

        // Calculate normal
        glm::dvec3 centerLine = isLast ?
            _data[polyIndex - 1].center - _data[polyIndex].center :
            _data[polyIndex].center - _data[polyIndex + 1].center;
        glm::dvec3 normal = _data[polyIndex].points[pointIndex].coordinate -
            glm::proj(_data[polyIndex].points[pointIndex].coordinate, centerLine) -
            centerLine;

        sidePoint.normal[0] = normal.x;
        sidePoint.normal[1] = normal.y;
        sidePoint.normal[2] = normal.z;

        if (_hasColorMapFile) {
            float prevPolyPointValue =
                _colorDataset.entries[polyIndex * _nPoints + pointIndex - 1].data[colorParamIndex];
            float currPolyPointValue =
                _colorDataset.entries[polyIndex * _nPoints + pointIndex].data[colorParamIndex];
            currPolyPointValue = t * currPolyPointValue + (1.0 - t) * prevPolyPointValue;

            sidePoint.value = currPolyPointValue;
        }

        _verticiesEnding.push_back(sidePoint);
    }

    // Add the cutplane for the current polygon
    if (_addEdges) {
        // Calculate normal
        glm::dvec3 cutplaneNormal =
            _data[_firstPolygonAfterNow].center - _data[_lastPolygonBeforeNow].center;
        float cutplaneCenterValue = 0.f;
        glm::dvec3 cutplaneCenterPoint = glm::dvec3(0.0);

        // Add the top's sides with proper normals
        // This will ensure a hard shadow on the tube edge
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            glm::dvec3 prevPolyPointPos = _data[_lastPolygonBeforeNow].points[pointIndex].coordinate;
            glm::dvec3 nextPolyPointPos = _data[_firstPolygonAfterNow].points[pointIndex].coordinate;
            glm::dvec3 currPolyPointPos = t * nextPolyPointPos + (1.0 - t) * prevPolyPointPos;

            PolygonVertex sidePoint;
            sidePoint.position[0] = currPolyPointPos.x;
            sidePoint.position[1] = currPolyPointPos.y;
            sidePoint.position[2] = currPolyPointPos.z;
            cutplaneCenterPoint += currPolyPointPos;

            // Texture coordinate??

            sidePoint.normal[0] = cutplaneNormal.x;
            sidePoint.normal[1] = cutplaneNormal.y;
            sidePoint.normal[2] = cutplaneNormal.z;

            if (_hasColorMapFile) {
                float prevPolyPointValue =
                    _colorDataset.entries[polyIndex * _nPoints + pointIndex - 1].data[colorParamIndex];
                float currPolyPointValue =
                    _colorDataset.entries[polyIndex * _nPoints + pointIndex].data[colorParamIndex];
                currPolyPointValue = t * currPolyPointValue + (1.0 - t) * prevPolyPointValue;

                sidePoint.value = currPolyPointValue;
                cutplaneCenterValue += currPolyPointValue;
            }

            _verticiesEnding.push_back(sidePoint);
        }

        // Add the cutplane's center point
        PolygonVertex topCenterPoint;
        cutplaneCenterPoint /= _nPoints;
        topCenterPoint.position[0] = cutplaneCenterPoint.x;
        topCenterPoint.position[1] = cutplaneCenterPoint.y;
        topCenterPoint.position[2] = cutplaneCenterPoint.z;

        // Texture coordinate??

        topCenterPoint.normal[0] = cutplaneNormal.x;
        topCenterPoint.normal[1] = cutplaneNormal.y;
        topCenterPoint.normal[2] = cutplaneNormal.z;

        if (_hasColorMapFile) {
            topCenterPoint.value = cutplaneCenterValue/_nPoints;
        }
        _verticiesEnding.push_back(topCenterPoint);
    }

    // Indicies
    for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
        bool isLast = pointIndex == _nPoints - 1;

        unsigned int v0 = pointIndex;
        unsigned int v1 = v0 + _nPoints;
        unsigned int v2 = isLast ? v0 + 1 : v1 + 1;
        unsigned int v3 = isLast ? v0 + 1 - _nPoints : v0 + 1;

        // 2 triangles per sector
        _indiciesEnding.push_back(v0);
        _indiciesEnding.push_back(v1);
        _indiciesEnding.push_back(v2);

        _indiciesEnding.push_back(v0);
        _indiciesEnding.push_back(v2);
        _indiciesEnding.push_back(v3);
    }

    // Add Indices for cutplane
    if (_addEdges) {
        unsigned int topCenterIndex = _verticiesEnding.size() - 1;
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            unsigned int vIndex = topCenterIndex - pointIndex - 1;
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = topCenterIndex;
            unsigned int v1 = vIndex;
            unsigned int v2 = isLast ? v0 - 1 : vIndex - 1;

            _indiciesEnding.push_back(v0);
            _indiciesEnding.push_back(v1);
            _indiciesEnding.push_back(v2);
        }
    }
}

void RenderableTube::createLowPolyEnding(double now) {
    // Get the selected color parameter
    int colorParamIndex = currentColorParameterIndex();

    // Interpolate to find current data
    double prevTime = _data[_lastPolygonBeforeNow].timestamp;
    double nextTime = _data[_firstPolygonAfterNow].timestamp;
    double t = (now - prevTime) / (nextTime - prevTime);

    // Verticies
    // Add the points of the polygon before now
    for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
        bool isLast = pointIndex == _nPoints - 1;

        TimePolygon prevTimePolygon = _data[_lastPolygonBeforeNow];
        TimePolygon nextTimePolygon = _data[_firstPolygonAfterNow];

        // Identify all the points that are included in this section
        TimePolygonPoint v0 = prevTimePolygon.points[pointIndex];
        TimePolygonPoint v1 = nextTimePolygon.points[pointIndex];
        TimePolygonPoint v2 = isLast ?
            nextTimePolygon.points[pointIndex + 1 - _nPoints] :
            nextTimePolygon.points[pointIndex + 1];
        TimePolygonPoint v3 = isLast ?
            prevTimePolygon.points[pointIndex + 1 - _nPoints] :
            prevTimePolygon.points[pointIndex + 1];

        // Interpolate the points related to the next polygon
        glm::dvec3 v1Pos = glm::dvec3(0.0);
        glm::dvec3 v2Pos = glm::dvec3(0.0);
        v1Pos = t * v1.coordinate + (1.0 - t) * v0.coordinate;
        v2Pos = t * v2.coordinate + (1.0 - t) * v3.coordinate;

        // Calculate normal of this section of the tube
        glm::dvec3 toNextPoly = glm::normalize(v1Pos - v0.coordinate);
        glm::dvec3 toNextPoint = glm::normalize(v3.coordinate - v0.coordinate);
        glm::dvec3 normal = glm::cross(toNextPoint, toNextPoly);

        // Create the Verticies for all points in this section
        PolygonVertex sidePointTriangleV0, sidePointTriangleV1, sidePointTriangleV2,
            sidePointTriangleV3;

        // Position
        sidePointTriangleV0.position[0] = v0.coordinate.x;
        sidePointTriangleV0.position[1] = v0.coordinate.y;
        sidePointTriangleV0.position[2] = v0.coordinate.z;

        sidePointTriangleV1.position[0] = v1Pos.x;
        sidePointTriangleV1.position[1] = v1Pos.y;
        sidePointTriangleV1.position[2] = v1Pos.z;

        sidePointTriangleV2.position[0] = v2Pos.x;
        sidePointTriangleV2.position[1] = v2Pos.y;
        sidePointTriangleV2.position[2] = v2Pos.z;

        sidePointTriangleV3.position[0] = v3.coordinate.x;
        sidePointTriangleV3.position[1] = v3.coordinate.y;
        sidePointTriangleV3.position[2] = v3.coordinate.z;

        // Texture coordinate
        sidePointTriangleV0.tex[0] = v0.tex.x;
        sidePointTriangleV0.tex[1] = v0.tex.y;

        // Interpolate?
        //sidePointTriangleV1.tex[0] = v1.tex.x;
        //sidePointTriangleV1.tex[1] = v1.tex.y;

        // Interpolate?
        //sidePointTriangleV2.tex[0] = v2.tex.x;
        //sidePointTriangleV2.tex[1] = v2.tex.y;

        sidePointTriangleV3.tex[0] = v3.tex.x;
        sidePointTriangleV3.tex[1] = v3.tex.y;

        // Value
        if (_hasColorMapFile) {
            unsigned int pointCounter = _lastPolygonBeforeNow * _nPoints + pointIndex;

            sidePointTriangleV0.value =
                _colorDataset.entries[pointCounter].data[colorParamIndex];
            sidePointTriangleV1.value =
                _colorDataset.entries[pointCounter + _nPoints].data[colorParamIndex];
            sidePointTriangleV2.value =
                _colorDataset.entries[isLast ? pointCounter + 1 : pointCounter + _nPoints + 1].data[colorParamIndex];
            sidePointTriangleV3.value =
                _colorDataset.entries[isLast ? pointCounter + 1 - _nPoints : pointCounter + 1].data[colorParamIndex];

            sidePointTriangleV1.value =
                t * sidePointTriangleV1.value + (1.0 - t) * sidePointTriangleV0.value;
            sidePointTriangleV2.value =
                t * sidePointTriangleV2.value + (1.0 - t) * sidePointTriangleV3.value;
        }

        // Normal
        sidePointTriangleV0.normal[0] = normal.x;
        sidePointTriangleV0.normal[1] = normal.y;
        sidePointTriangleV0.normal[2] = normal.z;

        sidePointTriangleV1.normal[0] = normal.x;
        sidePointTriangleV1.normal[1] = normal.y;
        sidePointTriangleV1.normal[2] = normal.z;

        sidePointTriangleV2.normal[0] = normal.x;
        sidePointTriangleV2.normal[1] = normal.y;
        sidePointTriangleV2.normal[2] = normal.z;

        sidePointTriangleV3.normal[0] = normal.x;
        sidePointTriangleV3.normal[1] = normal.y;
        sidePointTriangleV3.normal[2] = normal.z;

        // Add all points to the list
        _verticiesEnding.push_back(sidePointTriangleV0);
        _verticiesEnding.push_back(sidePointTriangleV1);
        _verticiesEnding.push_back(sidePointTriangleV2);
        _verticiesEnding.push_back(sidePointTriangleV3);
    }

    if (_addEdges) {
        // Calculate normal
        glm::dvec3 cutplaneNormal =
            _data[_firstPolygonAfterNow].center - _data[_lastPolygonBeforeNow].center;
        float cutplaneCenterValue = 0.f;
        glm::dvec3 cutplaneCenterPoint = glm::dvec3(0.0);

        // Add the cutplane's sides with proper normals
        // This will ensure a hard shadow on the tube edge
        int polyIndex = _firstPolygonAfterNow;
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            glm::dvec3 prevPolyPointPos = _data[_lastPolygonBeforeNow].points[pointIndex].coordinate;
            glm::dvec3 nextPolyPointPos = _data[_firstPolygonAfterNow].points[pointIndex].coordinate;
            glm::dvec3 currPolyPointPos = t * nextPolyPointPos + (1.0 - t) * prevPolyPointPos;

            PolygonVertex sidePoint;
            sidePoint.position[0] = currPolyPointPos.x;
            sidePoint.position[1] = currPolyPointPos.y;
            sidePoint.position[2] = currPolyPointPos.z;
            cutplaneCenterPoint += currPolyPointPos;

            // Texture coordinate??

            sidePoint.normal[0] = cutplaneNormal.x;
            sidePoint.normal[1] = cutplaneNormal.y;
            sidePoint.normal[2] = cutplaneNormal.z;

            if (_hasColorMapFile) {
                float prevPolyPointValue =
                    _colorDataset.entries[polyIndex * _nPoints + pointIndex - 1].data[colorParamIndex];
                float currPolyPointValue =
                    _colorDataset.entries[polyIndex * _nPoints + pointIndex].data[colorParamIndex];
                currPolyPointValue = t * currPolyPointValue + (1.0 - t) * prevPolyPointValue;

                sidePoint.value = currPolyPointValue;
                cutplaneCenterValue += currPolyPointValue;
            }

            _verticiesEnding.push_back(sidePoint);
        }

        // Add the cutplane's center point
        PolygonVertex topCenterPoint;
        cutplaneCenterPoint /= _nPoints;
        topCenterPoint.position[0] = cutplaneCenterPoint.x;
        topCenterPoint.position[1] = cutplaneCenterPoint.y;
        topCenterPoint.position[2] = cutplaneCenterPoint.z;

        // Texture coordinate??

        topCenterPoint.normal[0] = cutplaneNormal.x;
        topCenterPoint.normal[1] = cutplaneNormal.y;
        topCenterPoint.normal[2] = cutplaneNormal.z;

        if (_hasColorMapFile) {
            topCenterPoint.value = cutplaneCenterValue / _nPoints;
        }
        _verticiesEnding.push_back(topCenterPoint);
    }

    // Indicies
    unsigned int nPointsPerSection = 4;
    unsigned int vIndex = 0;
    for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
        bool isLast = pointIndex == _nPoints - 1;

        unsigned int v0 = vIndex;
        unsigned int v1 = v0 + 1;
        unsigned int v2 = v1 + 1;
        unsigned int v3 = v2 + 1;

        // 2 triangles per sector
        _indiciesEnding.push_back(v0);
        _indiciesEnding.push_back(v1);
        _indiciesEnding.push_back(v2);

        _indiciesEnding.push_back(v0);
        _indiciesEnding.push_back(v2);
        _indiciesEnding.push_back(v3);

        vIndex += nPointsPerSection;
    }

    // Add Indices for cutplane
    if (_addEdges) {
        unsigned int topCenterIndex = _verticiesEnding.size() - 1;
        for (unsigned int pointIndex = 0; pointIndex < _nPoints; ++pointIndex) {
            unsigned int vIndex = topCenterIndex - pointIndex - 1;
            bool isLast = pointIndex == _nPoints - 1;

            unsigned int v0 = topCenterIndex;
            unsigned int v1 = vIndex;
            unsigned int v2 = isLast ? v0 - 1 : vIndex - 1;

            _indiciesEnding.push_back(v0);
            _indiciesEnding.push_back(v1);
            _indiciesEnding.push_back(v2);
        }
    }
}

void RenderableTube::render(const RenderData& data, RendererTasks&) {
    if (_nIndiciesToRender == 0) {
        return;
    }

    _shader->activate();

    // Model transform and view transform needs to be in double precision
    const glm::dmat4 modelViewTransform = calcModelViewTransform(data);
    glm::dmat4 normalTransform = glm::transpose(glm::inverse(modelViewTransform));

    // Uniforms
    _shader->setUniform(_uniformCache.opacity, opacity());

    _shader->setUniform(_uniformCache.modelViewTransform, modelViewTransform);
    _shader->setUniform(
        _uniformCache.projectionTransform,
        glm::dmat4(data.camera.projectionMatrix())
    );
    _shader->setUniform(_uniformCache.normalTransform, glm::mat3(normalTransform));

    // Settings
    if (!_enableFaceCulling) {
        glDisable(GL_CULL_FACE);
    }

    if (_drawWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

#ifndef __APPLE__
        glLineWidth(_wireLineWidth);
#else
        glLineWidth(1.f);
#endif
    }

    // Shading and light settings
    int nLightSources = 0;
    _lightIntensitiesBuffer.resize(_lightSources.size());
    _lightDirectionsViewSpaceBuffer.resize(_lightSources.size());
    for (const std::unique_ptr<LightSource>& lightSource : _lightSources) {
        if (!lightSource->isEnabled()) {
            continue;
        }
        _lightIntensitiesBuffer[nLightSources] = lightSource->intensity();
        _lightDirectionsViewSpaceBuffer[nLightSources] =
            lightSource->directionViewSpace(data);

        ++nLightSources;
    }

    if (_uniformCache.performShading != -1) {
        _shader->setUniform(_uniformCache.performShading, _shading.enabled);
    }

    if (_shading.enabled) {
        _shader->setUniform(_uniformCache.nLightSources, nLightSources);
        _shader->setUniform(_uniformCache.lightIntensities, _lightIntensitiesBuffer);
        _shader->setUniform(
            _uniformCache.lightDirectionsViewSpace,
            _lightDirectionsViewSpaceBuffer
        );

        _shader->setUniform(_uniformCache.ambientIntensity, _shading.ambientIntensity);
        _shader->setUniform(_uniformCache.diffuseIntensity, _shading.diffuseIntensity);
        _shader->setUniform(_uniformCache.specularIntensity, _shading.specularIntensity);
    }

    // Colormap settings
    bool useColorMap = _hasColorMapFile && _colorSettings.colorMapping->enabled &&
        _colorSettings.colorMapping->texture();
    _shader->setUniform(_uniformCache.useColorMap, useColorMap);

    _shader->setUniform(_uniformCache.color, _colorSettings.tubeColor);

    ghoul::opengl::TextureUnit colorMapTextureUnit;
    _shader->setUniform(_uniformCache.colorMapTexture, colorMapTextureUnit);

    if (useColorMap) {
        colorMapTextureUnit.activate();
        _colorSettings.colorMapping->texture()->bind();

        const glm::vec2 range = _colorSettings.colorMapping->valueRange;
        _shader->setUniform(_uniformCache.cmapRangeMin, range.x);
        _shader->setUniform(_uniformCache.cmapRangeMax, range.y);
        _shader->setUniform(
            _uniformCache.hideOutsideRange,
            _colorSettings.colorMapping->hideOutsideRange
        );

        _shader->setUniform(
            _uniformCache.nanColor,
            _colorSettings.colorMapping->nanColor
        );
        _shader->setUniform(
            _uniformCache.useNanColor,
            _colorSettings.colorMapping->useNanColor
        );

        _shader->setUniform(
            _uniformCache.aboveRangeColor,
            _colorSettings.colorMapping->aboveRangeColor
        );
        _shader->setUniform(
            _uniformCache.useAboveRangeColor,
            _colorSettings.colorMapping->useAboveRangeColor
        );

        _shader->setUniform(
            _uniformCache.belowRangeColor,
            _colorSettings.colorMapping->belowRangeColor
        );
        _shader->setUniform(
            _uniformCache.useBelowRangeColor,
            _colorSettings.colorMapping->useBelowRangeColor
        );
    }

    // Render
    glBindVertexArray(_vaoId);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(_nIndiciesToRender),
        GL_UNSIGNED_INT,
        nullptr
    );

    // Render the last section until now with interpolation
    if (_interpolationNeeded && !_showAllTube) {
        glBindVertexArray(_vaoIdEnding);
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(_indiciesEnding.size()),
            GL_UNSIGNED_INT,
            nullptr
        );
    }

    // Reset
    if (!_enableFaceCulling) {
        glEnable(GL_CULL_FACE);
    }

    if (_drawWireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        global::renderEngine->openglStateCache().resetLineState();
    }

    glBindVertexArray(0);
    global::renderEngine->openglStateCache().resetLineState();

    _shader->deactivate();
}

void RenderableTube::updateBufferData() {
    glBindVertexArray(_vaoId);
    glBindBuffer(GL_ARRAY_BUFFER, _vaoId);
    glBufferData(
        GL_ARRAY_BUFFER,
        _verticies.size() * sizeof(PolygonVertex),
        _verticies.data(),
        GL_STREAM_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _iboId);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        _indicies.size() * sizeof(unsigned int),
        _indicies.data(),
        GL_STREAM_DRAW
    );
}

void RenderableTube::update(const UpdateData& data) {
    if (_shader->isDirty()) {
        _shader->rebuildFromFile();
        ghoul::opengl::updateUniformLocations(*_shader, _uniformCache, UniformNames);
    }

    if (_tubeIsDirty) {
        updateTube();
        updateBufferData();
        //setBoundingSphere(_length * glm::compMax(data.modelTransform.scale));
        _tubeIsDirty = false;
    }

    if (_showAllTube) {
        _nIndiciesToRender = _indicies.size();
        return;
    }

    const double now = data.time.j2000Seconds();
    double prev = 0.0;
    double next = std::numeric_limits<double>::max();
    _lastPolygonBeforeNow = 0;
    _firstPolygonAfterNow = std::numeric_limits<size_t>::max();
    bool hasPrev = false;
    _interpolationNeeded = true;

    for (size_t i = 0; i < _data.size(); ++i) {
        // Found a time smaller than now
        if (_data[i].timestamp < now) {
            prev = _data[i].timestamp;
            _lastPolygonBeforeNow = i;
            hasPrev = true;
        }
        // Found a time larger than now
        else if (_data[i].timestamp > now && _data[i].timestamp < next) {
            next = _data[i].timestamp;
            _firstPolygonAfterNow = i;
        }
        // Found a time exactly equal to now
        else if (std::abs(_data[i].timestamp - now) <
                 std::numeric_limits<double>::epsilon())
        {
            prev = _data[i].timestamp;
            _lastPolygonBeforeNow = i;
            hasPrev = true;
            _interpolationNeeded = false;
            LDEBUG(fmt::format("Polygon nr: '{}' is exactly at NOW",
                _lastPolygonBeforeNow)
            );
        }
    }

    // How many points are to and including polygon _lastPolygonBeforeNow?
    const size_t nPolygons = _data.size();
    const size_t nPoints = _data.front().points.size();

    int nPointsUntilNow = 0;

    // Where on the tube are we located in time
    if (!hasPrev) {
        // Before the time of the tube, do not show anything
        nPointsUntilNow = 0;
        _interpolationNeeded = false;
        LDEBUG("Before");
    }
    else if (_lastPolygonBeforeNow == nPolygons - 1) {
        // The previous step before now is the last polygon,
        // either after the time of the full tube or just at the exact end of it,
        // either way, show all of the tube
        nPointsUntilNow = _indicies.size();
        _interpolationNeeded = false;
        LDEBUG("After or End");
    }
    else {
        // Somewhere in the middle of the tube
        // could also be the very first polygon, _lastPolygonBeforeNow == 0

        // First add the bottom if that property is turned on
        if (_addEdges) {
            // Show at least the bottom
            nPointsUntilNow += static_cast<int>(nPoints * 3);
        }

        // Show the sides until _lastPolygonBeforeNow
        nPointsUntilNow += static_cast<int>(_lastPolygonBeforeNow * nPoints * 6);

        // We need at least one full side to show if we do not show the edges
        if (!_addEdges && _lastPolygonBeforeNow == 0) {
            LDEBUG("Nothing to show except edges");
            nPointsUntilNow = 0;
        }
    }

    LDEBUG(fmt::format("\nprev: '{}'\nnext: '{}'\nnPointsUntilNow: '{}'\n",
        _lastPolygonBeforeNow, _firstPolygonAfterNow, nPointsUntilNow)
    );

    if (nPointsUntilNow > _indicies.size()) {
        LERROR("Cannot render more verticies than what is in the tube");
        _nIndiciesToRender = 0;
    }
    else {
        _nIndiciesToRender = nPointsUntilNow;
    }

    // Update the ending of the tube
    if (_interpolationNeeded) {
        _verticiesEnding.clear();
        _indiciesEnding.clear();

        // Create the last piece of the tube
        if (_useSmoothNormals) {
            createSmoothEnding(now);
        }
        else {
            createLowPolyEnding(now);
        }

        // Update the buffer for the last piece
        glBindVertexArray(_vaoIdEnding);
        glBindBuffer(GL_ARRAY_BUFFER, _vboIdEnding);
        glBufferData(
            GL_ARRAY_BUFFER,
            _verticiesEnding.size() * sizeof(PolygonVertex),
            _verticiesEnding.data(),
            GL_STREAM_DRAW
        );

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _iboIdEnding);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            _indiciesEnding.size() * sizeof(unsigned int),
            _indiciesEnding.data(),
            GL_STREAM_DRAW
        );
    }

    glBindVertexArray(0);
}

} // namespace openspace