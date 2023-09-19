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

#include <modules/digitaluniverse/rendering/renderablepolygoncloud.h>

#include <modules/digitaluniverse/digitaluniversemodule.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/util/updatestructures.h>
#include <openspace/rendering/renderengine.h>
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
#include <ghoul/opengl/textureunit.h>
#include <glm/gtx/string_cast.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <optional>
#include <string>

namespace {
    constexpr std::string_view _loggerCat = "RenderablePolygonCloud";

    struct [[codegen::Dictionary(RenderablePolygonCloud)]] Parameters {
        // The number of sides for the polygon used to represent each point
        std::optional<int> polygonSides;
    };

#include "renderablepolygoncloud_codegen.cpp"
}  // namespace

namespace openspace {

documentation::Documentation RenderablePolygonCloud::Documentation() {
    return codegen::doc<Parameters>("digitaluniverse_RenderablePolygonCloud");
}

RenderablePolygonCloud::RenderablePolygonCloud(const ghoul::Dictionary& dictionary)
    : RenderableBillboardsCloud(dictionary)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _polygonSides = p.polygonSides.value_or(_polygonSides);
}

bool RenderablePolygonCloud::isReady() const {
    return RenderableBillboardsCloud::isReady() && _pTexture;
}

void RenderablePolygonCloud::initializeGL() {
    ZoneScoped;

    RenderableBillboardsCloud::initializeGL();
    createPolygonTexture();
}

void RenderablePolygonCloud::deinitializeGL() {
    glDeleteBuffers(1, &_polygonVbo);
    _polygonVbo = 0;
    glDeleteVertexArrays(1, &_polygonVao);
    _polygonVao = 0;

    _polygonTexture = nullptr;
    glDeleteTextures(1, &_pTexture);
}

void RenderablePolygonCloud::bindTextureForRendering() const {
    glBindTexture(GL_TEXTURE_2D, _pTexture);
}

void RenderablePolygonCloud::update(const UpdateData& data) {
    ZoneScoped;

    RenderableBillboardsCloud::update(data);

    // TODO: Polygon texture is dirty
    //if (_spriteTextureIsDirty) {
    //    updateSpriteTexture();
    //}
}

void RenderablePolygonCloud::createPolygonTexture() {
    ZoneScoped;

    LDEBUG("Creating Polygon Texture");

    glGenTextures(1, &_pTexture);
    glBindTexture(GL_TEXTURE_2D, _pTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Stopped using a buffer object for GL_PIXEL_UNPACK_BUFFER
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_BYTE, nullptr);

    renderToTexture(_pTexture, 256, 256);
}

void RenderablePolygonCloud::renderToTexture(GLuint textureToRenderTo,
                                             GLuint textureWidth, GLuint textureHeight)
{
    LDEBUG("Rendering to Texture");

    // Saves initial Application's OpenGL State
    GLint defaultFBO;
    GLint viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defaultFBO);
    glGetIntegerv(GL_VIEWPORT, viewport);

    GLuint textureFBO;
    glGenFramebuffers(1, &textureFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, textureFBO);
    GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBuffers);

    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureToRenderTo, 0);

    glViewport(viewport[0], viewport[1], textureWidth, textureHeight);

    loadPolygonGeometryForRendering();
    renderPolygonGeometry(_polygonVao);

    // Restores Applications' OpenGL State
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    glDeleteBuffers(1, &_polygonVbo);
    glDeleteVertexArrays(1, &_polygonVao);
    glDeleteFramebuffers(1, &textureFBO);
}

void RenderablePolygonCloud::loadPolygonGeometryForRendering() {
    if (_polygonVao == 0) {
        glGenVertexArrays(1, &_polygonVao);
    }
    if (_polygonVbo == 0) {
        glGenBuffers(1, &_polygonVbo);
    }

    glBindVertexArray(_polygonVao);
    glBindBuffer(GL_ARRAY_BUFFER, _polygonVbo);

    constexpr std::array<GLfloat, 4> VertexData = {
        //      x      y     z     w
        0.f, 0.f, 0.f, 1.f,
    };

    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexData), VertexData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void RenderablePolygonCloud::renderPolygonGeometry(GLuint vao) {
    std::unique_ptr<ghoul::opengl::ProgramObject> program =
        ghoul::opengl::ProgramObject::Build(
            "RenderableBillboardsCloud_Polygon",
            absPath("${MODULE_DIGITALUNIVERSE}/shaders/billboardpolygon_vs.glsl"),
            absPath("${MODULE_DIGITALUNIVERSE}/shaders/billboardpolygon_fs.glsl"),
            absPath("${MODULE_DIGITALUNIVERSE}/shaders/billboardpolygon_gs.glsl")
        );

    program->activate();
    constexpr glm::vec4 Black = glm::vec4(0.f, 0.f, 0.f, 0.f);
    glClearBufferfv(GL_COLOR, 0, glm::value_ptr(Black));

    program->setUniform("sides", _polygonSides);
    program->setUniform("polygonColor", _pointColor);

    glBindVertexArray(vao);
    glDrawArrays(GL_POINTS, 0, 1);
    glBindVertexArray(0);

    program->deactivate();
}

} // namespace openspace
