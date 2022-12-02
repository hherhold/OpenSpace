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

#include <modules/fieldlinessequence/rendering/renderablefieldlinessequencenew.h>

#include <openspace/engine/globals.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/documentation/documentation.h>

#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/textureunit.h>


namespace {
    constexpr std::string_view _loggerCat = "RenderableFieldlinesSequenceNew";

    constexpr openspace::properties::Property::PropertyInfo ColorMethodInfo = {
        "ColorMethod",
        "Color Method",
        "Color lines uniformly or using color tables based on extra quantities like, for "
        "examples, temperature or particle density"
    };
    constexpr openspace::properties::Property::PropertyInfo ColorQuantityInfo = {
        "ColorQuantity",
        "Quantity to Color By",
        "Quantity used to color lines if the 'By Quantity' color method is selected"
    };
    constexpr openspace::properties::Property::PropertyInfo ColorMinMaxInfo = {
        "ColorQuantityMinMax",
        "ColorTable Min Value",
        "Value to map to the lowest and highest end of the color table"
    };
    constexpr openspace::properties::Property::PropertyInfo ColorTablePathInfo = {
        "ColorTablePath",
        "Path to Color Table",
        "Color Table/Transfer Function to use for 'By Quantity' coloring"
    };
    constexpr openspace::properties::Property::PropertyInfo ColorUniformInfo = {
        "Color",
        "Uniform Line Color",
        "The uniform color of lines shown when 'Color Method' is set to 'Uniform'"
    };
    constexpr openspace::properties::Property::PropertyInfo ColorUseABlendingInfo = {
        "ABlendingEnabled",
        "Additive Blending",
        "Activate/deactivate additive blending"
    };

    constexpr openspace::properties::Property::PropertyInfo FlowEnabledInfo = {
        "FlowEnabled",
        "Flow Direction",
        "Toggles the rendering of moving particles along the lines. Can, for example, "
        "illustrate magnetic flow"
    };
    constexpr openspace::properties::Property::PropertyInfo FlowColorInfo = {
        "FlowColor",
        "Flow Color",
        "Color of particles flow direction indication"
    };
    constexpr openspace::properties::Property::PropertyInfo FlowReversedInfo = {
        "Reversed",
        "Reversed Flow",
        "Toggle to make the flow move in the opposite direction"
    };
    constexpr openspace::properties::Property::PropertyInfo FlowParticleSizeInfo = {
        "ParticleSize",
        "Particle Size",
        "Size of the particles"
    };
    constexpr openspace::properties::Property::PropertyInfo FlowParticleSpacingInfo = {
        "ParticleSpacing",
        "Particle Spacing",
        "Spacing inbetween particles"
    };
    constexpr openspace::properties::Property::PropertyInfo FlowSpeedInfo = {
        "Speed",
        "Speed",
        "Speed of the flow"
    };

    constexpr openspace::properties::Property::PropertyInfo MaskingEnabledInfo = {
        "MaskingEnabled",
        "Masking",
        "Enable/disable masking. Use masking to show lines where a given quantity is "
        "within a given range, for example, if you only want to see where the "
        "temperature is between 10 and 20 degrees. Also used for masking out line "
        "topologies like solar wind & closed lines"
    };
    constexpr openspace::properties::Property::PropertyInfo MaskingMinMaxInfo = {
        "MaskingMinLimit",
        "Lower Limit",
        "Lower and upper limit of the valid masking range"
    };
    constexpr openspace::properties::Property::PropertyInfo MaskingQuantityInfo = {
        "MaskingQuantity",
        "Quantity used for Masking",
        "Quantity used for masking"
    };


    struct [[codegen::Dictionary(RenderableFieldlinesSequenceNew)]] Parameters {
        // [[codegen::verbatim(ColorMethodInfo.description)]]
        std::optional<std::string> colorMethod;

        // [[codegen::verbatim(ColorQuantityInfo.description)]]
        std::optional<int> colorQuantity;

        // [[codegen::verbatim(ColorUniformInfo.description)]]
        std::optional<glm::vec4> color [[codegen::color()]];

        // A list of paths to transferfunction .txt files containing color tables
        // used for colorizing the fieldlines according to different parameters
        std::optional<std::vector<std::string>> colorTablePaths;

        // List of ranges for which their corresponding parameters values will be
        // colorized by. Should be entered as {min value, max value} per range
        std::optional<std::vector<glm::vec2>> colorTableRanges;


        // [[codegen::verbatim(FlowEnabledInfo.description)]]
        std::optional<bool> flowEnabled;
        // [[codegen::verbatim(FlowColorInfo.description)]]
        std::optional<glm::vec4> flowColor [[codegen::color()]];

        // [[codegen::verbatim(FlowReversedInfo.description)]]
        std::optional<bool> reversedFlow;

        // [[codegen::verbatim(FlowParticleSizeInfo.description)]]
        std::optional<int> particleSize;

        // [[codegen::verbatim(FlowParticleSpacingInfo.description)]]
        std::optional<int> particleSpacing;

        // [[codegen::verbatim(FlowSpeedInfo.description)]]
        std::optional<int> flowSpeed;

        // [[codegen::verbatim(MaskingEnabledInfo.description)]]
        std::optional<bool> maskingEnabled;
        // [[codegen::verbatim(MaskingQuantityInfo.description)]]
        std::optional<int> maskingQuantity;
        // List of ranges for which their corresponding parameters values will be
        // masked by. Should be entered as {min value, max value} per range
        std::optional<std::vector<glm::vec2>> maskingRanges;

    };
#include "renderablefieldlinessequencenew_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation RenderableFieldlinesSequenceNew::Documentation() {
    return codegen::doc<Parameters>("fieldlinessequence_renderablefieldlinessequencenew");
}

RenderableFieldlinesSequenceNew::RenderableFieldlinesSequenceNew(
                                                      const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _colorGroup({ "Color" })
    , _colorMethod(ColorMethodInfo, properties::OptionProperty::DisplayType::Radio)
    , _colorQuantity(ColorQuantityInfo, properties::OptionProperty::DisplayType::Dropdown)
    , _colorQuantityMinMax(
        ColorMinMaxInfo,
        glm::vec2(-0.f, 100.f),
        glm::vec2(-5000.f),
        glm::vec2(5000.f)
    )
    , _colorUniform(
        ColorUniformInfo,
        glm::vec4(0.3f, 0.57f, 0.75f, 0.5f),
        glm::vec4(0.f),
        glm::vec4(1.f)
    )
    , _colorABlendEnabled(ColorUseABlendingInfo, true)

    , _flowEnabled(FlowEnabledInfo, false)
    , _flowGroup({ "Flow" })
    , _flowColor(
        FlowColorInfo,
        glm::vec4(0.96f, 0.88f, 0.8f, 0.5f),
        glm::vec4(0.f),
        glm::vec4(1.f)
    )
    , _flowParticleSize(FlowParticleSizeInfo, 5, 0, 500)
    , _flowParticleSpacing(FlowParticleSpacingInfo, 60, 0, 500)
    , _flowReversed(FlowReversedInfo, false)
    , _flowSpeed(FlowSpeedInfo, 20, 0, 1000)

    , _maskingEnabled(MaskingEnabledInfo, false)
    , _maskingGroup({ "Masking" })
    , _maskingMinMax(
        MaskingMinMaxInfo,
        glm::vec2(0.f, 100.f),
        glm::vec2(-5000.f),
        glm::vec2(5000.f)
    )
    , _maskingQuantity(
        MaskingQuantityInfo,
        properties::OptionProperty::DisplayType::Dropdown
    )
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _flowEnabled = p.flowEnabled.value_or(_flowEnabled);
    _flowColor = p.flowColor.value_or(_flowColor);
    _flowReversed = p.reversedFlow.value_or(_flowReversed);
    _flowParticleSize = p.particleSize.value_or(_flowParticleSize);
    _flowParticleSpacing = p.particleSpacing.value_or(_flowParticleSpacing);
    _flowSpeed = p.flowSpeed.value_or(_flowSpeed);
    _maskingEnabled = p.maskingEnabled.value_or(_maskingEnabled);
    _maskingQuantity = p.maskingQuantity.value_or(_maskingQuantity);

    // Color group
    if (p.colorTablePaths.has_value()) {
        _colorTablePaths = p.colorTablePaths.value();
    }
    _colorUniform = p.color.value_or(_colorUniform);
    _colorMethod.addOption(static_cast<int>(ColorMethod::Uniform), "Uniform");
    _colorMethod.addOption(static_cast<int>(ColorMethod::ByQuantity), "By Quantity");
    if (p.colorMethod.has_value()) {
        if (p.colorMethod.value() == "Uniform") {
            _colorMethod = static_cast<int>(ColorMethod::Uniform);
        }
        else {
            _colorMethod = static_cast<int>(ColorMethod::ByQuantity);
        }
    }
    else {
        _colorMethod = static_cast<int>(ColorMethod::Uniform);
    }

    if (p.colorQuantity.has_value()) {
        _colorMethod = static_cast<int>(ColorMethod::ByQuantity);
    }

    if (p.colorTableRanges.has_value()) {
        _colorTableRanges = *p.colorTableRanges;
    }
    else {
        _colorTableRanges.push_back(glm::vec2(0.f, 1.f));
    }


    if (p.maskingRanges.has_value()) {
        _maskingRanges = *p.maskingRanges;
    }
    else {
        _maskingRanges.push_back(glm::vec2(-100000.f, 100000.f)); // some default values
    }



}

void RenderableFieldlinesSequenceNew::initialize() {
    //_transferFunction = std::make_unique<TransferFunction>(
    //    absPath(_colorTablePaths[0]).string()
    //);
}

void RenderableFieldlinesSequenceNew::initializeGL() {


    setupProperties();
}

void RenderableFieldlinesSequenceNew::setupProperties() {
    addProperty(_colorABlendEnabled);
    addProperty(_flowEnabled);
    addProperty(_maskingEnabled); //hasExtra

    // Add Property Groups
    addPropertySubOwner(_colorGroup);
    addPropertySubOwner(_flowGroup);
    addPropertySubOwner(_maskingGroup); // hasExtra

    _colorUniform.setViewOption(properties::Property::ViewOptions::Color);
    _colorGroup.addProperty(_colorUniform);
    _colorGroup.addProperty(_colorQuantity);// hasExtra

    _flowGroup.addProperty(_flowReversed);
    _flowColor.setViewOption(properties::Property::ViewOptions::Color);
    _flowGroup.addProperty(_flowColor);
    _flowGroup.addProperty(_flowParticleSize);
    _flowGroup.addProperty(_flowParticleSpacing);
    _flowGroup.addProperty(_flowSpeed);

    _maskingMinMax.setViewOption(properties::Property::ViewOptions::MinMaxRange);// hasExtra
    _maskingGroup.addProperty(_maskingMinMax);// hasExtra

    //if (!_maskingRanges.empty()) {
        _maskingMinMax = _maskingRanges[_colorQuantity];// hasExtra
    //}







}

void RenderableFieldlinesSequenceNew::definePropertyCallbackFunctions() {

    _colorQuantity.onChange([this]() {
        _transferFunction->setPath(_colorTablePaths[_colorQuantity]);
    });

    _maskingQuantity.onChange([this]() {
        _shouldUpdateMaskingBuffer = true;
        _maskingMinMax = _maskingRanges[_maskingQuantity];
    });

    _maskingMinMax.onChange([this]() {
        _maskingRanges[_maskingQuantity] = _maskingMinMax;
    });
}










void RenderableFieldlinesSequenceNew::deinitializeGL() {

}

bool RenderableFieldlinesSequenceNew::isReady() const {
    return true; // _shaderProgram != nullptr;
}

void RenderableFieldlinesSequenceNew::update(const UpdateData& data) {

}

void RenderableFieldlinesSequenceNew::render(const RenderData& data, RendererTasks&) {
    if (_colorMethod == static_cast<int>(ColorMethod::ByQuantity)) {
        ghoul::opengl::TextureUnit textureUnit;
        textureUnit.activate();
        _transferFunction->bind();
        _shaderProgram->setUniform("colorTable", textureUnit);
        _shaderProgram->setUniform("colorTableRange", _colorTableRanges[_colorQuantity]);
    }

    if (_maskingEnabled) {
        _shaderProgram->setUniform("maskingRange", _maskingRanges[_maskingQuantity]);
    }

    // Flow / Particles
    _shaderProgram->setUniform("flowColor", _flowColor);
    _shaderProgram->setUniform("usingParticles", _flowEnabled);
    _shaderProgram->setUniform("particleSize", _flowParticleSize);
    _shaderProgram->setUniform("particleSpacing", _flowParticleSpacing);
    _shaderProgram->setUniform("particleSpeed", _flowSpeed);
    _shaderProgram->setUniform(
        "time",
        global::windowDelegate->applicationTime() * (_flowReversed ? -1 : 1)
    );


}


} // namespace openspace
