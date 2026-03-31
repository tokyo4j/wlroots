#include <wlr/types/wlr_input_filter.h>
#include <wlr/util/log.h>
#include "util/list.h"

static struct wlr_input_filter_touch_point *find_point(
		struct wlr_input_filter_touch *touch, int32_t id) {
	for (size_t i = 0; i < touch->n_points; i++) {
		if (touch->points[i].id == id) {
			return &touch->points[i];
		}
	}
	return NULL;
}

static void remove_point(struct wlr_input_filter_touch *touch,
		struct wlr_input_filter_touch_point *point) {
	struct wlr_input_filter_touch_point *last = &touch->points[--touch->n_points];
	point->x = last->x;
	point->y = last->y;
	wlr_input_filter_focus_copy(&point->focus, &last->focus);
	wlr_input_filter_focus_finish(&last->focus);
}

void wlr_input_filter_touch_notify_position(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_position_event *event) {
	const struct wl_list *filters = &touch->manager->touch_filters;
	while ((touch = wlr_list_get_next(touch, filters, link)) != NULL) {
		struct wlr_input_filter_touch_point *point = find_point(touch, event->id);
		if (point == NULL) {
			// Skip silently to avoid noise
			return;
		}

		point->x = event->x;
		point->y = event->y;
		wlr_input_filter_focus_copy(&point->focus, event->focus);

		if (touch->impl->position != NULL) {
			struct wlr_input_filter_touch_position_event relayed = *event;
			relayed.index = point - touch->points;
			touch->impl->position(touch, &relayed);
			return;
		}
	}
}

uint32_t wlr_input_filter_touch_notify_down(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_down_event *event) {
	const struct wl_list *filters = &touch->manager->touch_filters;
	while ((touch = wlr_list_get_next(touch, filters, link)) != NULL) {
		struct wlr_input_filter_touch_point *point = find_point(touch, event->id);
		if (find_point(touch, event->id) != NULL) {
			wlr_log(WLR_ERROR, "%s received down for an existing touch point %"PRIi32,
				touch->impl->name, event->id);
			return 0;
		} else if (touch->n_points == WLR_INPUT_FILTER_MAX_TOUCH_POINTS) {
			wlr_log(WLR_ERROR, "%s has too many touch points, ignoring %"PRIi32,
				touch->impl->name, event->id);
			return 0;
		}

		point = &touch->points[touch->n_points++];
		wlr_input_filter_focus_init(&point->focus);

		point->id = event->id;
		point->x = event->x;
		point->y = event->y;
		wlr_input_filter_focus_copy(&point->focus, event->focus);

		if (touch->impl->down != NULL) {
			struct wlr_input_filter_touch_down_event relayed = *event;
			relayed.index = point - touch->points;
			return touch->impl->down(touch, &relayed);
		}
	}
	return 0;
}

uint32_t wlr_input_filter_touch_notify_up(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_up_event *event) {
	const struct wl_list *filters = &touch->manager->touch_filters;
	while ((touch = wlr_list_get_next(touch, filters, link)) != NULL) {
		struct wlr_input_filter_touch_point *point = find_point(touch, event->id);
		if (point == NULL) {
			wlr_log(WLR_ERROR, "%s received up for an unknown touch point %"PRIi32,
				touch->impl->name, event->id);
			return 0;
		}
		remove_point(touch, point);

		if (touch->impl->up != NULL) {
			struct wlr_input_filter_touch_up_event relayed = *event;
			relayed.index = point - touch->points;
			return touch->impl->up(touch, &relayed);
		}
	}
	return 0;
}

void wlr_input_filter_touch_notify_cancel(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_cancel_event *event) {
	const struct wl_list *filters = &touch->manager->touch_filters;
	while ((touch = wlr_list_get_next(touch, filters, link)) != NULL) {
		struct wlr_input_filter_touch_point *point = find_point(touch, event->id);
		if (point == NULL) {
			wlr_log(WLR_ERROR, "%s received cancel for an unknown touch point %"PRIi32,
				touch->impl->name, event->id);
			return;
		}
		remove_point(touch, point);

		if (touch->impl->cancel != NULL) {
			struct wlr_input_filter_touch_cancel_event relayed = *event;
			relayed.index = point - touch->points;
			touch->impl->cancel(touch, &relayed);
			return;
		}
	}
}

void wlr_input_filter_touch_notify_frame(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_frame_event *event) {
	const struct wl_list *filters = &touch->manager->touch_filters;
	while ((touch = wlr_list_get_next(touch, filters, link)) != NULL) {
		if (touch->impl->frame != NULL) {
			touch->impl->frame(touch, event);
			return;
		}
	}
}

void wlr_input_filter_touch_init(struct wlr_input_filter_touch *touch,
		struct wlr_input_filter_manager *manager,
		const struct wlr_input_filter_touch_impl *impl) {
	*touch = (struct wlr_input_filter_touch){
		.impl = impl,
		.manager = manager,
	};
	wl_list_insert(&manager->touch_filters, &touch->link);

	struct wlr_input_filter_touch *next =
		wlr_list_get_next(touch, &manager->touch_filters, link);
	if (next != NULL) {
		for (size_t i = 0; i < next->n_points; i++) {
			struct wlr_input_filter_touch_point *dst = &touch->points[i];
			wlr_input_filter_focus_init(&dst->focus);

			const struct wlr_input_filter_touch_point *src = &next->points[i];
			dst->id = src->id;
			dst->x = src->x;
			dst->y = src->y;
			wlr_input_filter_focus_copy(&dst->focus, &src->focus);
		}
		touch->n_points = next->n_points;
	}
}

void wlr_input_filter_touch_finish(struct wlr_input_filter_touch *touch) {
	for (size_t i = 0; i < touch->n_points; i++) {
		wlr_input_filter_focus_finish(&touch->points[i].focus);
	}
	wl_list_remove(&touch->link);
}
