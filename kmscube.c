/*
 * Copyright (c) 2012 Arvin Schnell <arvin.schnell@gmail.com>
 * Copyright (c) 2012 Rob Clark <rob@ti.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Based on a egl cube test app originally written by Arvin Schnell */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "common.h"
#include "drm-common.h"

#ifdef HAVE_GST
#include <gst/gst.h>
GST_DEBUG_CATEGORY(kmscube_debug);
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const struct egl *egl;
#ifndef TIZEN
static const struct gbm *gbm;
#else
static es_context_t gbm;
#include <tbm_log.h>
#endif
static const struct drm *drm;

static const char *shortopts = "AD:M:m:V:";

static const struct option longopts[] = {
	{"atomic", no_argument,       0, 'A'},
	{"device", required_argument, 0, 'D'},
	{"mode",   required_argument, 0, 'M'},
	{"modifier", required_argument, 0, 'm'},
	{"samples",  required_argument, 0, 's'},
	{"video",  required_argument, 0, 'V'},
	{0, 0, 0, 0}
};

static void usage(const char *name)
{
	printf("Usage: %s [-ADMmV]\n"
			"\n"
			"options:\n"
			"    -A, --atomic             use atomic modesetting and fencing\n"
			"    -D, --device=DEVICE      use the given device\n"
			"    -M, --mode=MODE          specify mode, one of:\n"
			"        smooth    -  smooth shaded cube (default)\n"
			"        rgba      -  rgba textured cube\n"
			"        nv12-2img -  yuv textured (color conversion in shader)\n"
			"        nv12-1img -  yuv textured (single nv12 texture)\n"
			"    -m, --modifier=MODIFIER  hardcode the selected modifier\n"
			"    -s, --samples=N          use MSAA\n"
			"    -V, --video=FILE         video textured cube\n",
			name);
}

int main(int argc, char *argv[])
{
	const char *device = "/dev/dri/card0";
	const char *video = NULL;
	enum mode mode = SMOOTH;
	uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
	int samples = 0;
	int atomic = 0;
	int opt;

#ifdef HAVE_GST
	gst_init(&argc, &argv);
	GST_DEBUG_CATEGORY_INIT(kmscube_debug, "kmscube", 0, "kmscube video pipeline");
#endif

	while ((opt = getopt_long_only(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch (opt) {
		case 'A':
			atomic = 1;
			break;
		case 'D':
			device = optarg;
			break;
		case 'M':
			if (strcmp(optarg, "smooth") == 0) {
				mode = SMOOTH;
			} else if (strcmp(optarg, "rgba") == 0) {
				mode = RGBA;
			} else if (strcmp(optarg, "nv12-2img") == 0) {
				mode = NV12_2IMG;
			} else if (strcmp(optarg, "nv12-1img") == 0) {
				mode = NV12_1IMG;
			} else {
				printf("invalid mode: %s\n", optarg);
				usage(argv[0]);
				return -1;
			}
			break;
		case 'm':
			modifier = strtoull(optarg, NULL, 0);
			break;
		case 's':
			samples = strtoul(optarg, NULL, 0);
			break;
		case 'V':
			mode = VIDEO;
			video = optarg;
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}
#ifndef TIZEN
	if (atomic)
		drm = init_drm_atomic(device);
	else
		drm = init_drm_legacy(device);
	if (!drm) {
		printf("failed to initialize %s DRM\n", atomic ? "atomic" : "legacy");
		return -1;
	}

	gbm = init_gbm(drm->fd, drm->mode->hdisplay, drm->mode->vdisplay,
			modifier);
	if (!gbm) {
		printf("failed to initialize GBM\n");
		return -1;
	}


	if (mode == SMOOTH)
		egl = init_cube_smooth(gbm, samples);
	else if (mode == VIDEO)
		egl = init_cube_video(gbm, video, samples);
	else
		egl = init_cube_tex(gbm, mode, samples);

	if (!egl) {
		printf("failed to initialize EGL\n");
		return -1;
	}
#else
	drm = init_drm_legacy(device);
        if (!drm) {
                printf("failed to initialize %s DRM\n", atomic ? "atomic" : "legacy");
                return -1;
        }

	tbm_surface_queue_h tbm_surf;

	gbm.bufmgr = tbm_bufmgr_init(drm->fd);
	if (!gbm.bufmgr) {
		printf("tbm_bufmgr_init fail\n");
		return -1;
	}

	tbm_log_set_debug_level(4);

	gbm.fd = drm->fd;

	gbm.width = drm->mode->hdisplay;
	gbm.height = drm->mode->vdisplay;
	gbm.format = TBM_FORMAT_XRGB8888;

	tbm_surf = tbm_surface_queue_create(3, gbm.width, gbm.height,
	                                    gbm.format, TBM_BO_SCANOUT);
	if (!tbm_surf) {
		printf("Failed to create tbm_surface.\n");
		return -1;
	}

	gbm.hWnd = (EGLNativeWindowType)tbm_surf;

	egl = init_cube_smooth(&gbm, samples);
	if (!egl) {
		printf("failed to initialize EGL\n");
		return -1;
	}
#endif

	/* clear the color buffer */
	glClearColor(0.5, 0.5, 0.5, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
#ifndef TIZEN
	return drm->run(gbm, egl);
#else
	return drm->run(&gbm, egl);
#endif
}
