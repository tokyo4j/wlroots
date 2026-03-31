#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_filter.h>
#include <wlr/util/log.h>

static void update_position(struct wlr_drag_input_filter_layer *layer, bool synthetic,
		uint32_t time_msec, const struct wlr_input_filter_focus *focus, double x, double y) {
	struct wlr_surface *surface = wlr_input_filter_focus_get_surface(focus);
	double sx = 0, sy = 0;

	if (surface != NULL) {
		double surface_x, surface_y;
		if (!wlr_input_filter_get_surface_position(layer->filter_manager,
				surface, &surface_x, &surface_y)) {
			goto out;
		}
		sx = x - surface_x;
		sy = y - surface_y;
	}

	// One of these will short-circuit
	wlr_drag_enter(layer->drag, surface, sx, sy);
	wlr_drag_send_motion(layer->drag, time_msec, sx, sy);

out:
	if (layer->icon_position.x != x || layer->icon_position.y != y) {
		layer->icon_position.x = x;
		layer->icon_position.y = y;
	}
	wl_signal_emit_mutable(&layer->events.set_icon_position, NULL);
}

static uint32_t pointer_position(struct wlr_input_filter_pointer *pointer,
		const struct wlr_input_filter_pointer_position_event *event) {
	struct wlr_drag_input_filter_layer *layer = wl_container_of(pointer, layer, pointer);
	update_position(layer, false, event->time_msec, event->focus, event->x, event->y);
	return 0;
}

static uint32_t pointer_button(struct wlr_input_filter_pointer *pointer,
		const struct wlr_input_filter_pointer_button_event *event) {
	struct wlr_drag_input_filter_layer *layer = wl_container_of(pointer, layer, pointer);
	uint32_t serial = wlr_input_filter_pointer_notify_button(pointer, event);

	if (event->button == layer->pointer_button &&
			event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		wlr_drag_drop_and_destroy(layer->drag, event->time_msec);
		return 0;
	}

	return serial;
}

static void pointer_axis(struct wlr_input_filter_pointer *pointer,
		const struct wlr_input_filter_pointer_axis_event *event) {
	// Consumed
}

static void pointer_frame(struct wlr_input_filter_pointer *pointer,
		const struct wlr_input_filter_pointer_frame_event *event) {
	// Consumed
}

static const struct wlr_input_filter_pointer_impl pointer_impl = {
	.name = "wlr_drag_input_filter_layer-pointer",
	.position = pointer_position,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
};

static void touch_position(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_position_event *event) {
	struct wlr_drag_input_filter_layer *layer = wl_container_of(touch, layer, touch);
	if (event->id == layer->touch_id) {
		update_position(layer, false, event->time_msec, event->focus, event->x, event->y);
	} else {
		wlr_input_filter_touch_notify_position(touch, event);
	}
}

static uint32_t touch_down(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_down_event *event) {
	struct wlr_drag_input_filter_layer *layer = wl_container_of(touch, layer, touch);
	assert(event->id != layer->touch_id);
	return wlr_input_filter_touch_notify_down(touch, event);
}

static uint32_t touch_up(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_up_event *event) {
	struct wlr_drag_input_filter_layer *layer = wl_container_of(touch, layer, touch);
	if (event->id == layer->touch_id) {
		wlr_drag_drop_and_destroy(layer->drag, event->time_msec);
		return 0;
	} else {
		return wlr_input_filter_touch_notify_up(touch, event);
	}
}

static void touch_cancel(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_cancel_event *event) {
	struct wlr_drag_input_filter_layer *layer = wl_container_of(touch, layer, touch);
	if (event->id == layer->touch_id) {
		wlr_drag_destroy(layer->drag);
	} else {
		wlr_input_filter_touch_notify_cancel(touch, event);
	}
}

static void touch_frame(struct wlr_input_filter_touch *touch,
		const struct wlr_input_filter_touch_frame_event *event) {
	// Consumed
}

static const struct wlr_input_filter_touch_impl touch_impl = {
	.name = "wlr_drag_input_filter_layer-touch",
	.position = touch_position,
	.down = touch_down,
	.up = touch_up,
	.cancel = touch_cancel,
	.frame = touch_frame,
};

static void handle_filter_manager_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drag_input_filter_layer *layer =
		wl_container_of(listener, layer, filter_manager_destroy);
	wlr_drag_input_filter_layer_destroy(layer);
}

static void handle_drag_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drag_input_filter_layer *layer = wl_container_of(listener, layer, drag_destroy);
	wlr_drag_input_filter_layer_destroy(layer);
}

static struct wlr_drag_input_filter_layer *layer_create(
	struct wlr_input_filter_manager *filter_manager,
		struct wlr_drag *drag, enum wlr_drag_input_filter_layer_type type) {
	struct wlr_drag_input_filter_layer *layer = calloc(1, sizeof(*layer));
	if (layer == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	layer->filter_manager = filter_manager;
	layer->filter_manager_destroy.notify = handle_filter_manager_destroy;
	wl_signal_add(&filter_manager->events.destroy, &layer->filter_manager_destroy);

	layer->drag = drag;
	layer->drag_destroy.notify = handle_drag_destroy;
	wl_signal_add(&drag->events.destroy, &layer->drag_destroy);

	wl_signal_init(&layer->events.destroy);
	wl_signal_init(&layer->events.set_icon_position);

	layer->icon_position.x = NAN;
	layer->icon_position.y = NAN;

	layer->type = type;

	wlr_drag_start(drag);

	switch (type) {
	case WLR_DRAG_INPUT_FILTER_LAYER_POINTER:
		wlr_input_filter_pointer_init(&layer->pointer, filter_manager, &pointer_impl);
		wlr_input_filter_pointer_clear_focus(&layer->pointer);
		update_position(layer, 0, true, &layer->pointer.focus,
			layer->pointer.x, layer->pointer.y);
		break;
	case WLR_DRAG_INPUT_FILTER_LAYER_TOUCH:
		wlr_input_filter_touch_init(&layer->touch, filter_manager, &touch_impl);
		wlr_input_filter_touch_notify_cancel(&layer->touch,
			&(struct wlr_input_filter_touch_cancel_event){
				.id = layer->touch_id,
			});
		for (size_t i = 0; i < layer->touch.n_points; i++) {
			struct wlr_input_filter_touch_point *point = &layer->touch.points[i];
			if (point != NULL) {
				update_position(layer, 0, true, &point->focus, point->x, point->y);
				break;
			}
		}
		break;
	}

	return layer;
}

struct wlr_drag_input_filter_layer *wlr_drag_input_filter_layer_create_pointer(
		struct wlr_input_filter *filter, struct wlr_drag *drag, uint32_t button) {
	struct wlr_drag_input_filter_layer *layer =
		layer_create(router, drag, WLR_DRAG_INPUT_FILTER_LAYER_POINTER);
	if (layer == NULL) {
		return NULL;
	}
	layer->pointer_button = button;

	return layer;
}

struct wlr_drag_input_filter_layer *wlr_drag_input_filter_layer_create_touch(
		struct wlr_input_filter *filter, struct wlr_drag *drag, int32_t id) {
	struct wlr_drag_input_filter_layer *layer =
		layer_create(router, drag, WLR_DRAG_INPUT_FILTER_LAYER_TOUCH);
	if (layer == NULL) {
		return NULL;
	}
	layer->touch_id = id;

	return layer;
}

void wlr_drag_input_filter_layer_destroy(struct wlr_drag_input_filter_layer *layer) {
	if (layer == NULL) {
		return;
	}

	wl_signal_emit_mutable(&layer->events.destroy, NULL);

	assert(wl_list_empty(&layer->events.destroy.listener_list));
	assert(wl_list_empty(&layer->events.set_icon_position.listener_list));

	switch (layer->type) {
	case WLR_DRAG_INPUT_FILTER_LAYER_POINTER:
		wlr_input_filter_pointer_refresh_position(&layer->pointer);
		wlr_input_filter_pointer_finish(&layer->pointer);
		break;
	case WLR_DRAG_INPUT_FILTER_LAYER_TOUCH:
		wlr_input_filter_touch_finish(&layer->touch);
		break;
	}

	wl_list_remove(&layer->filter_manager_destroy.link);
	wl_list_remove(&layer->drag_destroy.link);

	free(layer);
}
