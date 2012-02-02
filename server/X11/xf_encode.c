/**
 * FreeRDP: A Remote Desktop Protocol Client
 * X11 RemoteFX Encoder
 *
 * Copyright 2011 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <X11/Xlib.h>
#include <freerdp/utils/sleep.h>

#include "xf_encode.h"

XImage* xf_snapshot(xfPeerContext* xfp, int x, int y, int width, int height)
{
	XImage* image;
	xfInfo* xfi = xfp->info;

	pthread_mutex_lock(&(xfp->mutex));

	if (xfi->use_xshm)
	{
		XCopyArea(xfi->display, xfi->root_window, xfi->fb_pixmap,
				xfi->xdamage_gc, x, y, width, height, x, y);

		XSync(xfi->display, False);

		image = xfi->fb_image;
	}
	else
	{
		image = XGetImage(xfi->display, xfi->root_window,
				x, y, width, height, AllPlanes, ZPixmap);
	}

	pthread_mutex_unlock(&(xfp->mutex));

	return image;
}

void xf_xdamage_subtract_region(xfPeerContext* xfp, int x, int y, int width, int height)
{
	XRectangle region;
	xfInfo* xfi = xfp->info;

	region.x = x;
	region.y = y;
	region.width = width;
	region.height = height;

#ifdef WITH_XFIXES
	pthread_mutex_lock(&(xfp->mutex));
	XFixesSetRegion(xfi->display, xfi->xdamage_region, &region, 1);
	XDamageSubtract(xfi->display, xfi->xdamage, xfi->xdamage_region, None);
	pthread_mutex_unlock(&(xfp->mutex));
#endif
}

void* xf_frame_rate_thread(void* param)
{
	xfInfo* xfi;
	xfEvent* event;
	xfPeerContext* xfp;
	freerdp_peer* client;
	uint32 wait_interval;

	client = (freerdp_peer*) param;
	xfp = (xfPeerContext*) client->context;
	xfi = xfp->info;

	wait_interval = 1000000 / xfp->fps;

	while (1)
	{
		event = xf_event_new(XF_EVENT_TYPE_FRAME_TICK);
		xf_event_push(xfp->event_queue, (xfEvent*) event);
		freerdp_usleep(wait_interval);
	}
}

void* xf_monitor_updates(void* param)
{
	xfInfo* xfi;
	XEvent xevent;
	xfPeerContext* xfp;
	freerdp_peer* client;
	uint32 wait_interval;
	int pending_events = 0;
	int x, y, width, height;
	XDamageNotifyEvent* notify;
	xfEventRegion* event_region;

	client = (freerdp_peer*) param;
	xfp = (xfPeerContext*) client->context;
	xfi = xfp->info;

	pthread_create(&(xfp->frame_rate_thread), 0, xf_frame_rate_thread, (void*) client);

	pthread_detach(pthread_self());

	wait_interval = (1000000 / xfp->fps) / 16;

	while (1)
	{
		pthread_mutex_lock(&(xfp->mutex));
		pending_events = XPending(xfi->display);
		pthread_mutex_unlock(&(xfp->mutex));

		if (pending_events > 0)
		{
			pthread_mutex_lock(&(xfp->mutex));
			memset(&xevent, 0, sizeof(xevent));
			XNextEvent(xfi->display, &xevent);
			pthread_mutex_unlock(&(xfp->mutex));

			if (xevent.type == xfi->xdamage_notify_event)
			{
				notify = (XDamageNotifyEvent*) &xevent;

				x = notify->area.x;
				y = notify->area.y;
				width = notify->area.width;
				height = notify->area.height;

				xf_xdamage_subtract_region(xfp, x, y, width, height);

				event_region = xf_event_region_new(x, y, width, height);
				xf_event_push(xfp->event_queue, (xfEvent*) event_region);
			}
		}

		freerdp_usleep(wait_interval);
	}

	return NULL;
}
