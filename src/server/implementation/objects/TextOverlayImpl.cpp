#include <gst/gst.h>
#include "MediaPipelineImpl.hpp"
#include <TextOverlayImplFactory.hpp>
#include "TextOverlayImpl.hpp"
#include <jsonrpc/JsonSerializer.hpp>
#include <KurentoException.hpp>

#define GST_CAT_DEFAULT kurento_textoverlay_impl
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "KurentoTextOverlayImpl"

#define FACTORY_NAME "kmstextoverlay"

namespace kurento
{
TextOverlayImpl::TextOverlayImpl (const boost::property_tree::ptree &config,
                                  std::shared_ptr<MediaPipeline> mediaPipeline)  : MediaElementImpl (config,
                                        std::dynamic_pointer_cast<MediaObjectImpl> (mediaPipeline), FACTORY_NAME)
{
}


MediaObjectImpl *
TextOverlayImplFactory::createObject (const boost::property_tree::ptree &config,
                                      std::shared_ptr<MediaPipeline> mediaPipeline) const
{
  return new TextOverlayImpl (config, mediaPipeline);
}

void
TextOverlayImpl::setText (const std::string &text)
{
  g_object_set ( G_OBJECT (this), "style", text.c_str(), NULL);
}

TextOverlayImpl::StaticConstructor TextOverlayImpl::staticConstructor;

TextOverlayImpl::StaticConstructor::StaticConstructor()
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
                           GST_DEFAULT_NAME);
}

} /* kurento */
