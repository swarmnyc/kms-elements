#include <gst/gst.h>
#include "MediaPipeline.hpp"
#include <CompositeImplFactory.hpp>
#include "CompositeImpl.hpp"
#include <jsonrpc/JsonSerializer.hpp>
#include <KurentoException.hpp>
#include <gst/gst.h>

#define GST_CAT_DEFAULT kurento_composite_impl
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "KurentoCompositeImpl"

#define FACTORY_NAME "compositemixer"

namespace kurento
{

CompositeImpl::CompositeImpl (const boost::property_tree::ptree &conf,
                              std::shared_ptr<MediaPipeline> mediaPipeline) : HubImpl (conf,
                                    std::dynamic_pointer_cast<MediaObjectImpl> (mediaPipeline), FACTORY_NAME)
{
}

std::string
CompositeImpl::getBackgroundImage ()
{
  std::string backgroundImage;
  gchar *ret;

  g_object_get ( G_OBJECT (element), "background-image", &ret, NULL);

  if (ret != NULL) {
    backgroundImage = std::string (ret);
    g_free (ret);
  }

  return backgroundImage;
}

void
CompositeImpl::setBackgroundImage (const std::string &uri)
{
  GST_ERROR ("@rentao setBackgroundImage=%s", uri.c_str() );
  g_object_set ( G_OBJECT (element), "background-image",
                 uri.c_str(),
                 NULL);
}

MediaObjectImpl *
CompositeImplFactory::createObject (const boost::property_tree::ptree &conf,
                                    std::shared_ptr<MediaPipeline> mediaPipeline) const
{
  return new CompositeImpl (conf, mediaPipeline);
}

CompositeImpl::StaticConstructor CompositeImpl::staticConstructor;

CompositeImpl::StaticConstructor::StaticConstructor()
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
                           GST_DEFAULT_NAME);
}

} /* kurento */
