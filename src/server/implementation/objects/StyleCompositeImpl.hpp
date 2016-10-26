/* Autogenerated with kurento-module-creator */

#ifndef __STYLE_COMPOSITE_IMPL_HPP__
#define __STYLE_COMPOSITE_IMPL_HPP__

#include "HubImpl.hpp"
#include "StyleComposite.hpp"
#include <EventHandler.hpp>
#include <boost/property_tree/ptree.hpp>

namespace kurento
{
class StyleCompositeImpl;
} /* kurento */

namespace kurento
{
void Serialize (std::shared_ptr<kurento::StyleCompositeImpl> &object,
                JsonSerializer &serializer);
} /* kurento */

namespace kurento
{
class MediaPipelineImpl;
} /* kurento */

namespace kurento
{

class StyleCompositeImpl : public HubImpl, public virtual StyleComposite
{

public:

  StyleCompositeImpl (const boost::property_tree::ptree &config,
                      std::shared_ptr<MediaPipeline> mediaPipeline);

  virtual ~StyleCompositeImpl () {};

  void setStyle (const std::string &style);
  std::string getStyle ();
  void showView (int viewId);
  void hideView (int viewId);

  /* Next methods are automatically implemented by code generator */
  virtual bool connect (const std::string &eventType,
                        std::shared_ptr<EventHandler> handler);
  virtual void invoke (std::shared_ptr<MediaObjectImpl> obj,
                       const std::string &methodName, const Json::Value &params,
                       Json::Value &response);

  virtual void Serialize (JsonSerializer &serializer);

private:

  bool setViewEnableStatus (int viewId, char enable);
  class StaticConstructor
  {
  public:
    StaticConstructor();
  };

  static StaticConstructor staticConstructor;

};

} /* kurento */

#endif /*  __STYLE_COMPOSITE_IMPL_HPP__ */
