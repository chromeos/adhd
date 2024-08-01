// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_NC_H_
#define CRAS_SRC_SERVER_CRAS_NC_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Which NC module should provide noise cancellation support?
enum CRAS_NC_PROVIDER {
  CRAS_NC_PROVIDER_NONE = 0,      // NC is disabled for this ionode.
  CRAS_NC_PROVIDER_DSP = 1 << 0,  // NC is supported by DSP.
  CRAS_NC_PROVIDER_AP = 1 << 1,   // NC is supported by AP.
  CRAS_NC_PROVIDER_AST = 1 << 2,  // NC is supported by AST.
};

static inline enum CRAS_NC_PROVIDER cras_nc_resolve_provider(
    uint32_t nc_providers,
    bool dsp_nc_allowed,
    bool ap_nc_allowed,
    bool ast_allowed) {
  if (ast_allowed && (nc_providers & CRAS_NC_PROVIDER_AST)) {
    return CRAS_NC_PROVIDER_AST;
  }
  if (dsp_nc_allowed && (nc_providers & CRAS_NC_PROVIDER_DSP)) {
    return CRAS_NC_PROVIDER_DSP;
  }
  if (ap_nc_allowed && (nc_providers & CRAS_NC_PROVIDER_AP)) {
    return CRAS_NC_PROVIDER_AP;
  }
  return CRAS_NC_PROVIDER_NONE;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_NC_H_
