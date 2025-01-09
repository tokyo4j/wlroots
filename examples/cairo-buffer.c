
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <cairo.h>
#include <drm_fourcc.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/interfaces/wlr_buffer.h>

/* Simple scene-graph example with a custom buffer drawn by Cairo.
 *
 * Input is unimplemented. Surfaces are unimplemented. */

struct cairo_buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
};

static void cairo_buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct cairo_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	wlr_buffer_finish(wlr_buffer);
	cairo_surface_destroy(buffer->surface);
	free(buffer);
}

static bool cairo_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct cairo_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) {
		return false;
	}

	*format = DRM_FORMAT_ARGB8888;
	*data = cairo_image_surface_get_data(buffer->surface);
	*stride = cairo_image_surface_get_stride(buffer->surface);
	return true;
}

static void cairo_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
}

static const struct wlr_buffer_impl cairo_buffer_impl = {
	.destroy = cairo_buffer_destroy,
	.begin_data_ptr_access = cairo_buffer_begin_data_ptr_access,
	.end_data_ptr_access = cairo_buffer_end_data_ptr_access
};

static struct cairo_buffer *create_cairo_buffer(int width, int height) {
	struct cairo_buffer *buffer = calloc(1, sizeof(*buffer));
	if (!buffer) {
		return NULL;
	}

	wlr_buffer_init(&buffer->base, &cairo_buffer_impl, width, height);

	buffer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
			width, height);
	if (cairo_surface_status(buffer->surface) != CAIRO_STATUS_SUCCESS) {
		free(buffer);
		return NULL;
	}

	return buffer;
}

struct server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;

	struct wl_listener new_output;
};

struct output {
	struct wl_list link;
	struct server *server;
	struct wlr_output *wlr;
	struct wlr_scene_output *scene_output;

	struct wl_listener frame;
};

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct output *output = wl_container_of(listener, output, frame);

	wlr_scene_output_commit(output->scene_output, NULL);
}

static struct output *last_output;

static void server_handle_new_output(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct output *output = calloc(1, sizeof(*output));
	last_output = output;
	output->wlr = wlr_output;
	output->server = server;
	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->scene_output = wlr_scene_output_create(server->scene, wlr_output);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
}

static void
set_output_scale(struct output *output, float scale)
{
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_scale(&state, 2.0);
	wlr_output_commit_state(output->wlr, &state);
	wlr_output_state_finish(&state);
}

static struct wl_listener outputs_update_listener;
static void
handle_outputs_update(struct wl_listener *listener, void *data)
{
	wlr_log(WLR_ERROR, "outputs_update");
}

static struct wl_listener output_enter_listener;
static void
handle_output_enter(struct wl_listener *listener, void *data)
{
	wlr_log(WLR_ERROR, "output_enter");
}

static struct wl_listener output_leave_listener;
static void
handle_output_leave(struct wl_listener *listener, void *data)
{
	wlr_log(WLR_ERROR, "output_leave");
}

int main(void) {
	wlr_log_init(WLR_DEBUG, NULL);

	struct server server = {0};
	server.display = wl_display_create();
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.display), NULL);
	server.scene = wlr_scene_create();
	wlr_scene_node_set_enabled(&server.scene->tree.node, false);

	server.renderer = wlr_renderer_autocreate(server.backend);
	wlr_renderer_init_wl_display(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend,
			server.renderer);

	server.new_output.notify = server_handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	if (!wlr_backend_start(server.backend)) {
		wl_display_destroy(server.display);
		return EXIT_FAILURE;
	}

	struct cairo_buffer *buffer = create_cairo_buffer(256, 256);
	cairo_t *cr = cairo_create(buffer->surface);
	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);

	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_create(
			&server.scene->tree, &buffer->base);
	wlr_scene_node_set_position(&scene_buffer->node, 50, 50);
	wlr_buffer_drop(&buffer->base);

	outputs_update_listener.notify = handle_outputs_update;
	wl_signal_add(&scene_buffer->events.outputs_update, &outputs_update_listener);
	output_enter_listener.notify = handle_output_enter;
	wl_signal_add(&scene_buffer->events.output_enter, &output_enter_listener);
	output_leave_listener.notify = handle_output_leave;
	wl_signal_add(&scene_buffer->events.output_leave, &output_leave_listener);

	wlr_log(WLR_ERROR, "-- showing the buffer --");
	wlr_scene_node_set_enabled(&server.scene->tree.node, true);
	wlr_log(WLR_ERROR, "-- hiding the buffer --");
	wlr_scene_node_set_enabled(&scene_buffer->node, false);
	wlr_log(WLR_ERROR, "-- updating output scale --");
	set_output_scale(last_output, 2);
	wlr_log(WLR_ERROR, "-- showing the buffer again --");
	wlr_scene_node_set_enabled(&scene_buffer->node, true);

	wl_display_run(server.display);

	wl_display_destroy(server.display);
	return EXIT_SUCCESS;
}
