#ifndef __STYLE_COMPOSITE_IMPL_HPP__
#define __STYLE_COMPOSITE_IMPL_HPP__

#include "HubImpl.hpp"
#include "Composite.hpp"
#include "StyleComposite.hpp"
#include <EventHandler.hpp>

namespace kurento
{

class MediaPipeline;
class StyleCompositeImpl;

void Serialize (std::shared_ptr<StyleCompositeImpl> &object,
                JsonSerializer &serializer);

class StyleCompositeImpl : public HubImpl, public virtual Composite
{

public:

  StyleCompositeImpl (const boost::property_tree::ptree &conf,
                      std::shared_ptr<MediaPipeline> mediaPipeline);

  virtual ~StyleCompositeImpl () {};

  std::string getStyle ();
  void setStyle (const std::string &style);

  /* Next methods are automatically implemented by code generator */
  virtual bool connect (const std::string &eventType,
                        std::shared_ptr<EventHandler> handler);

  virtual void invoke (std::shared_ptr<MediaObjectImpl> obj,
                       const std::string &methodName, const Json::Value &params,
                       Json::Value &response);

  virtual void Serialize (JsonSerializer &serializer);

private:

  class StaticConstructor
  {
  public:
    StaticConstructor();
  };

  static StaticConstructor staticConstructor;

};

} /* kurento */

#endif /*  __STYLE_COMPOSITE_IMPL_HPP__ */
