 /*********************************************************************************
 *
 * Inviwo - Interactive Visualization Workshop
 * Version 0.6b
 *
 * Copyright (c) 2013-2014 Inviwo Foundation
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
 * Main file authors: Daniel J�nsson, Erik Sund�n
 *
 *********************************************************************************/

#include <modules/opencl/image/layercl.h>
#include <modules/opencl/image/layerclresizer.h>
#include <inviwo/core/util/assertion.h>
namespace inviwo {

LayerCL::LayerCL(uvec2 dimensions, LayerType type, const DataFormatBase* format, const void* data)
    : LayerCLBase(), LayerRepresentation(dimensions, type, format), layerFormat_(dataFormatToCLImageFormat(format->getId()))
{
    initialize(data);
}

LayerCL::LayerCL( const LayerCL& rhs )
    : LayerCLBase(), LayerRepresentation(rhs)
    , layerFormat_(dataFormatToCLImageFormat(rhs.getDataFormat()->getId())) {

    initialize(NULL);
    OpenCL::instance()->getQueue().enqueueCopyImage(rhs.get(), *clImage_ , glm::svec3(0), glm::svec3(0), glm::svec3(dimensions_, 1));
}

LayerCL::~LayerCL() { 
    deinitialize(); 
}

void LayerCL::initialize(const void* texels) {

    if (texels != NULL) {
        // Could performance be increased by using pinned memory?
        // 3.1.1 http://www.nvidia.com/content/cudazone/CUDABrowser/downloads/papers/NVIDIA_OpenCL_BestPracticesGuide.pdf
        //cl::Buffer pinnedMem(OpenCL::instance()->getContext(), CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, sizeof(texels), NULL, NULL);
        //unsigned char* mappedMem = (unsigned char*)OpenCL::instance()->getQueue().enqueueMapBuffer(pinnedMem, true, CL_MAP_WRITE, 0, sizeof(texels), 0);
        //memcpy(mappedMem, texels, sizeof(texels));
        //OpenCL::instance()->getQueue().enqueueWriteLayer(*layer2D_, true, glm::svec3(0), glm::svec3(dimensions_, 1), 0, 0, mappedMem);
        //OpenCL::instance()->getQueue().enqueueUnmapMemObject(pinnedMem, mappedMem);

        // This should also use pinned memory...
        clImage_ = new cl::Image2D(OpenCL::instance()->getContext(), 
            CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR, 
            getFormat(), static_cast<size_t>(dimensions_.x), static_cast<size_t>(dimensions_.y), 0, const_cast<void*>(texels));
        // Alternatively first allocate memory on device and then transfer
        //layer2D_ = new cl::Layer2D(OpenCL::instance()->getContext(), CL_MEM_READ_WRITE, getFormat(), dimensions_.x, dimensions_.y);
        //OpenCL::instance()->getQueue().enqueueWriteLayer(*layer2D_, true, glm::svec3(0), glm::svec3(dimensions_, 1), 0, 0, texels);
    } else {
        clImage_ = new cl::Image2D(OpenCL::instance()->getContext(), CL_MEM_READ_WRITE, getFormat(), static_cast<size_t>(dimensions_.x), static_cast<size_t>(dimensions_.y));
    }
    LayerCL::initialize();
}

LayerCL* LayerCL::clone() const {
    LayerCL* newLayerCL = new LayerCL(dimensions_, getLayerType(), getDataFormat());
    OpenCL::instance()->getQueue().enqueueCopyImage(*clImage_, (newLayerCL->get()), glm::svec3(0), glm::svec3(0), glm::svec3(dimensions_, 1));
    return newLayerCL;
}

void LayerCL::deinitialize() {
	delete clImage_; 
}

void LayerCL::upload( const void* data )
{
    OpenCL::instance()->getQueue().enqueueWriteImage(*clImage_, true, glm::svec3(0), glm::svec3(dimensions_, 1), 0, 0, const_cast<void*>(data));
}

void LayerCL::download( void* data ) const
{
    OpenCL::instance()->getQueue().enqueueReadImage(*clImage_, true, glm::svec3(0), glm::svec3(dimensions_, 1), 0, 0, data);
}

void LayerCL::resize(uvec2 dimensions)
{
    if (dimensions == dimensions_) {
        return;
    }
    cl::Image2D* resizedLayer2D = new cl::Image2D(OpenCL::instance()->getContext(), CL_MEM_READ_WRITE, getFormat(), dimensions.x, dimensions.y);
    LayerCLResizer::resize(*clImage_, *resizedLayer2D, dimensions);
    delete clImage_;
    clImage_ = resizedLayer2D;
    LayerRepresentation::resize(dimensions);

}

bool LayerCL::copyAndResizeLayer(DataRepresentation* target) const{
    LayerCL* targetCL = dynamic_cast<LayerCL*>(target);

    if (!targetCL) return false;

    LayerCLResizer::resize(*clImage_, (targetCL->get()), targetCL->getDimension());
	
	return true;
}

void LayerCL::setDimension( uvec2 dimensions )
{
    delete clImage_;
    clImage_ = new cl::Image2D(OpenCL::instance()->getContext(), CL_MEM_READ_WRITE, getFormat(), dimensions.x, dimensions.y);
}

} // namespace

namespace cl {

template <>
cl_int Kernel::setArg(cl_uint index, const inviwo::LayerCL& value) {
    return setArg(index, value.get());
}

} // namespace cl
