/* 2026-08-25T05:01:02.148830 */

/*
* Copyright (c) 2026 Nordic Semiconductor ASA
* SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
*/

#ifndef _NRF_EDGEAI_USER_MODEL_LABELS_H_
#define _NRF_EDGEAI_USER_MODEL_LABELS_H_

#include <nrf_edgeai/nrf_edgeai_ctypes.h>

#ifdef   __cplusplus
extern "C"
{
#endif

typedef enum nrf_edgeai_user_label_e {
    MODEL_LABEL_INDEX_OTHER,
    MODEL_LABEL_INDEX_SILENCE,
    MODEL_LABEL_INDEX_NO,
    MODEL_LABEL_INDEX_OK,
    MODEL_LABEL_INDEX_OPUS,
    MODEL_LABEL_INDEX_STOP,
    MODEL_LABEL_INDEX_YES,
} nrf_edgeai_user_label_t;

static const char* NRF_EDGEAI_USER_LABELS_NAME[] = {
    "OTHER", "SILENCE", "no", "ok", "opus", "stop", "yes"
};

#ifdef   __cplusplus
}
#endif

#endif /* _NRF_EDGEAI_USER_MODEL_LABELS_H_ */
