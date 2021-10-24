/*
 * Copyright (c) 2021 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup libui
 * @{
 */
/**
 * @file Window
 */

#include <congfx/console.h>
#include <display.h>
#include <errno.h>
#include <gfx/bitmap.h>
#include <gfx/context.h>
#include <gfx/cursor.h>
#include <gfx/render.h>
#include <io/kbd_event.h>
#include <io/pos_event.h>
#include <mem.h>
#include <memgfx/memgc.h>
#include <stdlib.h>
#include <ui/control.h>
#include <ui/resource.h>
#include <ui/ui.h>
#include <ui/wdecor.h>
#include <ui/window.h>
#include "../private/control.h"
#include "../private/dummygc.h"
#include "../private/resource.h"
#include "../private/ui.h"
#include "../private/wdecor.h"
#include "../private/window.h"

static void dwnd_close_event(void *);
static void dwnd_focus_event(void *);
static void dwnd_kbd_event(void *, kbd_event_t *);
static void dwnd_pos_event(void *, pos_event_t *);
static void dwnd_resize_event(void *, gfx_rect_t *);
static void dwnd_unfocus_event(void *);

static display_wnd_cb_t dwnd_cb = {
	.close_event = dwnd_close_event,
	.focus_event = dwnd_focus_event,
	.kbd_event = dwnd_kbd_event,
	.pos_event = dwnd_pos_event,
	.resize_event = dwnd_resize_event,
	.unfocus_event = dwnd_unfocus_event
};

static void wd_close(ui_wdecor_t *, void *);
static void wd_move(ui_wdecor_t *, void *, gfx_coord2_t *);
static void wd_resize(ui_wdecor_t *, void *, ui_wdecor_rsztype_t,
    gfx_coord2_t *);
static void wd_set_cursor(ui_wdecor_t *, void *, ui_stock_cursor_t);

static ui_wdecor_cb_t wdecor_cb = {
	.close = wd_close,
	.move = wd_move,
	.resize = wd_resize,
	.set_cursor = wd_set_cursor
};

static void ui_window_invalidate(void *, gfx_rect_t *);
static void ui_window_update(void *);
static errno_t ui_window_cursor_get_pos(void *, gfx_coord2_t *);
static errno_t ui_window_cursor_set_pos(void *, gfx_coord2_t *);
static errno_t ui_window_cursor_set_visible(void *, bool);

/** Window memory GC callbacks */
static mem_gc_cb_t ui_window_mem_gc_cb = {
	.invalidate = ui_window_invalidate,
	.update = ui_window_update,
	.cursor_get_pos = ui_window_cursor_get_pos,
	.cursor_set_pos = ui_window_cursor_set_pos,
	.cursor_set_visible = ui_window_cursor_set_visible
};

static void ui_window_app_invalidate(void *, gfx_rect_t *);
static void ui_window_app_update(void *);

/** Application area memory GC callbacks */
static mem_gc_cb_t ui_window_app_mem_gc_cb = {
	.invalidate = ui_window_app_invalidate,
	.update = ui_window_app_update
};

static void ui_window_expose_cb(void *);

/** Initialize window parameters structure.
 *
 * Window parameters structure must always be initialized using this function
 * first. By default, the window will be decorated. To get a non-decorated
 * window, one needs to clear ui_wds_decorated
 * (e.g. params->style &= ~ui_wds_decorated).
 *
 * @param params Window parameters structure
 */
void ui_wnd_params_init(ui_wnd_params_t *params)
{
	memset(params, 0, sizeof(ui_wnd_params_t));

	/* Make window decorated by default. */
	params->style = ui_wds_decorated;
}

/** Compute where window should be placed on the screen.
 *
 * This only applies to windows that do not use default placement or
 * if we are running in full-screen mode.
 *
 * @param window Window
 * @param drect Display rectangle
 * @param params Window parameters
 * @param pos Place to store position of top-left corner
 */
static void ui_window_place(ui_window_t *window, gfx_rect_t *drect,
    ui_wnd_params_t *params, gfx_coord2_t *pos)
{
	gfx_coord2_t dims;

	assert(params->placement != ui_wnd_place_default ||
	    ui_is_fullscreen(window->ui));

	pos->x = 0;
	pos->y = 0;

	switch (params->placement) {
	case ui_wnd_place_default:
		assert(ui_is_fullscreen(window->ui));
		/* Center window */
		gfx_rect_dims(&params->rect, &dims);
		pos->x = (drect->p0.x + drect->p1.x) / 2 - dims.x / 2;
		pos->y = (drect->p0.y + drect->p1.y) / 2 - dims.y / 2;
		break;
	case ui_wnd_place_top_left:
	case ui_wnd_place_full_screen:
		pos->x = drect->p0.x - params->rect.p0.x;
		pos->y = drect->p0.y - params->rect.p0.y;
		break;
	case ui_wnd_place_top_right:
		pos->x = drect->p1.x - params->rect.p1.x;
		pos->y = drect->p0.y - params->rect.p0.y;
		break;
	case ui_wnd_place_bottom_left:
		pos->x = drect->p0.x - params->rect.p0.x;
		pos->y = drect->p1.y - params->rect.p1.y;
		break;
	case ui_wnd_place_bottom_right:
		pos->x = drect->p1.x - params->rect.p1.x;
		pos->y = drect->p1.y - params->rect.p1.y;
		break;
	case ui_wnd_place_popup:
		/* Place popup window below parent rectangle */
		pos->x = params->prect.p0.x;
		pos->y = params->prect.p1.y;
		break;
	}
}

/** Create new window.
 *
 * @param ui User interface
 * @param params Window parameters
 * @param rwindow Place to store pointer to new window
 * @return EOK on success or an error code
 */
errno_t ui_window_create(ui_t *ui, ui_wnd_params_t *params,
    ui_window_t **rwindow)
{
	ui_window_t *window;
	display_info_t info;
	gfx_coord2_t scr_dims;
	display_wnd_params_t dparams;
	gfx_context_t *gc = NULL;
	ui_resource_t *res = NULL;
	ui_wdecor_t *wdecor = NULL;
	dummy_gc_t *dgc = NULL;
	gfx_bitmap_params_t bparams;
	gfx_bitmap_alloc_t alloc;
	gfx_bitmap_t *bmp = NULL;
	gfx_coord2_t off;
	mem_gc_t *memgc = NULL;
	xlate_gc_t *xgc = NULL;
	errno_t rc;

	window = calloc(1, sizeof(ui_window_t));
	if (window == NULL)
		return ENOMEM;

	window->ui = ui;

	display_wnd_params_init(&dparams);
	dparams.rect = params->rect;
	/* Only allow making the window larger */
	gfx_rect_dims(&params->rect, &dparams.min_size);

	if ((params->flags & ui_wndf_popup) != 0)
		dparams.flags |= wndf_popup;

	if (ui->display != NULL) {
		if (params->placement != ui_wnd_place_default) {
			rc = display_get_info(ui->display, &info);
			if (rc != EOK)
				goto error;
		}

		if (params->placement == ui_wnd_place_full_screen) {
			/* Make window the size of the screen */
			gfx_rect_dims(&info.rect, &scr_dims);
			gfx_coord2_add(&dparams.rect.p0, &scr_dims,
			    &dparams.rect.p1);
		}

		if (params->placement != ui_wnd_place_default) {
			/* Set initial display window position */
			ui_window_place(window, &info.rect, params,
			    &dparams.pos);

			dparams.flags |= wndf_setpos;
		}

		rc = display_window_create(ui->display, &dparams, &dwnd_cb,
		    (void *) window, &window->dwindow);
		if (rc != EOK)
			goto error;

		rc = display_window_get_gc(window->dwindow, &gc);
		if (rc != EOK)
			goto error;
	} else if (ui->console != NULL) {
		gc = console_gc_get_ctx(ui->cgc);

		if (params->placement == ui_wnd_place_full_screen) {
			/* Make window the size of the screen */
			gfx_rect_dims(&ui->rect, &scr_dims);
			gfx_coord2_add(&dparams.rect.p0, &scr_dims,
			    &dparams.rect.p1);
		}
	} else {
		/* Needed for unit tests */
		rc = dummygc_create(&dgc);
		if (rc != EOK)
			goto error;

		gc = dummygc_get_ctx(dgc);
	}

#ifdef CONFIG_UI_CS_RENDER
	/* Create window bitmap */
	gfx_bitmap_params_init(&bparams);
#ifndef CONFIG_WIN_DOUBLE_BUF
	/* Console does not support direct output */
	if (ui->display != NULL)
		bparams.flags |= bmpf_direct_output;
#endif

	/* Move rectangle so that top-left corner is 0,0 */
	gfx_rect_rtranslate(&dparams.rect.p0, &dparams.rect, &bparams.rect);

	rc = gfx_bitmap_create(gc, &bparams, NULL, &bmp);
	if (rc != EOK)
		goto error;

	/* Create memory GC */
	rc = gfx_bitmap_get_alloc(bmp, &alloc);
	if (rc != EOK) {
		gfx_bitmap_destroy(window->app_bmp);
		return rc;
	}

	rc = mem_gc_create(&bparams.rect, &alloc, &ui_window_mem_gc_cb,
	    (void *) window, &memgc);
	if (rc != EOK) {
		gfx_bitmap_destroy(window->app_bmp);
		return rc;
	}

	window->bmp = bmp;
	window->mgc = memgc;
	window->gc = mem_gc_get_ctx(memgc);
	window->realgc = gc;
	(void) off;
#else
	/* Server-side rendering */

	/* Full-screen mode? */
	if (ui->display == NULL) {
		/* Create translating GC to translate window contents */
		off.x = 0;
		off.y = 0;
		rc = xlate_gc_create(&off, gc, &xgc);
		if (rc != EOK)
			goto error;

		window->xgc = xgc;
		window->gc = xlate_gc_get_ctx(xgc);
		window->realgc = gc;
	} else {
		window->gc = gc;
	}

	(void) ui_window_mem_gc_cb;
	(void) alloc;
	(void) bparams;
#endif
	if (ui->display == NULL) {
		ui_window_place(window, &ui->rect, params, &window->dpos);

		if (window->xgc != NULL)
			xlate_gc_set_off(window->xgc, &window->dpos);
	}

	rc = ui_resource_create(window->gc, ui_is_textmode(ui), &res);
	if (rc != EOK)
		goto error;

	rc = ui_wdecor_create(res, params->caption, params->style, &wdecor);
	if (rc != EOK)
		goto error;

	ui_wdecor_set_rect(wdecor, &dparams.rect);
	ui_wdecor_set_cb(wdecor, &wdecor_cb, (void *) window);
	ui_wdecor_paint(wdecor);

	ui_resource_set_expose_cb(res, ui_window_expose_cb, (void *) window);

	window->rect = dparams.rect;
	window->res = res;
	window->wdecor = wdecor;
	window->cursor = ui_curs_arrow;
	*rwindow = window;

	list_append(&window->lwindows, &ui->windows);
	return EOK;
error:
	if (wdecor != NULL)
		ui_wdecor_destroy(wdecor);
	if (res != NULL)
		ui_resource_destroy(res);
	if (memgc != NULL)
		mem_gc_delete(memgc);
	if (xgc != NULL)
		xlate_gc_delete(xgc);
	if (bmp != NULL)
		gfx_bitmap_destroy(bmp);
	if (dgc != NULL)
		dummygc_destroy(dgc);
	free(window);
	return rc;
}

/** Destroy window.
 *
 * @param window Window or @c NULL
 */
void ui_window_destroy(ui_window_t *window)
{
	ui_t *ui;

	if (window == NULL)
		return;

	ui = window->ui;

	list_remove(&window->lwindows);
	ui_control_destroy(window->control);
	ui_wdecor_destroy(window->wdecor);
	ui_resource_destroy(window->res);
	if (0 && window->app_mgc != NULL)
		mem_gc_delete(window->app_mgc);
	if (0 && window->app_bmp != NULL)
		gfx_bitmap_destroy(window->app_bmp);
	if (window->mgc != NULL) {
		mem_gc_delete(window->mgc);
		window->gc = NULL;
	}
	if (window->bmp != NULL)
		gfx_bitmap_destroy(window->bmp);
	if (window->dwindow != NULL)
		display_window_destroy(window->dwindow);

	free(window);

	/* Need to repaint if windows are emulated */
	if (ui_is_fullscreen(ui)) {
		ui_paint(ui);
	}
}

/** Add control to window.
 *
 * Only one control can be added to a window. If more than one control
 * is added, the results are undefined.
 *
 * @param window Window
 * @param control Control
 * @return EOK on success, ENOMEM if out of memory
 */
void ui_window_add(ui_window_t *window, ui_control_t *control)
{
	assert(window->control == NULL);

	window->control = control;
	control->elemp = (void *) window;
}

/** Remove control from window.
 *
 * @param window Window
 * @param control Control
 */
void ui_window_remove(ui_window_t *window, ui_control_t *control)
{
	assert(window->control == control);
	assert((ui_window_t *) control->elemp == window);

	window->control = NULL;
	control->elemp = NULL;
}

/** Get active window (only valid in fullscreen mode).
 *
 * @param ui User interface
 * @return Active window
 */
ui_window_t *ui_window_get_active(ui_t *ui)
{
	link_t *link;

	link = list_last(&ui->windows);
	if (link == NULL)
		return NULL;

	return list_get_instance(link, ui_window_t, lwindows);
}

/** Resize/move window.
 *
 * Resize window to the dimensions of @a rect. If @a rect.p0 is not 0,0,
 * the top-left corner of the window will move on the screen accordingly.
 *
 * @param window Window
 * @param rect Rectangle
 *
 * @return EOK on success or an error code
 */
errno_t ui_window_resize(ui_window_t *window, gfx_rect_t *rect)
{
	gfx_coord2_t offs;
	gfx_rect_t nrect;
	gfx_rect_t arect;
	gfx_bitmap_t *app_bmp = NULL;
	gfx_bitmap_t *win_bmp = NULL;
	gfx_bitmap_params_t app_params;
	gfx_bitmap_params_t win_params;
	gfx_bitmap_alloc_t app_alloc;
	gfx_bitmap_alloc_t win_alloc;
	errno_t rc;

	/*
	 * Move rect so that p0=0,0 - keep window's coordinate system origin
	 * locked to top-left corner of the window.
	 */
	offs = rect->p0;
	gfx_rect_rtranslate(&offs, rect, &nrect);

	/* mgc != NULL iff client-side rendering */
	if (window->mgc != NULL) {
#ifdef CONFIG_WIN_DOUBLE_BUF
		/*
		 * Create new window bitmap in advance. If direct mapping,
		 * will need do it after resizing the window.
		 */
		assert(window->bmp != NULL);
		gfx_bitmap_params_init(&win_params);
		win_params.rect = nrect;

		rc = gfx_bitmap_create(window->realgc, &win_params, NULL,
		    &win_bmp);
		if (rc != EOK)
			goto error;

		rc = gfx_bitmap_get_alloc(win_bmp, &win_alloc);
		if (rc != EOK)
			goto error;
#endif
	}

	/* Application area GC? */
	if (window->app_gc != NULL) {
		/* Resize application bitmap */
		assert(window->app_bmp != NULL);

		gfx_bitmap_params_init(&app_params);

		/*
		 * The bitmap will have the same dimensions as the
		 * application rectangle, but start at 0,0.
		 */
		ui_wdecor_app_from_rect(window->wdecor->style, &nrect, &arect);
		gfx_rect_rtranslate(&arect.p0, &arect, &app_params.rect);

		rc = gfx_bitmap_create(window->gc, &app_params, NULL,
		    &app_bmp);
		if (rc != EOK)
			goto error;

		rc = gfx_bitmap_get_alloc(app_bmp, &app_alloc);
		if (rc != EOK)
			goto error;
	}

	/* dwindow can be NULL in case of unit tests */
	if (window->dwindow != NULL) {
		rc = display_window_resize(window->dwindow, &offs, &nrect);
		if (rc != EOK)
			goto error;
	}

	/* Client side rendering? */
	if (window->mgc != NULL) {
#ifndef CONFIG_WIN_DOUBLE_BUF
		/* Window is resized, now we can map the window bitmap again */
		gfx_bitmap_params_init(&win_params);
		win_params.flags |= bmpf_direct_output;
		win_params.rect = nrect;

		rc = gfx_bitmap_create(window->realgc, &win_params, NULL,
		    &win_bmp);
		if (rc != EOK)
			goto error;

		rc = gfx_bitmap_get_alloc(win_bmp, &win_alloc);
		if (rc != EOK)
			goto error;
#endif

		mem_gc_retarget(window->mgc, &win_params.rect, &win_alloc);

		gfx_bitmap_destroy(window->bmp);
		window->bmp = win_bmp;
	}

	ui_wdecor_set_rect(window->wdecor, &nrect);
	ui_wdecor_paint(window->wdecor);
	gfx_update(window->gc);

	/* Application area GC? */
	if (window->app_gc != NULL) {
		mem_gc_retarget(window->app_mgc, &app_params.rect, &app_alloc);

		gfx_bitmap_destroy(window->app_bmp);
		window->app_bmp = app_bmp;
	}

	return EOK;
error:
	if (app_bmp != NULL)
		gfx_bitmap_destroy(app_bmp);
	if (win_bmp != NULL)
		gfx_bitmap_destroy(win_bmp);
	return rc;
}

/** Set window callbacks.
 *
 * @param window Window
 * @param cb Window callbacks
 * @param arg Callback argument
 */
void ui_window_set_cb(ui_window_t *window, ui_window_cb_t *cb, void *arg)
{
	window->cb = cb;
	window->arg = arg;
}

/** Get window's containing UI.
 *
 * @param window Window
 * @return Containing UI
 */
ui_t *ui_window_get_ui(ui_window_t *window)
{
	return window->ui;
}

/** Get UI resource from window.
 *
 * @param window Window
 * @return UI resource
 */
ui_resource_t *ui_window_get_res(ui_window_t *window)
{
	return window->res;
}

/** Get window GC.
 *
 * @param window Window
 * @return GC (relative to window)
 */
gfx_context_t *ui_window_get_gc(ui_window_t *window)
{
	return window->gc;
}

/** Get window position.
 *
 * @param window Window
 * @param pos Place to store position
 * @return EOK on success or an error code
 */
errno_t ui_window_get_pos(ui_window_t *window, gfx_coord2_t *pos)
{
	errno_t rc;

	if (window->dwindow != NULL) {
		rc = display_window_get_pos(window->dwindow, pos);
		if (rc != EOK)
			return rc;
	} else {
		*pos = window->dpos;
	}

	return EOK;
}

/** Get window application area GC
 *
 * @param window Window
 * @param rgc Place to store GC (relative to application area)
 * @return EOK on success or an error code
 */
errno_t ui_window_get_app_gc(ui_window_t *window, gfx_context_t **rgc)
{
	gfx_bitmap_params_t params;
	gfx_bitmap_alloc_t alloc;
	gfx_rect_t rect;
	mem_gc_t *memgc;
	errno_t rc;

	if (window->app_gc == NULL) {
		assert(window->app_bmp == NULL);

		gfx_bitmap_params_init(&params);

		/*
		 * The bitmap will have the same dimensions as the
		 * application rectangle, but start at 0,0.
		 */
		ui_window_get_app_rect(window, &rect);
		gfx_rect_rtranslate(&rect.p0, &rect, &params.rect);

		rc = gfx_bitmap_create(window->gc, &params, NULL,
		    &window->app_bmp);
		if (rc != EOK)
			return rc;

		rc = gfx_bitmap_get_alloc(window->app_bmp, &alloc);
		if (rc != EOK) {
			gfx_bitmap_destroy(window->app_bmp);
			return rc;
		}

		rc = mem_gc_create(&params.rect, &alloc,
		    &ui_window_app_mem_gc_cb, (void *) window, &memgc);
		if (rc != EOK) {
			gfx_bitmap_destroy(window->app_bmp);
			return rc;
		}

		window->app_mgc = memgc;
		window->app_gc = mem_gc_get_ctx(memgc);
	}

	*rgc = window->app_gc;
	return EOK;
}

/** Get window application rectangle
 *
 * @param window Window
 * @param rect Place to store application rectangle
 */
void ui_window_get_app_rect(ui_window_t *window, gfx_rect_t *rect)
{
	ui_wdecor_geom_t geom;

	ui_wdecor_get_geom(window->wdecor, &geom);
	*rect = geom.app_area_rect;
}

/** Set cursor when pointer is hovering over a control.
 *
 * @param window Window
 * @param cursor Cursor
 */
void ui_window_set_ctl_cursor(ui_window_t *window, ui_stock_cursor_t cursor)
{
	display_stock_cursor_t dcursor;

	dcursor = wnd_dcursor_from_cursor(cursor);

	if (window->dwindow != NULL)
		(void) display_window_set_cursor(window->dwindow, dcursor);
}

/** Paint window
 *
 * @param window Window
 * @return EOK on success or an error code
 */
errno_t ui_window_paint(ui_window_t *window)
{
	return ui_window_send_paint(window);
}

/** Handle window close event. */
static void dwnd_close_event(void *arg)
{
	ui_window_t *window = (ui_window_t *) arg;

	ui_window_send_close(window);
}

/** Handle window focus event. */
static void dwnd_focus_event(void *arg)
{
	ui_window_t *window = (ui_window_t *) arg;

	if (window->wdecor != NULL) {
		ui_wdecor_set_active(window->wdecor, true);
		ui_wdecor_paint(window->wdecor);
	}

	ui_window_send_focus(window);
}

/** Handle window keyboard event */
static void dwnd_kbd_event(void *arg, kbd_event_t *kbd_event)
{
	ui_window_t *window = (ui_window_t *) arg;

	(void) window;
	ui_window_send_kbd(window, kbd_event);
}

/** Handle window position event */
static void dwnd_pos_event(void *arg, pos_event_t *event)
{
	ui_window_t *window = (ui_window_t *) arg;

	/* Make sure we don't process events until fully initialized */
	if (window->wdecor == NULL)
		return;

	ui_wdecor_pos_event(window->wdecor, event);
	ui_window_send_pos(window, event);
}

/** Handle window resize event */
static void dwnd_resize_event(void *arg, gfx_rect_t *rect)
{
	ui_window_t *window = (ui_window_t *) arg;

	/* Make sure we don't process events until fully initialized */
	if (window->wdecor == NULL)
		return;

	if ((window->wdecor->style & ui_wds_resizable) == 0)
		return;

	(void) ui_window_resize(window, rect);
	(void) ui_window_paint(window);
}

/** Handle window unfocus event. */
static void dwnd_unfocus_event(void *arg)
{
	ui_window_t *window = (ui_window_t *) arg;

	if (window->wdecor != NULL) {
		ui_wdecor_set_active(window->wdecor, false);
		ui_wdecor_paint(window->wdecor);
	}

	ui_window_send_unfocus(window);
}

/** Window decoration requested window closure.
 *
 * @param wdecor Window decoration
 * @param arg Argument (window)
 */
static void wd_close(ui_wdecor_t *wdecor, void *arg)
{
	ui_window_t *window = (ui_window_t *) arg;

	ui_window_send_close(window);
}

/** Window decoration requested window move.
 *
 * @param wdecor Window decoration
 * @param arg Argument (window)
 * @param pos Position where the title bar was pressed
 */
static void wd_move(ui_wdecor_t *wdecor, void *arg, gfx_coord2_t *pos)
{
	ui_window_t *window = (ui_window_t *) arg;

	if (window->dwindow != NULL)
		(void) display_window_move_req(window->dwindow, pos);
}

/** Window decoration requested window resize.
 *
 * @param wdecor Window decoration
 * @param arg Argument (window)
 * @param rsztype Resize type
 * @param pos Position where the button was pressed
 */
static void wd_resize(ui_wdecor_t *wdecor, void *arg,
    ui_wdecor_rsztype_t rsztype, gfx_coord2_t *pos)
{
	ui_window_t *window = (ui_window_t *) arg;

	if (window->dwindow != NULL)
		(void) display_window_resize_req(window->dwindow, rsztype, pos);
}

/** Get display stock cursor from UI stock cursor.
 *
 * @param cursor UI stock cursor
 * @return Display stock cursor
 */
display_stock_cursor_t wnd_dcursor_from_cursor(ui_stock_cursor_t cursor)
{
	display_stock_cursor_t dcursor;

	dcursor = dcurs_arrow;

	switch (cursor) {
	case ui_curs_arrow:
		dcursor = dcurs_arrow;
		break;
	case ui_curs_size_ud:
		dcursor = dcurs_size_ud;
		break;
	case ui_curs_size_lr:
		dcursor = dcurs_size_lr;
		break;
	case ui_curs_size_uldr:
		dcursor = dcurs_size_uldr;
		break;
	case ui_curs_size_urdl:
		dcursor = dcurs_size_urdl;
		break;
	case ui_curs_ibeam:
		dcursor = dcurs_ibeam;
		break;
	}

	return dcursor;
}

/** Window decoration requested changing cursor.
 *
 * @param wdecor Window decoration
 * @param arg Argument (window)
 * @param cursor Cursor to set
 */
static void wd_set_cursor(ui_wdecor_t *wdecor, void *arg,
    ui_stock_cursor_t cursor)
{
	ui_window_t *window = (ui_window_t *) arg;
	display_stock_cursor_t dcursor;

	if (cursor == window->cursor)
		return;

	dcursor = wnd_dcursor_from_cursor(cursor);

	if (window->dwindow != NULL)
		(void) display_window_set_cursor(window->dwindow, dcursor);

	window->cursor = cursor;
}

/** Send window close event.
 *
 * @param window Window
 */
void ui_window_send_close(ui_window_t *window)
{
	if (window->cb != NULL && window->cb->close != NULL)
		window->cb->close(window, window->arg);
}

/** Send window focus event.
 *
 * @param window Window
 */
void ui_window_send_focus(ui_window_t *window)
{
	if (window->cb != NULL && window->cb->focus != NULL)
		window->cb->focus(window, window->arg);
}

/** Send window keyboard event.
 *
 * @param window Window
 */
void ui_window_send_kbd(ui_window_t *window, kbd_event_t *kbd)
{
	if (window->cb != NULL && window->cb->kbd != NULL)
		window->cb->kbd(window, window->arg, kbd);
	else
		return ui_window_def_kbd(window, kbd);
}

/** Send window paint event.
 *
 * @param window Window
 */
errno_t ui_window_send_paint(ui_window_t *window)
{
	if (window->cb != NULL && window->cb->paint != NULL)
		return window->cb->paint(window, window->arg);
	else
		return ui_window_def_paint(window);
}

/** Send window position event.
 *
 * @param window Window
 */
void ui_window_send_pos(ui_window_t *window, pos_event_t *pos)
{
	if (window->cb != NULL && window->cb->pos != NULL)
		window->cb->pos(window, window->arg, pos);
	else
		ui_window_def_pos(window, pos);
}

/** Send window unfocus event.
 *
 * @param window Window
 */
void ui_window_send_unfocus(ui_window_t *window)
{
	if (window->cb != NULL && window->cb->unfocus != NULL)
		window->cb->unfocus(window, window->arg);
	else
		return ui_window_def_unfocus(window);
}

/** Default window keyboard event routine.
 *
 * @param window Window
 */
void ui_window_def_kbd(ui_window_t *window, kbd_event_t *kbd)
{
	if (window->control != NULL)
		ui_control_kbd_event(window->control, kbd);
}

/** Default window paint routine.
 *
 * @param window Window
 * @return EOK on success or an error code
 */
errno_t ui_window_def_paint(ui_window_t *window)
{
	gfx_rect_t app_rect;
	errno_t rc;

	rc = gfx_set_color(window->gc, window->res->wnd_face_color);
	if (rc != EOK)
		return rc;

	ui_window_get_app_rect(window, &app_rect);

	rc = gfx_fill_rect(window->gc, &app_rect);
	if (rc != EOK)
		return rc;

	if (window->control != NULL)
		return ui_control_paint(window->control);

	rc = gfx_update(window->res->gc);
	if (rc != EOK)
		return rc;

	return EOK;
}

/** Default window position event routine.
 *
 * @param window Window
 */
void ui_window_def_pos(ui_window_t *window, pos_event_t *pos)
{
	if (window->control != NULL)
		ui_control_pos_event(window->control, pos);
}

/** Default window unfocus routine.
 *
 * @param window Window
 * @return EOK on success or an error code
 */
void ui_window_def_unfocus(ui_window_t *window)
{
	if (window->control != NULL)
		ui_control_unfocus(window->control);
}

/** Window invalidate callback
 *
 * @param arg Argument (ui_window_t *)
 * @param rect Rectangle to update
 */
static void ui_window_invalidate(void *arg, gfx_rect_t *rect)
{
	ui_window_t *window = (ui_window_t *) arg;
	gfx_rect_t env;

	gfx_rect_envelope(&window->dirty_rect, rect, &env);
	window->dirty_rect = env;
}

/** Window update callback
 *
 * @param arg Argument (ui_window_t *)
 */
static void ui_window_update(void *arg)
{
	ui_window_t *window = (ui_window_t *) arg;

	if (!gfx_rect_is_empty(&window->dirty_rect)) {
		(void) gfx_bitmap_render(window->bmp, &window->dirty_rect,
		    &window->dpos);
	}

	window->dirty_rect.p0.x = 0;
	window->dirty_rect.p0.y = 0;
	window->dirty_rect.p1.x = 0;
	window->dirty_rect.p1.y = 0;
}

/** Window cursor get position callback
 *
 * @param arg Argument (ui_window_t *)
 * @param pos Place to store position
 */
static errno_t ui_window_cursor_get_pos(void *arg, gfx_coord2_t *pos)
{
	ui_window_t *window = (ui_window_t *) arg;
	gfx_coord2_t cpos;
	errno_t rc;

	rc = gfx_cursor_get_pos(window->realgc, &cpos);
	if (rc != EOK)
		return rc;

	pos->x = cpos.x - window->dpos.x;
	pos->y = cpos.y - window->dpos.y;
	return EOK;
}

/** Window cursor set position callback
 *
 * @param arg Argument (ui_window_t *)
 * @param pos New position
 */
static errno_t ui_window_cursor_set_pos(void *arg, gfx_coord2_t *pos)
{
	ui_window_t *window = (ui_window_t *) arg;
	gfx_coord2_t cpos;

	cpos.x = pos->x + window->dpos.x;
	cpos.y = pos->y + window->dpos.y;

	return gfx_cursor_set_pos(window->realgc, &cpos);
}

/** Window cursor set visibility callback
 *
 * @param arg Argument (ui_window_t *)
 * @param visible @c true iff cursor is to be made visible
 */
static errno_t ui_window_cursor_set_visible(void *arg, bool visible)
{
	ui_window_t *window = (ui_window_t *) arg;

	return gfx_cursor_set_visible(window->realgc, visible);
}

/** Application area invalidate callback
 *
 * @param arg Argument (ui_window_t *)
 * @param rect Rectangle to update
 */
static void ui_window_app_invalidate(void *arg, gfx_rect_t *rect)
{
	ui_window_t *window = (ui_window_t *) arg;
	gfx_rect_t arect;

	ui_window_get_app_rect(window, &arect);

	/* Render bitmap rectangle inside the application area */
	(void) gfx_bitmap_render(window->app_bmp, rect, &arect.p0);
	/*
	 * TODO Update applications to call gfx_update(), then
	 * we can defer update to ui_window_app_update().
	 */
	(void) gfx_update(window->res->gc);
}

/** Application area update callback
 *
 * @param arg Argument (ui_window_t *)
 */
static void ui_window_app_update(void *arg)
{
	ui_window_t *window = (ui_window_t *) arg;

	/*
	 * Not used since display is updated immediately
	 * in ui_window_app_invalidate
	 */
	(void) window;
}

/** Window expose callback. */
static void ui_window_expose_cb(void *arg)
{
	ui_window_t *window = (ui_window_t *) arg;

	ui_window_paint(window);
}

/** @}
 */
