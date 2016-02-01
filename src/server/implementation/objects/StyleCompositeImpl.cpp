/* Autogenerated with kurento-module-creator */

#include <gst/gst.h>
#include "MediaPipeline.hpp"
#include <StyleCompositeImplFactory.hpp>
#include "StyleCompositeImpl.hpp"
#include <jsonrpc/JsonSerializer.hpp>
#include <KurentoException.hpp>

#define GST_CAT_DEFAULT kurento_style_composite_impl
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "KurentoStyleCompositeImpl"

#define FACTORY_NAME "stylecompositemixer"

namespace kurento
{

StyleCompositeImpl::StyleCompositeImpl (const boost::property_tree::ptree
                                        &config, std::shared_ptr<MediaPipeline> mediaPipeline)  : HubImpl (config,
                                              std::dynamic_pointer_cast<MediaObjectImpl> (mediaPipeline), FACTORY_NAME)
{}

void StyleCompositeImpl::setStyle (const std::string &style)
{
  g_object_set ( G_OBJECT (element), "style", style.c_str(), NULL);
}

std::string StyleCompositeImpl::getStyle ()
{
  std::string style;
  gchar *ret;

  g_object_get ( G_OBJECT (element), "style", &ret, NULL);

  if (ret != NULL) {
    style = std::string (ret);
    g_free (ret);
  }

  return style;
}

void StyleCompositeImpl::showView (int viewId)
{

}
void StyleCompositeImpl::hideView (int viewId)
{

}

MediaObjectImpl *
StyleCompositeImplFactory::createObject (const boost::property_tree::ptree
    &config, std::shared_ptr<MediaPipeline> mediaPipeline) const
{
  return new StyleCompositeImpl (config, mediaPipeline);
}

StyleCompositeImpl::StaticConstructor StyleCompositeImpl::staticConstructor;

StyleCompositeImpl::StaticConstructor::StaticConstructor()
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
                           GST_DEFAULT_NAME);
}

} /* kurento */
