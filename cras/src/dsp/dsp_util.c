/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fpu_control.h>
#include "dsp_util.h"

void dsp_enable_flush_denormal_to_zero()
{
#if defined(__i386__) || defined(__x86_64__)
	unsigned int mxcsr;
	mxcsr = __builtin_ia32_stmxcsr();
	__builtin_ia32_ldmxcsr(mxcsr | 0x8040);
#elif defined(__arm__)
	int cw;
	_FPU_GETCW(cw);
	_FPU_SETCW(cw | (1 << 24));
#else
#warning "Don't know how to disable denorms. Performace may suffer."
#endif
}
