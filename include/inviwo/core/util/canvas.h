#ifndef IVW_CANVAS_H
#define IVW_CANVAS_H

#include <inviwo/core/common/inviwocoredefine.h>
#include <inviwo/core/common/inviwo.h>
#include <inviwo/core/datastructures/image.h>
#include <inviwo/core/network/processornetworkevaluator.h>
#include <inviwo/core/interaction/events/eventhandler.h>
#include <inviwo/core/interaction/events/keyboardevent.h>
#include <inviwo/core/interaction/events/mouseevent.h>
#include <inviwo/core/interaction/events/timerevent.h>

namespace inviwo {

class IVW_CORE_API Canvas {
public:
    Canvas(uvec2 dimensions);
    virtual ~Canvas();

    virtual void initialize();
    virtual void deinitialize();
    virtual void activate();
    virtual void render(const Image*);
    virtual void repaint();
    virtual void resize(uvec2 dimensions);
    virtual uvec2 size() { return dimensions_;}
    virtual void update();

    void setNetworkEvaluator(ProcessorNetworkEvaluator* networkEvaluator) { processorNetworkEvaluator_ = networkEvaluator; };

    static ProcessorNetworkEvaluator* processorNetworkEvaluator_; //TODO: should this rather be private?

protected:
    uvec2 dimensions_;
};

} // namespace

#endif // IVW_CANVAS_H
