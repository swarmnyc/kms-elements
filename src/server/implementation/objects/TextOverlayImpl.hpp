#ifndef __TEXTOVERLAY_IMPL_HPP__
#define __TEXTOVERLAY_IMPL_HPP__

#include "MediaElementImpl.hpp"
#include "TextOverlay.hpp"
#include <EventHandler.hpp>
#include <boost/property_tree/ptree.hpp>

namespace kurento
{
class PassThroughImpl;
} /* kurento */

namespace kurento
{
void Serialize (std::shared_ptr<kurento::PassThroughImpl> &object,
                JsonSerializer &serializer);
} /* kurento */

namespace kurento
{
class MediaPipelineImpl;
} /* kurento */

namespace kurento
{
class TextOverlayImpl : public MediaElementImpl, public virtual TextOverlay
{

public:

  TextOverlayImpl (const boost::property_tree::ptree &config,
                   std::shared_ptr<MediaPipeline> mediaPipeline);

  virtual ~TextOverlayImpl () {};

  /* Next methods are automatically implemented by code generator */
  virtual bool connect (const std::string &eventType,
                        std::shared_ptr<EventHandler> handler);

  virtual void invoke (std::shared_ptr<MediaObjectImpl> obj,
                       const std::string &methodName, const Json::Value &params,
                       Json::Value &response);

  virtual void Serialize (JsonSerializer &serializer);

  void setText (const std::string &text);

private:

  class StaticConstructor
  {
  public:
    StaticConstructor();
  };

  static StaticConstructor staticConstructor;

};

} /* kurento */

#endif /*  __TEXTOVERLAY_IMPL_HPP__ */
