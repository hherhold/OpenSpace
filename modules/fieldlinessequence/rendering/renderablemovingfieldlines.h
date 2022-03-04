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

#ifndef __OPENSPACE_MODULE_FIELDLINESSEQUENCE___RENDERABLEMOVINGFIELDLINES___H__
#define __OPENSPACE_MODULE_FIELDLINESSEQUENCE___RENDERABLEMOVINGFIELDLINES___H__

#include <openspace/rendering/renderable.h>

#include <modules/fieldlinessequence/util/fieldlinesstate.h>
#include <modules/fieldlinessequence/util/movingfieldlinehelper.h>
#include <modules/fieldlinessequence/util/matchingfieldlineshelper.h>
#include <openspace/properties/optionproperty.h>
#include <openspace/properties/vector/vec2property.h>
#include <openspace/properties/vector/vec4property.h>

#include <openspace/rendering/transferfunction.h>

namespace openspace {

namespace documentation { struct Documentation; }

class RenderableMovingFieldlines : public Renderable {
public:
    struct PathLineTraverser {
        PathLineTraverser(const std::vector<FieldlinesState::Fieldline>& fieldlines_);
        void advanceCurrent();
        bool isAtEnd() const;
        std::vector<FieldlinesState::Fieldline>::const_iterator nextFieldline();

        const std::vector<FieldlinesState::Fieldline>& fieldlines;
        float timeSinceInterpolation = 0.f;
        bool forward = true;
        std::vector<FieldlinesState::Fieldline>::const_iterator currentFieldline;

    };


    RenderableMovingFieldlines(const ghoul::Dictionary& dictionary);
    void initialize() override;
    void initializeGL() override;
    void deinitializeGL() override;

    bool isReady() const override;

    void render(const RenderData& data, RendererTasks& rendertask) override;
    void update(const UpdateData& data) override;
    static documentation::Documentation Documentation();

private:
    bool getStateFromCdfFiles();
    void updateVertexPositionBuffer();
    void updateVertexColorBuffer();
    void moveLines(const double dt);
    
    template <bool LerpLine>
    void setNewRenderedLinePosition(
        PathLineTraverser traverser,
        GLint lineStart, GLsizei nVertices);
    
    void moveLine(const double dt, const FieldlinesState::PathLine& pathLine,
        PathLineTraverser& traverser, GLint lineStart,
        GLsizei nVertices);

    enum class ColorMethod {
        Uniform = 0,
        ByQuantity = 1
    };

    // Line width for the line rendering part
    properties::FloatProperty _lineWidth;
    // Renders flow line/ pathline if true
    properties::BoolProperty _renderFlowLine;
    // Group to hold the color properties
    properties::PropertyOwner _colorGroup;
    // Uniform or transfer function
    properties::OptionProperty _colorMethod;
    // Uniform Field Line Color
    properties::Vec4Property _colorUniform;
    // Values represents min & max values represented in the color table
    std::vector<glm::vec2> _colorTableRanges;
    // Index of the extra quantity to color lines by
    properties::OptionProperty _colorQuantity;
    // Used to save property for later initialization
    int _colorQuantityTemp = 0;
    // Color table/transfer function min and max range
    properties::Vec2Property _colorQuantityMinMax;
    // Paths to color tables. One for each 'extraQuantity'
    std::vector<std::string> _colorTablePaths;
    // Color table/transfer function for "By Quantity" coloring
    properties::StringProperty _colorTablePath;

    std::unique_ptr<ghoul::opengl::ProgramObject> _shaderProgram;
    // Transfer function used to color lines when _pColorMethod is set to BY_QUANTITY
    std::unique_ptr<TransferFunction> _transferFunction;
    // True when new state is loaded or user change which quantity to color the lines by
    bool _shouldUpdateColorBuffer = false;
    // OpenGL Vertex Array Object
    GLuint _vertexArrayObject = 0;
    // OpenGL Vertex Buffer Object containing the vertex positions
    GLuint _vertexPositionBuffer = 0;
    // OpenGL Vertex Buffer Object containing the extraQuantity values used for coloring
    // the lines
    GLuint _vertexColorBuffer = 0;
    GLuint _vertexArrayObjectFlow = 0;
    GLuint _vertexPositionBufferFlow = 0;

    FieldlinesState _fieldlineState;
    double _manualTimeOffset = 0.0;
    std::vector<std::filesystem::path> _sourceFiles;
    std::filesystem::path _seedFilePath;
    std::vector<glm::vec3> _seedPoints;
    // Extra variables such as rho, p or t
    std::vector<std::string> _extraVars;
    size_t _nPointsOnPathLine = 200;
    size_t _nPointsOnFieldlines = 100;
    // which tracing vaiable to trace. 'b' for fieldline is default
    std::string _tracingVariable = "u_perp_b";

    std::vector<glm::vec3> _renderedLines;
    std::vector<float> _debugTopologyColor;
    std::vector<PathLineTraverser> _traversers;
};
}
#endif // __OPENSPACE_MODULE_FIELDLINESSEQUENCE___RENDERABLEMOVINGFIELDLINES___H__