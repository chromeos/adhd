/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_mix.h"

#include <stdint.h>

#include "cras/src/server/cras_mix_ops.h"
#include "cras/src/server/cras_system_state.h"

static const struct cras_mix_ops* ops = &mixer_ops;

static const struct cras_mix_ops* get_mixer_ops(unsigned int cpu_flags) {
#if HAVE_FMA
  // Exclude APUs that crash when FMA is enabled: (b/184852038)
  if ((cpu_flags & CPU_X86_FMA) && !(cpu_flags & CPU_X86_FMA_CRASH)) {
    return &mixer_ops_fma;
  }
#endif
#if HAVE_AVX2
  if (cpu_flags & CPU_X86_AVX2) {
    return &mixer_ops_avx2;
  }
#endif
#if HAVE_AVX
  if (cpu_flags & CPU_X86_AVX) {
    return &mixer_ops_avx;
  }
#endif
#if HAVE_SSE42
  if (cpu_flags & CPU_X86_SSE4_2) {
    return &mixer_ops_sse42;
  }
#endif

  // default C implementation
  return &mixer_ops;
}
#if defined(__amd64__)
// CPU detection - probaby best to move this elsewhere
static void cpuid(unsigned int* eax,
                  unsigned int* ebx,
                  unsigned int* ecx,
                  unsigned int* edx,
                  unsigned int op) {
  // clang-format off
	__asm__ __volatile__ (
		"cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "a" (op), "c" (0)
	);
  // clang-format on
}

static unsigned int cpu_x86_flags(void) {
  unsigned int eax, ebx, ecx, edx, id;
  unsigned int cpu_flags = 0;

  cpuid(&id, &ebx, &ecx, &edx, 0);

  if (id >= 1) {
    cpuid(&eax, &ebx, &ecx, &edx, 1);

    if (ecx & (1 << 20)) {
      cpu_flags |= CPU_X86_SSE4_2;
    }

    if (ecx & (1 << 28)) {
      cpu_flags |= CPU_X86_AVX;
    }

    if (ecx & (1 << 12)) {
      cpu_flags |= CPU_X86_FMA;
    }

    unsigned int ext_fam = (eax >> 20) & 0xff;
    unsigned int ext_model = (eax >> 16) & 0xf;
    unsigned int base_fam = (eax >> 8) & 0xf;
    unsigned int base_model = (eax >> 4) & 0xf;
    // Trinity CPUs experience an FMA crash
    if (ext_fam == 0x6 && ext_model == 0x1 && base_fam == 0xf &&
        base_model == 0x0) {
      cpu_flags |= CPU_X86_FMA_CRASH;
    }
    // Richland CPUs experience an FMA crash
    if (ext_fam == 0x6 && ext_model == 0x1 && base_fam == 0xf &&
        base_model == 0x3) {
      cpu_flags |= CPU_X86_FMA_CRASH;
    }
    // Vishera CPUs experience an FMA crash
    if (ext_fam == 0x6 && ext_model == 0x0 && base_fam == 0xf &&
        base_model == 0x2) {
      cpu_flags |= CPU_X86_FMA_CRASH;
    }
    // Kaveri CPUs experience an FMA crash
    if (ext_fam == 0x6 && ext_model == 0x3 && base_fam == 0xf &&
        base_model == 0x0) {
      cpu_flags |= CPU_X86_FMA_CRASH;
    }
    // Godavari CPUs experience an FMA crash
    if (ext_fam == 0x6 && ext_model == 0x3 && base_fam == 0xf &&
        base_model == 0x8) {
      cpu_flags |= CPU_X86_FMA_CRASH;
    }
    // Carrizo CPUs experience an FMA crash
    if (ext_fam == 0x6 && ext_model == 0x6 && base_fam == 0xf &&
        base_model == 0x0) {
      cpu_flags |= CPU_X86_FMA_CRASH;
    }
    // Bristol Ridge CPUs experience an FMA crash
    if (ext_fam == 0x6 && ext_model == 0x6 && base_fam == 0xf &&
        base_model == 0x5) {
      cpu_flags |= CPU_X86_FMA_CRASH;
    }
  }

  if (id >= 7) {
    cpuid(&eax, &ebx, &ecx, &edx, 7);

    if (ebx & (1 << 5)) {
      cpu_flags |= CPU_X86_AVX2;
    }
  }

  return cpu_flags;
}
#endif

int cpu_get_flags() {
#if defined(__amd64__)
  return cpu_x86_flags();
#endif
  return 0;
}

void cras_mix_init() {
  ops = get_mixer_ops(cpu_get_flags());
}

/*
 * Exported Interface
 */

void cras_scale_buffer_increment(snd_pcm_format_t fmt,
                                 uint8_t* buff,
                                 unsigned int frame,
                                 float scaler,
                                 float increment,
                                 float target,
                                 int channel) {
  ops->scale_buffer_increment(fmt, buff, frame * channel, scaler, increment,
                              target, channel);
}

void cras_scale_buffer(snd_pcm_format_t fmt,
                       uint8_t* buff,
                       unsigned int count,
                       float scaler) {
  ops->scale_buffer(fmt, buff, count, scaler);
}

void cras_mix_add(snd_pcm_format_t fmt,
                  uint8_t* dst,
                  uint8_t* src,
                  unsigned int count,
                  unsigned int index,
                  int mute,
                  float mix_vol) {
  ops->add(fmt, dst, src, count, index, mute, mix_vol);
}

void cras_mix_add_scale_stride(snd_pcm_format_t fmt,
                               uint8_t* dst,
                               uint8_t* src,
                               unsigned int count,
                               unsigned int dst_stride,
                               unsigned int src_stride,
                               float scaler) {
  ops->add_scale_stride(fmt, dst, src, count, dst_stride, src_stride, scaler);
}

size_t cras_mix_mute_buffer(uint8_t* dst, size_t frame_bytes, size_t count) {
  return ops->mute_buffer(dst, frame_bytes, count);
}
