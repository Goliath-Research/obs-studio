/*
 * Spec for the FreeType text vertex-buffer overflow.
 *
 * A scene collection is JSON. OBSBasic::Load reads it with
 * obs_data_create_from_json_file_safe(), LoadData() pulls the "sources"
 * array, and obs_load_source_type() passes each source's "settings" object
 * into the source. text_ft2_source is a video source, so obs_source_update()
 * defers the plugin update until obs_source_video_tick(). That update copies
 * the "text" string with os_utf8_to_wcs_ptr(), or reads "text_file" with
 * load_text_from_file(), then calls cache_glyphs() and set_up_vertex_buffer().
 *
 * set_up_vertex_buffer() sizes the buffer with (uint32_t)wcslen(text) * 6.
 * 715827883 characters make that product wrap to 2. fill_vertex_buffer()
 * still writes six vertices per character.
 *
 * These tests fail while that write is present. They pass when the source
 * either refuses the text (no vertex buffer) or allocates a buffer that can
 * hold six vertices per character that is still in srcdata->text. A check
 * that does not bound the buffer actually filled still fails.
 *
 * Not covered here, and why:
 * - read_from_end() (log mode) feeds the same set_up_vertex_buffer(), but it
 *   seeks once per byte until it has seen log_lines newlines. A payload with
 *   no newlines cannot finish that scan in this test.
 * - The UTF-16 branch of load_text_from_file() copies raw file bytes into the
 *   wchar buffer. On this platform wchar_t is 4 bytes, so that branch is not
 *   the repeated-character path the finding describes.
 * - draw_outlines(), draw_drop_shadow(), and ft2_source_render() pass the
 *   same wrapped product to gs_draw() as a draw count. That count is the
 *   tiny wrapped value, so those calls do not write the vertex arrays.
 */

#include <errno.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include <graphics/graphics.h>
#include <obs-data.h>
#include <util/bmem.h>
#include <util/platform.h>

#include "find-font.h"
#include "text-freetype2.h"

#define WRAP_GLYPHS 715827883u

extern size_t g_last_vertex_count;

uint32_t texbuf_w = 2048;
uint32_t texbuf_h = 2048;
FT_Library ft2_lib;

static void crash_exit(const char *format, va_list args, void *param)
{
	(void)param;
	fputs("bcrash: ", stderr);
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
	exit(1);
}

static void die(const char *message)
{
	fprintf(stderr, "FAIL: %s\n", message);
	exit(1);
}

static void write_repeated(FILE *file, char ch, size_t count)
{
	char block[1 << 20];
	memset(block, ch, sizeof(block));
	while (count > 0) {
		size_t chunk = count < sizeof(block) ? count : sizeof(block);
		if (fwrite(block, 1, chunk, file) != chunk)
			die("short write");
		count -= chunk;
	}
}

static obs_data_t *load_collection(const char *path)
{
	obs_data_t *data = obs_data_create_from_json_file_safe(path, "bak");
	if (!data)
		die("scene collection JSON did not parse");
	return data;
}

static obs_data_t *source_settings(obs_data_t *collection)
{
	obs_data_array_t *sources = obs_data_get_array(collection, "sources");
	if (!sources)
		die("scene collection has no sources array");
	obs_data_t *source = obs_data_array_item(sources, 0);
	obs_data_array_release(sources);
	if (!source)
		die("scene collection sources array is empty");

	const char *id = obs_data_get_string(source, "id");
	if (strcmp(id, "text_ft2_source") != 0)
		die("source id is not text_ft2_source");

	obs_data_t *settings = obs_data_get_obj(source, "settings");
	obs_data_release(source);
	if (!settings)
		die("text source has no settings");
	return settings;
}

static struct ft2_source *source_with_font(obs_data_t *settings)
{
	obs_data_t *font_obj = obs_data_get_obj(settings, "font");
	if (!font_obj)
		die("text source settings have no font object");

	const char *font_name = obs_data_get_string(font_obj, "face");
	const char *font_style = obs_data_get_string(font_obj, "style");
	uint16_t font_size = (uint16_t)obs_data_get_int(font_obj, "size");
	uint32_t font_flags = (uint32_t)obs_data_get_int(font_obj, "flags");
	obs_data_release(font_obj);

	if (!font_name || !*font_name || font_size == 0)
		die("font settings are empty");

	struct ft2_source *src = bzalloc(sizeof(*src));
	src->antialiasing = obs_data_get_bool(settings, "antialiasing");
	src->word_wrap = obs_data_get_bool(settings, "word_wrap");
	src->log_mode = obs_data_get_bool(settings, "log_mode");
	src->log_lines = (uint32_t)obs_data_get_int(settings, "log_lines");
	src->color[0] = (uint32_t)obs_data_get_int(settings, "color1");
	src->color[1] = (uint32_t)obs_data_get_int(settings, "color2");

	uint32_t custom_width = (uint32_t)obs_data_get_int(settings, "custom_width");
	src->custom_width = custom_width >= 100 ? custom_width : 0;

	FT_Long index = 0;
	const char *path = get_font_path(font_name, font_size, font_style, font_flags, &index);
	if (!path)
		die("get_font_path found no font");
	if (FT_New_Face(ft2_lib, path, index, &src->font_face) != 0)
		die("FT_New_Face failed");
	FT_Set_Pixel_Sizes(src->font_face, 0, font_size);
	FT_Select_Charmap(src->font_face, FT_ENCODING_UNICODE);

	src->font_size = font_size;
	src->texbuf = bzalloc((size_t)texbuf_w * (size_t)texbuf_h);
	cache_standard_glyphs(src);
	return src;
}

static void load_text_like_update(struct ft2_source *src, obs_data_t *settings)
{
	bool from_file = obs_data_get_bool(settings, "from_file");
	src->from_file = from_file;

	if (from_file) {
		const char *path = obs_data_get_string(settings, "text_file");
		if (!path || !*path || !os_file_exists(path))
			die("text file from the collection is missing");
		if (src->log_mode)
			read_from_end(src, path);
		else
			load_text_from_file(src, path);
	} else {
		const char *text = obs_data_get_string(settings, "text");
		if (!text)
			die("inline text setting is missing");
		os_utf8_to_wcs_ptr(text, strlen(text), &src->text);
	}

	if (!src->text)
		die("text conversion produced no wide string");
}

static void render_text(struct ft2_source *src)
{
	if (!src->font_face)
		die("font face missing before render");
	cache_glyphs(src, src->text);
	g_last_vertex_count = 0;
	set_up_vertex_buffer(src);
}

static void expect_buffer_holds_text(struct ft2_source *src, const char *name)
{
	size_t glyphs = wcslen(src->text);
	struct gs_vb_data *data = src->vbuf ? gs_vertexbuffer_get_data(src->vbuf) : NULL;

	if (!data) {
		printf("PASS %s: %zu characters produced no vertex buffer\n", name, glyphs);
		return;
	}

	if (glyphs > SIZE_MAX / 6 || data->num < glyphs * 6) {
		fprintf(stderr,
			"FAIL %s: %zu characters need %zu vertices, buffer num is %zu (last create %zu)\n",
			name, glyphs, glyphs > SIZE_MAX / 6 ? 0 : glyphs * 6, data->num, g_last_vertex_count);
		exit(1);
	}

	printf("PASS %s: buffer num %zu holds %zu characters\n", name, data->num, glyphs);
}

static const char *choose_face(void)
{
	static const char *faces[] = {"Noto Sans", "DejaVu Sans", "Liberation Sans", "sans-serif", NULL};

	for (size_t i = 0; faces[i]; i++) {
		FT_Long index = 0;
		const char *path = get_font_path(faces[i], 32, "Regular", 0, &index);
		if (path && *path)
			return faces[i];
	}
	die("fontconfig has no usable face");
	return NULL;
}

static void write_collection(const char *path, bool from_file, const char *text_or_path, size_t text_len,
			     const char *face)
{
	FILE *file = fopen(path, "wb");
	if (!file)
		die("could not create scene collection");

	fputs("{\"sources\":[{\"id\":\"text_ft2_source\",\"versioned_id\":\"text_ft2_source_v2\","
	      "\"name\":\"Overflow\",\"settings\":{",
	      file);

	if (from_file) {
		fprintf(file, "\"from_file\":true,\"text_file\":\"%s\"", text_or_path);
	} else {
		fputs("\"from_file\":false,\"text\":\"", file);
		write_repeated(file, 'A', text_len);
		fputc('"', file);
	}

	fprintf(file,
		",\"font\":{\"face\":\"%s\",\"style\":\"Regular\",\"size\":32,\"flags\":0},"
		"\"antialiasing\":true,\"word_wrap\":false,\"log_mode\":false}}]}",
		face);

	if (fclose(file) != 0)
		die("could not finish scene collection");
}

static void run_case(const char *name, bool from_file, size_t glyphs)
{
	char dir[] = "/tmp/ft2-vertex-XXXXXX";
	if (!mkdtemp(dir))
		die("mkdtemp failed");

	char collection[512];
	char payload[512];
	snprintf(collection, sizeof(collection), "%s/collection.json", dir);
	snprintf(payload, sizeof(payload), "%s/payload.txt", dir);

	const char *face = choose_face();

	if (from_file) {
		FILE *payload_file = fopen(payload, "wb");
		if (!payload_file)
			die("could not create text payload");
		write_repeated(payload_file, 'A', glyphs);
		if (fclose(payload_file) != 0)
			die("could not finish text payload");
		write_collection(collection, true, payload, 0, face);
	} else {
		write_collection(collection, false, NULL, glyphs, face);
	}

	obs_data_t *collection_data = load_collection(collection);
	obs_data_t *settings = source_settings(collection_data);
	obs_data_release(collection_data);

	if (!from_file) {
		const char *text = obs_data_get_string(settings, "text");
		if (strlen(text) != glyphs)
			die("scene collection text length was truncated");
	}

	struct ft2_source *src = source_with_font(settings);
	load_text_like_update(src, settings);
	obs_data_release(settings);

	if (wcslen(src->text) != glyphs) {
		fprintf(stderr, "FAIL: wide text length is %zu, expected %zu\n", wcslen(src->text), glyphs);
		exit(1);
	}

	fprintf(stderr, "rendering %s with %zu characters\n", name, glyphs);
	render_text(src);
	expect_buffer_holds_text(src, name);

	remove(collection);
	remove(payload);
	rmdir(dir);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fputs("usage: freetype-vertex-overflow-test short|inline|file\n", stderr);
		return 2;
	}

	uint32_t wrapped = (uint32_t)WRAP_GLYPHS * 6u;
	if (wrapped != 2)
		die("test constant does not wrap the vertex count to 2");

	base_set_crash_handler(crash_exit, NULL);

	if (FT_Init_FreeType(&ft2_lib) != 0)
		die("FT_Init_FreeType failed");

	if (strcmp(argv[1], "short") == 0) {
		run_case("short", false, 2);
		return 0;
	}
	if (strcmp(argv[1], "inline") == 0) {
		run_case("inline", false, WRAP_GLYPHS);
		return 0;
	}
	if (strcmp(argv[1], "file") == 0) {
		run_case("file", true, WRAP_GLYPHS);
		return 0;
	}

	fputs("usage: freetype-vertex-overflow-test short|inline|file\n", stderr);
	return 2;
}
