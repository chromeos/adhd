/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2024 Intelligo Technology Inc. All rights reserved.
 *
 * Author: Fu-Yun TSUO <fy.tsuo@intelli-go.com>
 */

/*******************************************************************************
 * [2017] - [2024] Copyright (c) Intelligo Technology Inc.
 *
 * This unpublished material is proprietary to Intelligo Technology Inc.
 * All rights reserved. The methods and techniques described herein are
 * considered trade secrets and/or confidential. Reproduction or
 * distribution, in whole or in part, is forbidden except by express written
 * permission of Intelligo Technology Inc.
 *
 *******************************************************************************/

#ifndef _IGO_LIB_H_
#define _IGO_LIB_H_

#include <stdint.h>

enum IgoRet {
  IGO_RET_OK = 0,
  IGO_RET_ERR,
  IGO_RET_NO_SERVICE,
  IGO_RET_INVL_ARG,
  IGO_RET_NO_MEMORY,
  IGO_RET_NOT_SUPPORT,
  IGO_RET_ALGO_NAME_NOT_FOUND,
  IGO_RET_CH_NUM_ERR,
  IGO_RET_SAMPLING_RATE_NOT_SUPPORT,
  IGO_RET_IN_DATA_ERR,
  IGO_RET_REF_DATA_ERR,
  IGO_RET_OUT_DATA_ERR,
  IGO_RET_PARAM_NOT_FOUND,
  IGO_RET_PARAM_READ_ONLY,
  IGO_RET_PARAM_WRITE_ONLY,
  IGO_RET_PARAM_INVALID_VAL,
  IGO_RET_LAST
};

enum IgoDataWidth {
  IGO_DATA_INT16 = 0,
  IGO_DATA_INT32,
  IGO_DATA_FLOAT32,
  IGO_DATA_LAST
};

/**
 * @brief IgoLibInfo is used to keep information for iGo library.
 *
 */
struct IgoLibInfo {
  const char* algo_name;  /* Algorithm name */
  uint32_t source_id;     /* Library source ID */
  uint32_t date_code;     /* BCD format. e.g., 0x20220527 */
  uint32_t major_version; /* Major version */
  uint32_t minor_version; /* Minor version */
  uint32_t build_version; /* Build version */
  uint32_t ext_version;   /* Extension version */
  uint32_t git_commit_id; /* Git commit ID */
  uint8_t max_in_ch_num;  /* Maximal input channel nubmer */
  uint8_t max_ref_ch_num; /* Maximal reference channel nubmer */
  uint8_t max_out_ch_num; /* Maximal output channel nubmer */
};

/**
 * @brief IgoStreamData is used to keep audio data for iGo library.
 *
 */
struct IgoStreamData {
  void* data;                   /* Data array */
  enum IgoDataWidth data_width; /* Specify audio data bit width */
  uint16_t sample_num;          /* Sample number in this data bulk */
  uint16_t sampling_rate;       /* Sampling rate for the data stream */
};

/**
 * @brief IgoLibConfig is used to keep lib configuration for lib instance
 * initialization.
 *
 */
struct IgoLibConfig {
  const void* private_data; /* Point to private data */
  void* public_data;        /* Point to public data */
  uint8_t in_ch_num;        /* Input channel number in use*/
  uint8_t ref_ch_num;       /* Reference channel number in use */
  uint8_t out_ch_num;       /* Output channel number in use*/
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IgoLibGetInfo() - Retrieve the lib information.
 * @param[out]      info     Lib information.
 *
 * This API is used to get library detail information.
 *
 * @return iGo defined return value.
 */
enum IgoRet IgoLibGetInfo(struct IgoLibInfo* info);

/**
 * @brief IgoLibNew() - Allocate an iGo lib instance.
 * @param[in]       config       Lib configuration.
 * @param[in]       in      input audio stream.
 * @param[in]       ref     reference audio stream.
 * @param[out]      out     output audio stream.
 *
 * This API is used to allocate an iGo lib instance
 *
 * @return iGo defined return value.
 */
enum IgoRet IgoLibNew(struct IgoLibConfig* config,
                      struct IgoStreamData* in,
                      struct IgoStreamData* ref,
                      struct IgoStreamData* out);

/**
 * @brief IgoLibSendBufferAddr() - Send address of data buffer to lib
 * @param[in]       config       Lib configuration.
 * @param[in]       in      input audio stream.
 * @param[in]       ref     reference audio stream.
 * @param[out]      out     output audio stream.
 *
 * This API is used to send buffer address to iGo lib instance.
 *
 * @return iGo defined return value.
 */
enum IgoRet IgoLibUpdateStreamData(struct IgoLibConfig* config,
                                   struct IgoStreamData* in,
                                   struct IgoStreamData* ref,
                                   struct IgoStreamData* out);

/**
 * @brief IgoLibDelete() - Delete an iGo lib instance.
 * @param[in]       config       Lib configuration.
 *
 * This API is used to delete an iGo lib instance
 *
 * @return iGo defined return value.
 */
enum IgoRet IgoLibDelete(struct IgoLibConfig* config);

/**
 * @brief IgoLibProcess() - Process audio stream.
 * @param[in]       in      input audio stream.
 * @param[in]       ref     reference audio stream.
 * @param[out]      out     output audio stream.
 *
 * This API is used to process audio stream. The default audio sample is 16bit.
 * The sampling rate and sample number should be specified in IgoStreamData
 * structure. If the channel number > 1 for IgoStreamData, the data should be
 * interleaved sample by sample.
 *
 * Note:    IgoLibProcess supports 16k/48k 16bit data only by default.
 *          If other data format or sampling rate is required, please
 *          ask intelliGo for support.
 *
 * @return iGo defined return value.
 */
enum IgoRet IgoLibProcess(struct IgoLibConfig* config,
                          struct IgoStreamData* in,
                          struct IgoStreamData* ref,
                          struct IgoStreamData* out);

#ifdef __cplusplus
}
#endif

#endif
