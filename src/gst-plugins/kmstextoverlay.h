/*
 * (C) Copyright 2016 Swarmnyc (http://www.swarmnyc.com/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Created by: Tao Ren (tao@swarmnyc.com)
 *
 */
#ifndef _KMS_TEXT_OVERLAY_H_
#define _KMS_TEXT_OVERLAY_H_

#include <commons/kmselement.h>

G_BEGIN_DECLS
#define KMS_TYPE_TEXT_OVERLAY (kms_text_overlay_get_type())
#define KMS_TEXT_OVERLAY(obj) (                 \
  G_TYPE_CHECK_INSTANCE_CAST (                  \
    (obj),                                      \
    KMS_TYPE_TEXT_OVERLAY,                      \
    KmsTextOverlay                              \
  )                                             \
)
#define KMS_TEXT_OVERLAY_CLASS(klass) (         \
  G_TYPE_CHECK_CLASS_CAST (                     \
    (klass),                                    \
    KMS_TYPE_TEXT_OVERLAY,                      \
    KmsTextOverlayClass                         \
  )                                             \
)
#define KMS_IS_TEXT_OVERLAY(obj) (              \
  G_TYPE_CHECK_INSTANCE_TYPE (                  \
    (obj),                                      \
    KMS_TYPE_TEXT_OVERLAY                       \
    )                                           \
)
#define KMS_IS_TEXT_OVERLAY_CLASS(klass) (      \
  G_TYPE_CHECK_CLASS_TYPE (                     \
  (klass),                                      \
  KMS_TYPE_TEXT_OVERLAY                         \
  )                                             \
)
typedef struct _KmsTextOverlay KmsTextOverlay;
typedef struct _KmsTextOverlayClass KmsTextOverlayClass;
typedef struct _KmsTextOverlayPrivate KmsTextOverlayPrivate;

struct _KmsTextOverlay
{
    KmsElement parent;
    /*< private > */
    KmsTextOverlayPrivate *priv;
};

struct _KmsTextOverlayClass
{
    KmsElementClass parent_class;
};

GType kms_text_overlay_get_type (void);

gboolean kms_text_overlay_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif