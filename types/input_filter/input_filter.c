#include <assert.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_filter.h>
#include <wlr/util/log.h>

static void focus_handle_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_filter_focus *focus = wl_container_of(listener, focus, destroy);
	wlr_input_filter_focus_clear(focus);
}

static void focus_set_generic(struct wlr_input_filter_focus *focus,
		enum wlr_input_filter_focus_type type, struct wl_signal *destroy_signal) {
	focus->type = type;
	wl_list_remove(&focus->destroy.link);
	if (destroy_signal != NULL) {
		wl_signal_add(destroy_signal, &focus->destroy);
	} else {
		wl_list_init(&focus->destroy.link);
	}
}

void wlr_input_filter_at(struct wlr_input_filter_manager *manager, double x, double y,
		struct wlr_input_filter_focus *focus, double *local_x, double *local_y) {
	struct wlr_input_filter_focus focus_placeholder;
	wlr_input_filter_focus_init(&focus_placeholder);
	if (focus == NULL) {
		focus = &focus_placeholder;
	}

	double local_x_placeholder, local_y_placeholder;
	if (local_x == NULL) {
		local_x = &local_x_placeholder;
	}
	if (local_y == NULL) {
		local_y = &local_y_placeholder;
	}

	wlr_input_filter_focus_clear(focus);
	*local_x = NAN;
	*local_y = NAN;

	if (manager->impl->at != NULL) {
		manager->impl->at(manager, x, y, focus, local_x, local_y);
	}

	wlr_input_filter_focus_finish(&focus_placeholder);
}

bool wlr_input_filter_get_surface_position(
		struct wlr_input_filter_manager *manager,
		struct wlr_surface *surface, double *x, double *y) {
	double x_placeholder, y_placeholder;
	if (x == NULL) {
		x = &x_placeholder;
	}
	if (y == NULL) {
		y = &y_placeholder;
	}

	*x = NAN;
	*y = NAN;

	if (manager->impl->get_surface_position != NULL) {
		return manager->impl->get_surface_position(manager, surface, x, y);
	}
	return false;
}

void wlr_input_filter_manager_init(struct wlr_input_filter_manager *manager,
		const struct wlr_input_filter_interface *impl) {
	*manager = (struct wlr_input_filter_manager){
		.impl = impl,
	};

	wlr_addon_set_init(&manager->addons);

	wl_signal_init(&manager->events.destroy);
}

void wlr_input_filter_manager_finish(struct wlr_input_filter_manager *manager) {
	wl_signal_emit_mutable(&manager->events.destroy, NULL);

	wlr_addon_set_finish(&manager->addons);

	assert(wl_list_empty(&manager->events.destroy.listener_list));

	assert(wl_list_empty(&manager->keyboard_filters));
	assert(wl_list_empty(&manager->pointer_filters));
	assert(wl_list_empty(&manager->touch_filters));
}

void wlr_input_filter_focus_init(struct wlr_input_filter_focus *focus) {
	*focus = (struct wlr_input_filter_focus) {
		.type = WLR_INPUT_FILTER_FOCUS_NONE,
		.destroy.notify = focus_handle_destroy,
	};
	wl_list_init(&focus->destroy.link);
}

void wlr_input_filter_focus_finish(struct wlr_input_filter_focus *focus) {
	wl_list_remove(&focus->destroy.link);
}

bool wlr_input_filter_focus_is_none(const struct wlr_input_filter_focus *focus) {
	return focus == NULL || focus->type == WLR_INPUT_FILTER_FOCUS_NONE;
}

struct wlr_surface *wlr_input_filter_focus_get_surface(
		const struct wlr_input_filter_focus *focus) {
	return focus != NULL && focus->type == WLR_INPUT_FILTER_FOCUS_SURFACE ? focus->surface : NULL;
}

void *wlr_input_filter_focus_get_user(const struct wlr_input_filter_focus *focus) {
	return focus != NULL && focus->type == WLR_INPUT_FILTER_FOCUS_USER ? focus->user : NULL;
}

void wlr_input_filter_focus_clear(struct wlr_input_filter_focus *focus) {
	focus_set_generic(focus, WLR_INPUT_FILTER_FOCUS_NONE, NULL);
}

void wlr_input_filter_focus_set_surface(struct wlr_input_filter_focus *focus,
		struct wlr_surface *surface) {
	if (surface != NULL) {
		focus_set_generic(focus, WLR_INPUT_FILTER_FOCUS_SURFACE, &surface->events.destroy);
		focus->surface = surface;
	} else {
		wlr_input_filter_focus_clear(focus);
	}
}

void wlr_input_filter_focus_set_user(struct wlr_input_filter_focus *focus,
		void *user, struct wl_signal *destroy_signal) {
	if (user != NULL) {
		focus_set_generic(focus, WLR_INPUT_FILTER_FOCUS_USER, destroy_signal);
		focus->user = user;
		focus->destroy_signal = destroy_signal;
	} else {
		wlr_input_filter_focus_clear(focus);
	}
}

void wlr_input_filter_focus_copy(struct wlr_input_filter_focus *dst,
		const struct wlr_input_filter_focus *src) {
	if (src == NULL) {
		wlr_input_filter_focus_clear(dst);
		return;
	}

	switch (src->type) {
	case WLR_INPUT_FILTER_FOCUS_NONE:
		wlr_input_filter_focus_clear(dst);
		break;
	case WLR_INPUT_FILTER_FOCUS_SURFACE:
		wlr_input_filter_focus_set_surface(dst, src->surface);
		break;
	case WLR_INPUT_FILTER_FOCUS_USER:
		wlr_input_filter_focus_set_user(dst, src->user, src->destroy_signal);
		break;
	}
}
