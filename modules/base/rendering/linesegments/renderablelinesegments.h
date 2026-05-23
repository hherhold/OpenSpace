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

#ifndef __OPENSPACE_MODULE_BASE___RENDERABLELINESEGMENTS___H__
#define __OPENSPACE_MODULE_BASE___RENDERABLELINESEGMENTS___H__

#include <openspace/rendering/renderable.h>

#include <openspace/data/dataloader.h>
#include <openspace/data/datamapping.h>
#include <openspace/properties/misc/stringproperty.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/properties/scalar/floatproperty.h>
#include <openspace/properties/scalar/uintproperty.h>
#include <openspace/properties/vector/vec3property.h>
#include <openspace/rendering/colormappingcomponent.h>
#include <openspace/util/distanceconversion.h>
#include <ghoul/glm.h>
#include <ghoul/opengl/ghoul_gl.h>
#include <ghoul/opengl/uniformcache.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ghoul::opengl { class ProgramObject; }

namespace openspace {

/**
 * This class describes a renderable that draws arbitrary lines based on a CSV data file
 * with 3D positions. Each row in the data file is a line vertex. If a line identifier
 * column is specified, consecutive rows that share the same identifier value are
 * connected into a polyline. Otherwise, every consecutive pair of rows is interpreted as
 * one separate line segment. The lines can optionally be colored based on a separate
 * column in the data file.
 */
class RenderableLineSegments : public Renderable {
public:
    explicit RenderableLineSegments(const ghoul::Dictionary& dictionary);
    ~RenderableLineSegments() override = default;

    void initialize() override;
    void initializeGL() override;
    void deinitializeGL() override;

    void render(const RenderData& data, RendererTasks& rendererTask) override;
    void update(const UpdateData& data) override;

    static openspace::Documentation Documentation();

protected:
    virtual void initializeShaders();
    virtual void deinitializeShaders();

    /**
     * Set any extra uniforms required by a subclass. Called while the shader program is
     * active, during rendering.
     */
    virtual void setExtraUniforms();

    /**
     * The number of float attributes that are stored in the vertex buffer per line
     * vertex.
     */
    virtual int nAttributesPerVertex() const;

    /**
     * The number of objects (line vertices) in the dataset that should be considered
     * when grouping rows into line segments. The base class uses all entries in the
     * dataset. Subclasses that store multiple time steps may return a smaller number.
     */
    virtual unsigned int nObjects() const;

    /**
     * Append the vertex buffer data for the line vertex corresponding to the given
     * dataset entry index, and update the maximum radius used for the bounding sphere.
     */
    virtual void addVertexData(unsigned int datasetIndex, std::vector<float>& result,
        double& maxRadius) const;

    /**
     * Set up the vertex attribute pointers for the currently bound vertex array and
     * buffer. Must match the layout produced by `addVertexData`.
     */
    virtual void setVertexAttributePointers();

    glm::dvec3 transformedPosition(const dataloader::Dataset::Entry& e) const;

    bool hasColorData() const;
    int currentColorParameterIndex() const;

    /**
     * Helper function to set up a single vertex attribute. Assumes that the value is a
     * float value.
     *
     * \return The updated offset after this attribute is added
     */
    int bufferVertexAttribute(const std::string& name, GLint nValues,
        int attributesPerVertex, int offset) const;

    /**
     * Walk the dataset and build the ordered list of dataset-entry indices that form the
     * line segments to render (two indices per segment).
     */
    void buildSegmentEndpoints();

    std::vector<float> createDataSlice();
    void updateBufferData();

    bool _dataIsDirty = true;
    bool _hasDataFile = false;
    bool _hasColorMapFile = false;
    bool _useCaching = true;

    struct ColorSettings : PropertyOwner {
        explicit ColorSettings(const ghoul::Dictionary& dictionary);
        Vec3Property fixedColor;
        std::unique_ptr<ColorMappingComponent> colorMapping;
    };
    ColorSettings _colorSettings;

    BoolProperty _drawElements;
    FloatProperty _lineWidth;
    UIntProperty _nLineSegments;

    std::filesystem::path _dataFile;

    std::string _lineIdentifierColumn;
    bool _hasLineIdentifierColumn = false;

    DistanceUnit _unit = DistanceUnit::Parsec;

    dataloader::Dataset _dataset;
    dataloader::DataMapping _dataMapping;

    /// Dataset-entry indices in draw order, two per line segment
    std::vector<unsigned int> _segmentEndpoints;

    ghoul::opengl::ProgramObject* _program = nullptr;

    UniformCache(
        modelViewTransform, projectionTransform, color, opacity, useColorMap,
        colorMapTexture, cmapRangeMin, cmapRangeMax, hideOutsideRange, nanColor,
        useNanColor, aboveRangeColor, useAboveRangeColor, belowRangeColor,
        useBelowRangeColor
    ) _uniformCache;

    GLuint _vao = 0;
    GLuint _vbo = 0;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_BASE___RENDERABLELINESEGMENTS___H__
