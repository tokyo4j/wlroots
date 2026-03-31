#include <wlr/types/wlr_input_filter.h>
#include <wlr/util/log.h>
#include "util/list.h"

static void set_device(struct wlr_input_filter_keyboard *keyboard, struct wlr_keyboard *device) {
	keyboard->device = device;
	wl_list_remove(&keyboard->device_destroy.link);
	if (device != NULL) {
		wl_signal_add(&device->base.events.destroy, &keyboard->device_destroy);
	} else {
		wl_list_init(&keyboard->device_destroy.link);
	}
}

static void handle_device_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_filter_keyboard *keyboard =
		wl_container_of(listener, keyboard, device_destroy);
}

uint32_t wlr_input_filter_keyboard_notify_focus(struct wlr_input_filter_keyboard *keyboard,
		const struct wlr_input_filter_keyboard_focus_event *event) {
	const struct wl_list *filters = &keyboard->manager->keyboard_filters;
	while ((keyboard = wlr_list_get_next(keyboard, filters, link)) != NULL) {
		wlr_input_filter_focus_copy(&keyboard->focus, event->focus);

		if (keyboard->impl->focus != NULL) {
			return keyboard->impl->focus(keyboard, event);
		}
	}
	return 0;
}

void wlr_input_filter_keyboard_notify_device(struct wlr_input_filter_keyboard *keyboard,
		const struct wlr_input_filter_keyboard_device_event *event) {
	const struct wl_list *filters = &keyboard->manager->keyboard_filters;
	while ((keyboard = wlr_list_get_next(keyboard, filters, link)) != NULL) {
		if (keyboard->device == event->device) {
			return;
		}
		set_device(keyboard, event->device);

		if (keyboard->impl->device != NULL) {
			keyboard->impl->device(keyboard, event);
			return;
		}
	}
}

uint32_t wlr_input_filter_keyboard_notify_key(struct wlr_input_filter_keyboard *keyboard,
		const struct wlr_input_filter_keyboard_key_event *event) {
	const struct wl_list *filters = &keyboard->manager->keyboard_filters;
	while ((keyboard = wlr_list_get_next(keyboard, filters, link)) != NULL) {
		if (keyboard->device == NULL) {
			wlr_log(WLR_ERROR, "%s received a key event without an active device",
				keyboard->impl->name);
			return 0;
		}

		if (keyboard->impl->key != NULL) {
			return keyboard->impl->key(keyboard, event);
		}
	}
	return 0;
}

void wlr_input_filter_keyboard_notify_modifiers(struct wlr_input_filter_keyboard *keyboard,
		const struct wlr_input_filter_keyboard_modifiers_event *event) {
	const struct wl_list *filters = &keyboard->manager->keyboard_filters;
	while ((keyboard = wlr_list_get_next(keyboard, filters, link)) != NULL) {
		if (keyboard->device == NULL) {
			wlr_log(WLR_ERROR, "%s received a modifiers event without an active device",
				keyboard->impl->name);
			return;
		}

		if (keyboard->impl->modifiers != NULL) {
			keyboard->impl->modifiers(keyboard, event);
			return;
		}
	}
}

void wlr_input_filter_keyboard_init(struct wlr_input_filter_keyboard *keyboard,
		struct wlr_input_filter_manager *manager,
		const struct wlr_input_filter_keyboard_impl *impl) {
	*keyboard = (struct wlr_input_filter_keyboard){
		.impl = impl,
	};

	wlr_input_filter_focus_init(&keyboard->focus);

	keyboard->device_destroy.notify = handle_device_destroy;
	wl_list_init(&keyboard->device_destroy.link);

	wl_list_insert(manager->keyboard_filters.prev, &keyboard->link);

	struct wlr_input_filter_keyboard *next =
		wlr_list_get_next(keyboard, &manager->keyboard_filters, link);
	if (next != NULL) {
		wlr_input_filter_focus_copy(&keyboard->focus, &next->focus);
		set_device(keyboard, next->device);
	}
}

void wlr_input_filter_keyboard_finish(struct wlr_input_filter_keyboard *keyboard) {
	wlr_input_filter_focus_finish(&keyboard->focus);
	wl_list_remove(&keyboard->device_destroy.link);
	wl_list_remove(&keyboard->link);
}
