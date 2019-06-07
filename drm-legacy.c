/*
 * Copyright (c) 2017 Rob Clark <rclark@redhat.com>
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

#include "common.h"
#include "drm-common.h"
#include <tbm_surface_internal.h>

static struct drm drm;

static void page_flip_handler(int fd, unsigned int frame,
		  unsigned int sec, unsigned int usec, void *data)
{
	/* suppress 'unused parameter' warnings */
	(void)fd, (void)frame, (void)sec, (void)usec;

	int *waiting_for_flip = data;
	*waiting_for_flip = 0;
}

#ifndef TIZEN
static int legacy_run(const struct gbm *gbm, const struct egl *egl)
#else
static int legacy_run(const es_context_t *gbm, const struct egl *egl)
#endif
{
	fd_set fds;
	drmEventContext evctx = {
			.version = 2,
			.page_flip_handler = page_flip_handler,
	};
#ifndef TIZEN
	struct gbm_bo *bo;
	struct drm_fb *fb;
#else
	tbm_surface_h tbm_surf = NULL;
	tbm_surface_queue_h tbm_queue = NULL;
	tbm_surface_queue_error_e tbm_err = TBM_SURFACE_QUEUE_ERROR_NONE;
	unsigned int width;
	unsigned int height;
	unsigned int format;
	unsigned int handles[4] = {0,};
	unsigned int pitches[4] = {0,};
	unsigned int offsets[4] = {0,};
	unsigned int size;
	int count;
	tbm_bo bo;

	uint32_t fb_id, fb_id2;
#endif
	uint32_t i = 0;
	int ret;

	eglSwapBuffers(egl->display, egl->surface);
#ifndef TIZEN
	bo = gbm_surface_lock_front_buffer(gbm->surface);
	fb = drm_fb_get_from_bo(bo);
	if (!fb) {
		fprintf(stderr, "Failed to get a new framebuffer BO\n");
		return -1;
	}
#else
	tbm_queue = (tbm_surface_queue_h)gbm->hWnd;

	/* wait till egl driver (within separate thread) enqueues surface with drawn image */
	tbm_surface_queue_can_acquire(tbm_queue, 1);

	tbm_err = tbm_surface_queue_acquire(tbm_queue, &tbm_surf);
	if (tbm_err != TBM_SURFACE_QUEUE_ERROR_NONE || !tbm_surf) {
		printf("Failed to acquire tbm_surf(err:%d)\n", tbm_err);
		return -1;
	}

	width = tbm_surface_get_width(tbm_surf);
	height = tbm_surface_get_height(tbm_surf);
	format = tbm_surface_get_format(tbm_surf);
	count = tbm_surface_internal_get_num_planes(format);
	bo = tbm_surface_internal_get_bo(tbm_surf, 0);
	tbm_bo_handle handle;
	tbm_error_e error;
	handle = tbm_bo_get_handle(bo, TBM_DEVICE_DEFAULT);
	if (handle.ptr == NULL) {
		error = tbm_get_last_error();
		printf("unable to get handle %x", error);
		return -1;
	}

	handles[0] = handle.u32;
	for (i = 1; i < count; i++)
		handles[i] = handles[0];

	for (i = 0; i < count; i++)
		tbm_surface_internal_get_plane_data(tbm_surf, i, &size, &offsets[i], &pitches[i]);

	ret = drmModeAddFB2(tbm_drm_helper_get_fd(), width, height, format,
							handles, pitches, offsets, &fb_id, 0);
	if (ret) {
		printf("failed to add framebuffer: ret %d, errno %d fd = %d %s\n", ret, errno, drm.fd, strerror(errno));
		return ret;
	}
#endif

	/* set mode: */
	ret = drmModeSetCrtc(drm.fd, drm.crtc_id, fb_id, 0, 0,
			&drm.connector_id, 1, drm.mode);
	if (ret) {
		printf("failed to set mode: %s\n", strerror(errno));
		return ret;
	}

	while (1) {
#ifndef TIZEN
		struct gbm_bo *next_bo;
#else
		tbm_surface_h tbm_surf_next;
#endif
		int waiting_for_flip = 1;

		egl->draw(i++);

		eglSwapBuffers(egl->display, egl->surface);
#ifndef TIZEN
		next_bo = gbm_surface_lock_front_buffer(gbm->surface);
		fb = drm_fb_get_from_bo(next_bo);
		if (!fb) {
			fprintf(stderr, "Failed to get a new framebuffer BO\n");
			return -1;
		}
#else
		tbm_surface_queue_can_acquire(tbm_queue, 1);

		tbm_err = tbm_surface_queue_acquire(tbm_queue, &tbm_surf_next);
		if (tbm_err != TBM_SURFACE_QUEUE_ERROR_NONE || !tbm_surf_next) {
			printf("Failed to acquire tbm_surf_next(err:%d)\n", tbm_err);
			return;
		}

		width = tbm_surface_get_width(tbm_surf_next);
		height = tbm_surface_get_height(tbm_surf_next);
		format = tbm_surface_get_format(tbm_surf_next);
		count = tbm_surface_internal_get_num_planes(format);
		bo = tbm_surface_internal_get_bo(tbm_surf_next, 0);
		handles[0] = tbm_bo_get_handle(bo, TBM_DEVICE_DEFAULT).u32;
		for (i = 1; i < count; i++)
			handles[i] = handles[0];

		for (i = 0; i < count; i++)
			tbm_surface_internal_get_plane_data(tbm_surf_next, i, &size, &offsets[i], &pitches[i]);

		ret = drmModeAddFB2(tbm_drm_helper_get_fd(), width, height, format,
							handles, pitches, offsets, &fb_id2, 0);
#endif

		/*
		 * Here you could also update drm plane layers if you want
		 * hw composition
		 */

		ret = drmModePageFlip(tbm_drm_helper_get_master_fd(), drm.crtc_id, fb_id2,
				DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
		if (ret) {
			printf("failed to queue page flip: ret %d errno %d %s\n", ret, errno, strerror(errno));
			return -1;
		}

		while (waiting_for_flip) {
			FD_ZERO(&fds);
			FD_SET(0, &fds);
			FD_SET(drm.fd, &fds);

			ret = select(drm.fd + 1, &fds, NULL, NULL, NULL);
			if (ret < 0) {
				printf("select err: %s\n", strerror(errno));
				return ret;
			} else if (ret == 0) {
				printf("select timeout!\n");
				return -1;
			} else if (FD_ISSET(0, &fds)) {
				printf("user interrupted!\n");
				return 0;
			}
			drmHandleEvent(drm.fd, &evctx);
		}
#ifndef TIZEN
		/* release last buffer to render on again: */
		gbm_surface_release_buffer(gbm->surface, bo);
		bo = next_bo;
#else
		tbm_surface_queue_release(tbm_queue, tbm_surf);
		tbm_surf = tbm_surf_next;
#endif
	}

	return 0;
}

const struct drm * init_drm_legacy(const char *device)
{
	int ret;

	ret = init_drm(&drm, device);
	if (ret)
		return NULL;

	drm.run = legacy_run;

	return &drm;
}
