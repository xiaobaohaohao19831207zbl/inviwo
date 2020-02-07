/*********************************************************************************
 *
 * Inviwo - Interactive Visualization Workshop
 *
 * Copyright (c) 2019 Inviwo Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************************/

#include <modules/meshrenderinggl/rendering/fragmentlistrenderer.h>
#include <modules/opengl/geometry/meshgl.h>
#include <modules/opengl/sharedopenglresources.h>
#include <modules/opengl/openglutils.h>
#include <modules/opengl/texture/textureutils.h>
#include <modules/opengl/image/imagegl.h>
#include <modules/opengl/openglcapabilities.h>

#include <cstdio>
#include <fmt/format.h>

namespace inviwo {

FragmentListRenderer::Illustration::Illustration(size2_t screenSize, size_t fragmentSize)
    : index{screenSize, GL_RED, GL_R32F, GL_FLOAT, GL_NEAREST}
    , count{screenSize, GL_RED, GL_R32F, GL_FLOAT, GL_NEAREST}
    , color{fragmentSize * 2 * sizeof(GLfloat), GLFormats::getGLFormat(GL_FLOAT, 2),
            GL_DYNAMIC_DRAW, GL_SHADER_STORAGE_BUFFER}
    , surfaceInfo{fragmentSize * 2 * sizeof(GLfloat), GLFormats::getGLFormat(GL_FLOAT, 2),
                  GL_DYNAMIC_DRAW, GL_SHADER_STORAGE_BUFFER}
    , smoothing{{{fragmentSize * 2 * sizeof(GLfloat), GLFormats::getGLFormat(GL_FLOAT, 2),
                  GL_DYNAMIC_DRAW, GL_SHADER_STORAGE_BUFFER},
                 {fragmentSize * 2 * sizeof(GLfloat), GLFormats::getGLFormat(GL_FLOAT, 2),
                  GL_DYNAMIC_DRAW, GL_SHADER_STORAGE_BUFFER}}}
    , activeSmoothing{0}
    , fill{"simplequad.vert", "sortandfillillustrationbuffer.frag", false}
    , resolveNeighbors{"simplequad.vert", "resolveneighborsillustrationbuffer.frag", false}
    , draw{"simplequad.vert", "displayillustrationbuffer.frag", false}
    , smooth{"simplequad.vert", "smoothillustrationbuffer.frag", false}
    , settings{} {

    index.initialize(nullptr);
    count.initialize(nullptr);
}

FragmentListRenderer::FragmentListRenderer()
    : screenSize_{0, 0}
    , fragmentSize_{1024}

    , abufferIdxTex_{screenSize_, GL_RED, GL_R32F, GL_FLOAT, GL_NEAREST}
    , textureUnits_{}
    , atomicCounter_{sizeof(GLuint), GLFormats::getGLFormat(GL_UNSIGNED_INT, 1), GL_DYNAMIC_DRAW,
                     GL_ATOMIC_COUNTER_BUFFER}
    , pixelBuffer_{fragmentSize_ * 4 * sizeof(GLfloat), GLFormats::getGLFormat(GL_FLOAT, 4),
                   GL_DYNAMIC_DRAW, GL_SHADER_STORAGE_BUFFER}
    , totalFragmentQuery_{0}
    , clear_("simplequad.vert", "oit/clearabufferlinkedlist.frag", false)
    , display_("simplequad.vert", "oit/dispabufferlinkedlist.frag", false)
    , illustration_{screenSize_, fragmentSize_} {

    buildShaders();

    abufferIdxTex_.initialize(nullptr);

    // create fragment query
    glGenQueries(1, &totalFragmentQuery_);
    LGL_ERROR;
}

FragmentListRenderer::~FragmentListRenderer() {
    if (totalFragmentQuery_) glDeleteQueries(1, &totalFragmentQuery_);
}

void FragmentListRenderer::prePass(const size2_t& screenSize) {
    resizeBuffers(screenSize);

    // reset counter

    GLuint v[1] = {0};
    atomicCounter_.upload(v, sizeof(GLuint));
    atomicCounter_.unbind();

    // clear textures
    clear_.activate();
    auto& texUnit = textureUnits_.emplace_back();
    setUniforms(clear_, texUnit);

    utilgl::GlBoolState depthTest(GL_DEPTH_TEST, GL_TRUE);
    utilgl::DepthMaskState depthMask(GL_TRUE);
    utilgl::DepthFuncState depthFunc(GL_ALWAYS);
    utilgl::CullFaceState culling(GL_NONE);
    utilgl::singleDrawImagePlaneRect();

    clear_.deactivate();

    // memory barrier
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    // start query
    // The query is used to determinate the size needed for the shader storage buffer
    // to store all the fragments.
    glBeginQuery(GL_SAMPLES_PASSED, totalFragmentQuery_);
    LGL_ERROR;
}

bool FragmentListRenderer::postPass(bool useIllustration, bool debug) {
    // memory barrier
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    LGL_ERROR;

    // get query result
    GLuint numFrags = 0;
    glEndQuery(GL_SAMPLES_PASSED);
    glGetQueryObjectuiv(totalFragmentQuery_, GL_QUERY_RESULT, &numFrags);
    LGL_ERROR;

    if (debug) debugFragmentLists(numFrags);

    // check if enough space was available
    if (numFrags > fragmentSize_) {
        // we have to resize the fragment storage buffer
        LogInfo("fragment lists resolved, pixels drawn: "
                << numFrags << ", available: " << fragmentSize_ << ", allocate space for "
                << int(1.1f * numFrags) << " pixels");
        fragmentSize_ = static_cast<size_t>(1.1f * numFrags);

        // unbind texture
        textureUnits_.clear();
        return false;
    }

    if (!useIllustration) {
        // render fragment list
        display_.activate();
        setUniforms(display_, textureUnits_[0]);
        utilgl::BlendModeState blendModeStateGL(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        utilgl::GlBoolState depthTest(GL_DEPTH_TEST, GL_TRUE);
        utilgl::DepthMaskState depthMask(GL_TRUE);
        utilgl::DepthFuncState depthFunc(GL_ALWAYS);
        utilgl::CullFaceState culling(GL_NONE);
        utilgl::singleDrawImagePlaneRect();
        display_.deactivate();
    }

    // Note: illustration buffers are only called when enough space was available.
    // This allows us to drop the tests for overflow
    TextureUnit idxUnit;
    TextureUnit countUnit;
    if (useIllustration) {  // 1. copy to illustration buffer
        illustration_.resizeBuffers(screenSize_, fragmentSize_);
        fillIllustration(textureUnits_[0], idxUnit, countUnit);
    }

    // unbind texture with abuffer indices
    textureUnits_.clear();

    if (useIllustration) {  // 2. perform all the crazy post-processing steps
        illustration_.processIllustration(pixelBuffer_, idxUnit, countUnit);
        illustration_.drawIllustration(idxUnit, countUnit);

        if (debug) debugIllustrationBuffer(numFrags);
    }

    return true;  // success, enough storage available
}

void FragmentListRenderer::setShaderUniforms(Shader& shader) {
    setUniforms(shader, textureUnits_[0]);
}

void FragmentListRenderer::setUniforms(Shader& shader, TextureUnit& abuffUnit) const {
    // screen size textures

    abuffUnit.activate();

    abufferIdxTex_.bind();
    glBindImageTexture(abuffUnit.getUnitNumber(), abufferIdxTex_.getID(), 0, false, 0,
                       GL_READ_WRITE, GL_R32UI);

    shader.setUniform("abufferIdxImg", abuffUnit.getUnitNumber());
    glActiveTexture(GL_TEXTURE0);

    // pixel storage
    atomicCounter_.bindBase(6);
    pixelBuffer_.bindBase(7);
    LGL_ERROR;

    // other uniforms
    shader.setUniform("AbufferParams.screenWidth", static_cast<GLint>(screenSize_.x));
    shader.setUniform("AbufferParams.screenHeight", static_cast<GLint>(screenSize_.y));
    shader.setUniform("AbufferParams.storageSize", static_cast<GLuint>(fragmentSize_));
}

bool FragmentListRenderer::supportsFragmentLists() {
    return OpenGLCapabilities::getOpenGLVersion() >= 430;
}

bool FragmentListRenderer::supportsIllustration() {
    if (OpenGLCapabilities::getOpenGLVersion() >= 460)
        return true;
    else if (OpenGLCapabilities::getOpenGLVersion() >= 450)
        return OpenGLCapabilities::isExtensionSupported("GL_ARB_shader_atomic_counter_ops");
    else
        return false;
}

void FragmentListRenderer::buildShaders() {
    auto* dfs = display_.getFragmentShaderObject();

    dfs->addShaderDefine("COLOR_LAYER");

    dfs->clearShaderExtensions();
    dfs->addShaderExtension("GL_NV_gpu_shader5", true);
    dfs->addShaderExtension("GL_EXT_shader_image_load_store", true);
    dfs->addShaderExtension("GL_NV_shader_buffer_load", true);
    dfs->addShaderExtension("GL_NV_shader_buffer_store", true);
    dfs->addShaderExtension("GL_EXT_bindable_uniform", true);

    auto* cfs = clear_.getFragmentShaderObject();
    cfs->clearShaderExtensions();
    cfs->addShaderExtension("GL_NV_gpu_shader5", true);
    cfs->addShaderExtension("GL_EXT_shader_image_load_store", true);
    cfs->addShaderExtension("GL_NV_shader_buffer_load", true);
    cfs->addShaderExtension("GL_NV_shader_buffer_store", true);
    cfs->addShaderExtension("GL_EXT_bindable_uniform", true);

    auto* ffs = illustration_.fill.getFragmentShaderObject();
    ffs->addShaderExtension("GL_ARB_shader_atomic_counter_ops", true);

    display_.build();
    clear_.build();
    illustration_.fill.build();
    illustration_.draw.build();
    illustration_.resolveNeighbors.build();
    illustration_.smooth.build();
}

void FragmentListRenderer::resizeBuffers(const size2_t& screenSize) {
    if (screenSize != screenSize_) {
        screenSize_ = screenSize;
        // reallocate screen size texture that holds the pointer to the end of the fragment list at
        // that pixel
        abufferIdxTex_.resize(screenSize_);
    }

    const auto bufferSize = static_cast<GLsizeiptr>(fragmentSize_ * 4 * sizeof(GLfloat));
    if (pixelBuffer_.getSizeInBytes() != bufferSize) {
        // create new SSBO for the pixel storage
        pixelBuffer_.setSizeInBytes(bufferSize);
        pixelBuffer_.unbind();

        LogInfo("fragment-list: pixel storage for "
                << fragmentSize_
                << " pixels allocated, memory usage: " << (bufferSize / 1024 / 1024.0f) << " MB");
    }
}

void FragmentListRenderer::Illustration::resizeBuffers(size2_t screenSize, size_t fragmentSize) {
    // reallocate textures with head and count
    if (index.getDimensions() != screenSize) {
        // reallocate screen size texture that holds the pointer to the begin of the block of
        // fragments
        index.resize(screenSize);
    }
    if (count.getDimensions() != screenSize) {
        // reallocate screen size texture that holds the count of fragments at that pixel
        count.resize(screenSize);
        count.bind();

        LogInfo("Illustration Buffers: additional screen size buffers allocated of size "
                << screenSize);
    }

    const auto bufferSize = static_cast<GLsizeiptr>(fragmentSize * 2 * sizeof(GLfloat));
    if (color.getSizeInBytes() != bufferSize) {
        // reallocate SSBO for the illustration buffer storage
        // color: alpha+rgb
        color.setSizeInBytes(bufferSize);
        color.unbind();

        // surface info: depth, gradient, compressed normal (not yet)
        surfaceInfo.setSizeInBytes(bufferSize);
        surfaceInfo.unbind();

        // smoothing: beta + gamma
        for (int i = 0; i < 2; ++i) {
            smoothing[i].setSizeInBytes(bufferSize);
            smoothing[i].unbind();
        }
        // reuse fragment lists as neighborhood storage

        LogInfo("Illustration Buffers: additional pixel storage for "
                << fragmentSize << " pixels allocated, memory usage: "
                << (bufferSize * 4 / 1024 / 1024.0f) << " MB");
    }
}

void FragmentListRenderer::fillIllustration(TextureUnit& abuffUnit, TextureUnit& idxUnit,
                                            TextureUnit& countUnit) {
    // reset counter
    LGL_ERROR;
    GLuint v[1] = {0};
    atomicCounter_.upload(v, sizeof(GLuint));
    atomicCounter_.unbind();
    LGL_ERROR;

    // execute sort+fill shader
    illustration_.fill.activate();
    setUniforms(illustration_.fill, abuffUnit);
    illustration_.setUniforms(illustration_.fill, idxUnit, countUnit);

    illustration_.color.bindBase(0);       // out: alpha + color
    illustration_.surfaceInfo.bindBase(1); // out: depth + gradient
    atomicCounter_.bindBase(6);

    utilgl::GlBoolState depthTest(GL_DEPTH_TEST, false);
    utilgl::DepthMaskState depthMask(GL_FALSE);
    utilgl::CullFaceState culling(GL_NONE);
    utilgl::singleDrawImagePlaneRect();

    illustration_.fill.deactivate();
}

void FragmentListRenderer::Illustration::processIllustration(BufferObject& pixelBuffer,
                                                             TextureUnit& idxUnit,
                                                             TextureUnit& countUnit) {
    // resolve neighbors
    // and set initial conditions for silhouettes+halos
    resolveNeighbors.activate();
    setUniforms(resolveNeighbors, idxUnit, countUnit);
    surfaceInfo.bindBase(0);                     // in:  depth + gradient
    pixelBuffer.bindBase(1);                     // out: neighbors
    smoothing[1 - activeSmoothing].bindBase(2);  // out: beta + gamma
    activeSmoothing = 1 - activeSmoothing;

    utilgl::GlBoolState depthTest(GL_DEPTH_TEST, false);
    utilgl::DepthMaskState depthMask(GL_FALSE);
    utilgl::CullFaceState culling(GL_NONE);
    utilgl::singleDrawImagePlaneRect();

    resolveNeighbors.deactivate();

    // perform the bluring
    if (settings.smoothingSteps_ > 0) {
        smooth.activate();
        smooth.setUniform("lambdaBeta", 1.0f - settings.edgeSmoothing_);
        smooth.setUniform("lambdaGamma", 1.0f - settings.haloSmoothing_);
        for (int i = 0; i < settings.smoothingSteps_; ++i) {
            setUniforms(smooth, idxUnit, countUnit);
            pixelBuffer.bindBase(0);                     // in: neighbors
            smoothing[activeSmoothing].bindBase(1);      // in: beta + gamma
            smoothing[1 - activeSmoothing].bindBase(2);  // out: beta + gamma
            activeSmoothing = 1 - activeSmoothing;

            utilgl::singleDrawImagePlaneRect();
        }
        smooth.deactivate();
    }
}

void FragmentListRenderer::Illustration::drawIllustration(TextureUnit& idxUnit,
                                                          TextureUnit& countUnit) {
    // final blending
    draw.activate();
    setUniforms(draw, idxUnit, countUnit);
    surfaceInfo.bindBase(0);                 // in: depth + gradient
    color.bindBase(1);                       // in: alpha + color
    smoothing[activeSmoothing].bindBase(2);  // in: beta + gamma
    vec4 edgeColor = vec4(settings.edgeColor_, settings.edgeStrength_);
    draw.setUniform("edgeColor", edgeColor);
    draw.setUniform("haloStrength", settings.haloStrength_);

    utilgl::BlendModeState blendModeStateGL(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    utilgl::DepthMaskState depthMask(GL_TRUE);
    utilgl::DepthFuncState depthFunc(GL_ALWAYS);
    utilgl::CullFaceState culling(GL_NONE);
    utilgl::singleDrawImagePlaneRect();

    draw.deactivate();
}

void FragmentListRenderer::Illustration::setUniforms(Shader& shader, TextureUnit& idxUnit,
                                                     TextureUnit& countUnit) {
    idxUnit.activate();
    index.bind();
    glBindImageTexture(idxUnit.getUnitNumber(), index.getID(), 0, false, 0, GL_READ_WRITE,
                       GL_R32UI);
    shader.setUniform("illustrationBufferIdxImg", idxUnit.getUnitNumber());
    glActiveTexture(GL_TEXTURE0);

    countUnit.activate();
    count.bind();
    glBindImageTexture(countUnit.getUnitNumber(), count.getID(), 0, false, 0, GL_READ_WRITE,
                       GL_R32UI);
    shader.setUniform("illustrationBufferCountImg", countUnit.getUnitNumber());
    glActiveTexture(GL_TEXTURE0);

    shader.setUniform("screenSize", static_cast<ivec2>(index.getDimensions()));
}

void FragmentListRenderer::debugFragmentLists(GLuint numFrags) {
    std::ostringstream oss;
    oss << "========= Fragment List Renderer - DEBUG =========\n\n";

    // read global counter
    GLuint counter = 0xffffffff;
    atomicCounter_.bind();
    LGL_ERROR;
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &counter);
    LGL_ERROR;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    LGL_ERROR;
    oss << "FLR: value of the global counter: " << counter << std::endl;

    oss << "fragment query: " << numFrags << std::endl;
    oss << "global counter: " << counter << std::endl;

    // read index image
    oss << "Index image:" << std::endl;
    std::vector<GLuint> idxImg(screenSize_.x * screenSize_.y);
    abufferIdxTex_.download(&idxImg[0]);
    LGL_ERROR;
    for (size_t y = 0; y < screenSize_.y; ++y) {
        oss << "y = " << y;
        for (size_t x = 0; x < screenSize_.x; ++x) {
            oss << " " << idxImg[x + screenSize_.x * y];
        }
        oss << std::endl;
    }

    // read pixel storage buffer
    oss << std::endl << "Pixel storage: " << std::endl;
    glBindBuffer(GL_ARRAY_BUFFER, pixelBuffer_.getId());
    LGL_ERROR;
    size_t size = std::min((size_t)counter, fragmentSize_);
    std::vector<GLfloat> pixelBuffer(4 * counter);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GLfloat) * 4 * size, &pixelBuffer[0]);
    LGL_ERROR;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;
    for (size_t i = 0; i < size; ++i) {
        GLuint previous = *reinterpret_cast<GLuint*>(&pixelBuffer[4 * i]);
        GLfloat depth = pixelBuffer[4 * i + 1];
        GLfloat alpha = pixelBuffer[4 * i + 2];
        GLuint c = *reinterpret_cast<GLuint*>(&pixelBuffer[4 * i + 3]);
        float r = float((c >> 20) & 0x3ff) / 1023.0f;
        float g = float((c >> 10) & 0x3ff) / 1023.0f;
        float b = float(c & 0x3ff) / 1023.0f;
        oss << fmt::format(
            "%5d: previous=%5d, depth=%6.3f, alpha=%5.3f, r=%5.3f, g=%5.3f, b=%5.3f\n", i,
            (int)previous, (float)depth, (float)alpha, r, g, b);
    }

    oss << std::endl << "\n==================================================" << std::endl;
}

void FragmentListRenderer::debugIllustrationBuffer(GLuint numFrags) {
    printf("========= Fragment List Renderer - DEBUG Illustration Buffers =========\n\n");

    // read images
    std::vector<GLuint> idxImg(screenSize_.x * screenSize_.y);
    illustration_.index.download(&idxImg[0]);
    LGL_ERROR;
    std::vector<GLuint> countImg(screenSize_.x * screenSize_.y);
    illustration_.count.download(&countImg[0]);
    LGL_ERROR;

    // read pixel storage buffer
    size_t size = std::min((size_t)numFrags, fragmentSize_);

    glBindBuffer(GL_ARRAY_BUFFER, illustration_.color.getId());
    LGL_ERROR;
    std::vector<glm::tvec2<GLfloat>> colorBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(GLfloat) * size, &colorBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    glBindBuffer(GL_ARRAY_BUFFER, illustration_.surfaceInfo.getId());
    LGL_ERROR;
    std::vector<glm::tvec2<GLfloat>> surfaceInfoBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(GLfloat) * size, &surfaceInfoBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    glBindBuffer(GL_ARRAY_BUFFER, pixelBuffer_.getId());
    LGL_ERROR;
    std::vector<glm::tvec4<GLint>> neighborBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 4 * sizeof(GLint) * size, &neighborBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    glBindBuffer(GL_ARRAY_BUFFER,
                 illustration_.smoothing[1 - illustration_.activeSmoothing].getId());
    LGL_ERROR;
    std::vector<glm::tvec2<GLfloat>> smoothingBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(GLfloat) * size, &smoothingBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    // print
    for (size_t y = 0; y < screenSize_.y; ++y) {
        for (size_t x = 0; x < screenSize_.x; ++x) {
            auto start = idxImg[x + screenSize_.x * y];
            auto count = countImg[x + screenSize_.x * y];
            printf(" %4d:%4d:  start=%5d, count=%5d\n", (int)x, (int)y, start, count);
            for (uint32_t i = 0; i < count; ++i) {
                float alpha = colorBuffer[start + i].x;
                int rgb = *reinterpret_cast<int*>(&colorBuffer[start + i].y);
                float depth = surfaceInfoBuffer[start + i].x;
                glm::tvec4<GLint> neighbors = neighborBuffer[start + i];
                float beta = smoothingBuffer[start + i].x;
                float gamma = smoothingBuffer[start + i].y;
                float r = float((rgb >> 20) & 0x3ff) / 1023.0f;
                float g = float((rgb >> 10) & 0x3ff) / 1023.0f;
                float b = float(rgb & 0x3ff) / 1023.0f;
                printf(
                    "     depth=%5.3f, alpha=%5.3f, r=%5.3f, g=%5.3f, b=%5.3f, beta=%5.3f, "
                    "gamma=%5.3f, neighbors:",
                    depth, alpha, r, g, b, beta, gamma);
                if (neighbors.x >= 0) {
                    if (neighbors.x < size) {
                        printf("(%d:%5.3f)", neighbors.x, surfaceInfoBuffer[neighbors.x].x);
                    } else {

                        printf("(>size)");
                    }
                } else {
                    printf("(-1)");
                }
                if (neighbors.y >= 0) {
                    if (neighbors.y < size) {
                        printf("(%d:%5.3f)", neighbors.y, surfaceInfoBuffer[neighbors.y].x);
                    } else {
                        printf("(>size)");
                    }
                } else
                    printf("(-1)");
                if (neighbors.z >= 0) {
                    if (neighbors.z < size)
                        printf("(%d:%5.3f)", neighbors.z, surfaceInfoBuffer[neighbors.z].x);
                    else
                        printf("(>size)");
                } else
                    printf("(-1)");
                if (neighbors.w >= 0) {
                    if (neighbors.w < size)
                        printf("(%d:%5.3f)", neighbors.w, surfaceInfoBuffer[neighbors.w].x);
                    else
                        printf("(>size)");
                } else
                    printf("(-1)");
                printf("\n");
            }
        }
    }

    printf("\n==================================================\n");
}

}  // namespace inviwo
