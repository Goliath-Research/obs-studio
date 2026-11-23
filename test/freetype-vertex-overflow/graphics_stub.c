/*
 * Graphics stand-in for the CPU side of a dynamic vertex buffer.
 *
 * create_uv_vbuffer() calls gs_vertexbuffer_create() with GS_DYNAMIC and
 * without GS_DUP_BUFFER. libobs/graphics/graphics.c keeps that gs_vb_data
 * pointer in that case (the GS_DUP_BUFFER branch is the only one that
 * copies it). libobs-opengl/gl-vertexbuffer.c then stores the same pointer,
 * and because the buffer is dynamic it does not free the CPU arrays. The
 * GL upload uses data->num and does not change the CPU allocation.
 * gs_vertexbuffer_get_data() returns that pointer, which is what
 * fill_vertex_buffer() writes.
 *
 * This file follows that success path. OBS reaches it only after
 * obs_reset_video() has created a graphics context (frontend/widgets/OBSBasic.cpp
 * ResetVideo runs before ActivateSceneCollection). obs_enter_graphics()
 * then makes gs_vertexbuffer_create() valid. The GPU upload is not a step
 * that resizes or replaces the CPU arrays the fill loop writes.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec3.h>
#include <util/bmem.h>
#include <util/threading.h>

struct gs_vertex_buffer {
	struct gs_vb_data *data;
	size_t num;
	bool dynamic;
};

struct gs_texture {
	int alive;
};

static struct gs_texture dummy_texture;

size_t g_last_vertex_count;

void obs_enter_graphics(void) {}

void obs_leave_graphics(void) {}

gs_vertbuffer_t *gs_vertexbuffer_create(struct gs_vb_data *data, uint32_t flags)
{
	struct gs_vb_data *stored = data;

	if (data && data->num && (flags & GS_DUP_BUFFER) != 0) {
		struct gs_vb_data *copy = gs_vbdata_create();
		copy->num = data->num;
		if (data->points)
			copy->points = bmemdup(data->points, sizeof(*data->points) * data->num);
		if (data->normals)
			copy->normals = bmemdup(data->normals, sizeof(*data->normals) * data->num);
		if (data->tangents)
			copy->tangents = bmemdup(data->tangents, sizeof(*data->tangents) * data->num);
		if (data->colors)
			copy->colors = bmemdup(data->colors, sizeof(*data->colors) * data->num);
		if (data->tvarray && data->num_tex) {
			copy->num_tex = data->num_tex;
			copy->tvarray = bzalloc(sizeof(struct gs_tvertarray) * data->num_tex);
			for (size_t i = 0; i < data->num_tex; i++) {
				struct gs_tvertarray *tv = &data->tvarray[i];
				struct gs_tvertarray *new_tv = &copy->tvarray[i];
				size_t bytes = tv->width * sizeof(float) * data->num;
				new_tv->width = tv->width;
				new_tv->array = bmemdup(tv->array, bytes);
			}
		}
		stored = copy;
	}

	struct gs_vertex_buffer *vb = bzalloc(sizeof(*vb));
	vb->data = stored;
	vb->num = stored ? stored->num : 0;
	vb->dynamic = (flags & GS_DYNAMIC) != 0;
	g_last_vertex_count = vb->num;

	/*
	 * Static buffers drop the CPU copy after the GPU upload. Dynamic
	 * buffers, which create_uv_vbuffer requests, keep it.
	 */
	if (stored && !vb->dynamic) {
		gs_vbdata_destroy(stored);
		vb->data = NULL;
	}

	fprintf(stderr, "gs_vertexbuffer_create num=%zu dynamic=%d\n", vb->num, vb->dynamic);
	return vb;
}

void gs_vertexbuffer_destroy(gs_vertbuffer_t *vb)
{
	if (!vb)
		return;
	gs_vbdata_destroy(vb->data);
	bfree(vb);
}

void gs_vertexbuffer_flush(gs_vertbuffer_t *vb)
{
	(void)vb;
}

struct gs_vb_data *gs_vertexbuffer_get_data(const gs_vertbuffer_t *vb)
{
	if (!vb)
		return NULL;
	return vb->data;
}

gs_texture_t *gs_texture_create(uint32_t width, uint32_t height, enum gs_color_format color_format, uint32_t levels,
				const uint8_t **data, uint32_t flags)
{
	(void)width;
	(void)height;
	(void)color_format;
	(void)levels;
	(void)data;
	(void)flags;
	dummy_texture.alive = 1;
	return &dummy_texture;
}

void gs_texture_destroy(gs_texture_t *tex)
{
	(void)tex;
}

void gs_matrix_push(void) {}
void gs_matrix_pop(void) {}
void gs_matrix_identity(void) {}
void gs_matrix_translate3f(float x, float y, float z)
{
	(void)x;
	(void)y;
	(void)z;
}

gs_technique_t *gs_effect_get_technique(const gs_effect_t *effect, const char *name)
{
	(void)effect;
	(void)name;
	return NULL;
}

gs_eparam_t *gs_effect_get_param_by_name(const gs_effect_t *effect, const char *name)
{
	(void)effect;
	(void)name;
	return NULL;
}

size_t gs_technique_begin(gs_technique_t *technique)
{
	(void)technique;
	return 0;
}

bool gs_technique_begin_pass(gs_technique_t *technique, size_t pass)
{
	(void)technique;
	(void)pass;
	return false;
}

void gs_technique_end_pass(gs_technique_t *technique)
{
	(void)technique;
}

void gs_technique_end(gs_technique_t *technique)
{
	(void)technique;
}

void gs_effect_set_texture(gs_eparam_t *param, gs_texture_t *val)
{
	(void)param;
	(void)val;
}

void gs_effect_set_texture_srgb(gs_eparam_t *param, gs_texture_t *val)
{
	(void)param;
	(void)val;
}

void gs_effect_set_bool(gs_eparam_t *param, bool val)
{
	(void)param;
	(void)val;
}

bool gs_get_linear_srgb(void)
{
	return false;
}

void gs_enable_framebuffer_srgb(bool enable)
{
	(void)enable;
}

bool gs_framebuffer_srgb_enabled(void)
{
	return false;
}

void gs_load_vertexbuffer(gs_vertbuffer_t *vertbuffer)
{
	(void)vertbuffer;
}

void gs_load_indexbuffer(gs_indexbuffer_t *indexbuffer)
{
	(void)indexbuffer;
}

void gs_draw(enum gs_draw_mode draw_mode, uint32_t start_vert, uint32_t num_verts)
{
	(void)draw_mode;
	(void)start_vert;
	(void)num_verts;
}

struct dbus_sleep_info *dbus_sleep_info_create(void)
{
	return NULL;
}

void dbus_inhibit_sleep(struct dbus_sleep_info *dbus, const char *sleep, bool active)
{
	(void)dbus;
	(void)sleep;
	(void)active;
}

void dbus_sleep_info_destroy(struct dbus_sleep_info *dbus)
{
	(void)dbus;
}

struct portal_inhibit_info *portal_inhibit_info_create(void)
{
	return NULL;
}

void portal_inhibit(struct portal_inhibit_info *portal, const char *reason, bool active)
{
	(void)portal;
	(void)reason;
	(void)active;
}

void portal_inhibit_info_destroy(struct portal_inhibit_info *portal)
{
	(void)portal;
}

bool obs_get_video_info(struct obs_video_info *ovi)
{
	(void)ovi;
	return false;
}

int os_event_init(os_event_t **event, enum os_event_type type)
{
	(void)event;
	(void)type;
	return -1;
}

void os_event_destroy(os_event_t *event)
{
	(void)event;
}

int os_event_timedwait(os_event_t *event, unsigned long milliseconds)
{
	(void)event;
	(void)milliseconds;
	return -1;
}

int os_event_signal(os_event_t *event)
{
	(void)event;
	return -1;
}
