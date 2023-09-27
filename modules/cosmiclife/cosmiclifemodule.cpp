/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2022                                                               *
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

#include <modules/cosmiclife/cosmiclifemodule.h>

/*#include <modules/digitaluniverse/rendering/renderablebillboardscloud.h>
#include <modules/digitaluniverse/rendering/renderabledumeshes.h>
#include <modules/digitaluniverse/rendering/renderableplanescloud.h>*/
#include <modules/cosmiclife/rendering/renderablecosmicpoints.h> 
#include <openspace/documentation/documentation.h>
#include <openspace/rendering/renderable.h>
#include <openspace/util/factorymanager.h>
#include <ghoul/misc/assert.h>
#include <ghoul/misc/templatefactory.h>

namespace openspace {

ghoul::opengl::ProgramObjectManager CosmicLifeModule::ProgramObjectManager;
ghoul::opengl::TextureManager CosmicLifeModule::TextureManager;

CosmicLifeModule::CosmicLifeModule()
    : OpenSpaceModule(CosmicLifeModule::Name)
{}

void CosmicLifeModule::internalInitialize(const ghoul::Dictionary&) {
    ghoul::TemplateFactory<Renderable>* fRenderable =
        FactoryManager::ref().factory<Renderable>();
    ghoul_assert(fRenderable, "Renderable factory was not created");

    fRenderable->registerClass<RenderableCosmicPoints>("RenderableCosmicPoints");
    // fRenderable->registerClass<RenderableBillboardsCloud>("RenderableBillboardsCloud");
    // fRenderable->registerClass<RenderablePlanesCloud>("RenderablePlanesCloud");
    // fRenderable->registerClass<RenderableDUMeshes>("RenderableDUMeshes");
}

void CosmicLifeModule::internalDeinitializeGL() {
    ProgramObjectManager.releaseAll(ghoul::opengl::ProgramObjectManager::Warnings::Yes);
    TextureManager.releaseAll(ghoul::opengl::TextureManager::Warnings::Yes);
}

std::vector<documentation::Documentation> CosmicLifeModule::documentations() const {
    return {
        RenderableCosmicPoints::Documentation(),
        // RenderableBillboardsCloud::Documentation(),
        // RenderablePlanesCloud::Documentation(),
        // RenderableDUMeshes::Documentation()
    };
}

} // namespace openspace