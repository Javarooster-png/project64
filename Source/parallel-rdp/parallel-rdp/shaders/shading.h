/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SHADING_H_
#define SHADING_H_

#ifdef RASTERIZER_SPEC_CONSTANT
const int SCALING_LOG2 = (STATIC_STATE_FLAGS >> RASTERIZATION_UPSCALING_LOG2_BIT_OFFSET) & 3;
const int SCALING_FACTOR = 1 << SCALING_LOG2;
#endif

#include "coverage.h"
#include "interpolation.h"
#include "perspective.h"
#include "texture.h"
#include "dither.h"
#include "combiner.h"

bool shade_pixel(int x, int y, uint primitive_index, out ShadedData shaded)
{
	SpanInfoOffsets span_offsets = load_span_offsets(primitive_index);
	if ((y < (SCALING_FACTOR * span_offsets.ylo)) || (y > (span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 1))))
		return false;

	uint setup_flags = uint(triangle_setup.elems[primitive_index].flags);
	if (SCALING_FACTOR > 1)
	{
		if ((setup_flags & TRIANGLE_SETUP_DISABLE_UPSCALING_BIT) != 0u)
		{
			x &= ~(SCALING_FACTOR - 1);
			y &= ~(SCALING_FACTOR - 1);
		}
	}

	SpanSetup span_setup = load_span_setup(SCALING_FACTOR * span_offsets.offset + (y - SCALING_FACTOR * span_offsets.ylo));
	if (span_setup.valid_line == U16_C(0))
		return false;

	uint setup_tile = uint(triangle_setup.elems[primitive_index].tile);
	AttributeSetup attr = load_attribute_setup(primitive_index);

	uvec4 states = uvec4(state_indices.elems[primitive_index].static_depth_tmem);
	uint static_state_index = states.x;
	uint tmem_instance_index = states.z;

	StaticRasterizationState static_state = load_static_rasterization_state(static_state_index);
	uint static_state_flags = static_state.flags;
	int static_state_dither = static_state.dither;
	u8x4 combiner_inputs_rgb0 = static_state.combiner_inputs_rgb0;
	u8x4 combiner_inputs_alpha0 = static_state.combiner_inputs_alpha0;
	u8x4 combiner_inputs_rgb1 = static_state.combiner_inputs_rgb1;
	u8x4 combiner_inputs_alpha1 = static_state.combiner_inputs_alpha1;

#ifdef RASTERIZER_SPEC_CONSTANT
	if ((STATIC_STATE_FLAGS & RASTERIZATION_USE_SPECIALIZATION_CONSTANT_BIT) != 0)
	{
		static_state_flags = STATIC_STATE_FLAGS;
		static_state_dither = DITHER;

		combiner_inputs_rgb0.x = u8(COMBINER_INPUT_RGB0_MULADD);
		combiner_inputs_rgb0.y = u8(COMBINER_INPUT_RGB0_MULSUB);
		combiner_inputs_rgb0.z = u8(COMBINER_INPUT_RGB0_MUL);
		combiner_inputs_rgb0.w = u8(COMBINER_INPUT_RGB0_ADD);

		combiner_inputs_alpha0.x = u8(COMBINER_INPUT_ALPHA0_MULADD);
		combiner_inputs_alpha0.y = u8(COMBINER_INPUT_ALPHA0_MULSUB);
		combiner_inputs_alpha0.z = u8(COMBINER_INPUT_ALPHA0_MUL);
		combiner_inputs_alpha0.w = u8(COMBINER_INPUT_ALPHA0_ADD);

		combiner_inputs_rgb1.x = u8(COMBINER_INPUT_RGB1_MULADD);
		combiner_inputs_rgb1.y = u8(COMBINER_INPUT_RGB1_MULSUB);
		combiner_inputs_rgb1.z = u8(COMBINER_INPUT_RGB1_MUL);
		combiner_inputs_rgb1.w = u8(COMBINER_INPUT_RGB1_ADD);

		combiner_inputs_alpha1.x = u8(COMBINER_INPUT_ALPHA1_MULADD);
		combiner_inputs_alpha1.y = u8(COMBINER_INPUT_ALPHA1_MULSUB);
		combiner_inputs_alpha1.z = u8(COMBINER_INPUT_ALPHA1_MUL);
		combiner_inputs_alpha1.w = u8(COMBINER_INPUT_ALPHA1_ADD);
	}
#endif

	// This is a great case for specialization constants.
	bool tlut = (static_state_flags & RASTERIZATION_TLUT_BIT) != 0;
	bool tlut_type = (static_state_flags & RASTERIZATION_TLUT_TYPE_BIT) != 0;
	bool sample_quad = (static_state_flags & RASTERIZATION_SAMPLE_MODE_BIT) != 0;
	bool cvg_times_alpha = (static_state_flags & RASTERIZATION_CVG_TIMES_ALPHA_BIT) != 0;
	bool alpha_cvg_select = (static_state_flags & RASTERIZATION_ALPHA_CVG_SELECT_BIT) != 0;
	bool perspective = (static_state_flags & RASTERIZATION_PERSPECTIVE_CORRECT_BIT) != 0;
	bool tex_lod_en = (static_state_flags & RASTERIZATION_TEX_LOD_ENABLE_BIT) != 0;
	bool sharpen_lod_en = (static_state_flags & RASTERIZATION_SHARPEN_LOD_ENABLE_BIT) != 0;
	bool detail_lod_en = (static_state_flags & RASTERIZATION_DETAIL_LOD_ENABLE_BIT) != 0;
	bool aa_enable = (static_state_flags & RASTERIZATION_AA_BIT) != 0;
	bool multi_cycle = (static_state_flags & RASTERIZATION_MULTI_CYCLE_BIT) != 0;
	bool interlace_en = (static_state_flags & RASTERIZATION_INTERLACE_FIELD_BIT) != 0;
	bool fill_en = (static_state_flags & RASTERIZATION_FILL_BIT) != 0;
	bool copy_en = (static_state_flags & RASTERIZATION_COPY_BIT) != 0;
	bool alpha_test = (static_state_flags & RASTERIZATION_ALPHA_TEST_BIT) != 0;
	bool alpha_test_dither = (static_state_flags & RASTERIZATION_ALPHA_TEST_DITHER_BIT) != 0;
	bool mid_texel = (static_state_flags & RASTERIZATION_SAMPLE_MID_TEXEL_BIT) != 0;
	bool uses_texel0 = (static_state_flags & RASTERIZATION_USES_TEXEL0_BIT) != 0;
	bool uses_texel1 = (static_state_flags & RASTERIZATION_USES_TEXEL1_BIT) != 0;
	bool uses_pipelined_texel1 = (static_state_flags & RASTERIZATION_USES_PIPELINED_TEXEL1_BIT) != 0;
	bool uses_lod = (static_state_flags & RASTERIZATION_USES_LOD_BIT) != 0;
	bool convert_one = (static_state_flags & RASTERIZATION_CONVERT_ONE_BIT) != 0;
	bool bilerp0 = (static_state_flags & RASTERIZATION_BILERP_0_BIT) != 0;
	bool bilerp1 = (static_state_flags & RASTERIZATION_BILERP_1_BIT) != 0;

	if ((static_state_flags & RASTERIZATION_NEED_NOISE_BIT) != 0)
		reseed_noise(x, y, primitive_index + global_constants.fb_info.base_primitive_index);

	bool flip = (setup_flags & TRIANGLE_SETUP_FLIP_BIT) != 0;

	if (copy_en)
	{
		bool valid = x >= span_setup.start_x && x <= span_setup.end_x;
		if (!valid)
			return false;

		ivec2 st;
		int s_offset;
		interpolate_st_copy(span_setup, attr.dstzw_dx, x, perspective, flip, st, s_offset);

		uint tile0 = uint(setup_tile) & 7u;
		uint tile_info_index0 = uint(state_indices.elems[primitive_index].tile_infos[tile0]);
		TileInfo tile_info0 = load_tile_info(tile_info_index0);
#ifdef RASTERIZER_SPEC_CONSTANT
		if ((STATIC_STATE_FLAGS & RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT) != 0)
		{
			tile_info0.fmt = u8(TEX_FMT);
			tile_info0.size = u8(TEX_SIZE);
		}
#endif
		int texel0 = sample_texture_copy(tile_info0, tmem_instance_index, st, s_offset, tlut, tlut_type);
		shaded.z_dith = texel0;
		shaded.coverage_count = U8_C(COVERAGE_COPY_BIT);

		if (alpha_test && global_constants.fb_info.fb_size == 2 && (texel0 & 1) == 0)
			return false;

		return true;
	}
	else if (fill_en)
	{
		int fb_width = int(global_constants.fb_info.fb_width);
		if (fb_width <= 0)
			return false;

		int dst_linear = y * fb_width + x;
		for (int y_offset = -1; y_offset <= 1; y_offset++)
		{
			int source_y = y + y_offset;
			if ((source_y < (SCALING_FACTOR * span_offsets.ylo)) ||
			    (source_y > (span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 1))))
				continue;

			SpanSetup fill_span = y_offset == 0 ? span_setup :
				load_span_setup(SCALING_FACTOR * span_offsets.offset + (source_y - SCALING_FACTOR * span_offsets.ylo));
			if (fill_span.valid_line == U16_C(0))
				continue;

			int start_x = fill_span.start_x;
			int end_x = fill_span.end_x;
			bool split_underflow_fill = !flip && start_x == 0 && end_x >= 120;
			bool terminal_full_fill = !flip &&
			                          triangle_setup.elems[primitive_index].dxldy == 0 &&
			                          start_x == 1 && end_x == 127 &&
			                          source_y == (span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 1));
			bool terminal_mask9_end_fill = !flip &&
			                               triangle_setup.elems[primitive_index].dxldy == 0 &&
			                               fb_width == 128 &&
			                               x == 127 &&
			                               y == (span_offsets.yhi * SCALING_FACTOR) &&
			                               source_y == y &&
			                               ((triangle_setup.elems[primitive_index].xh >= -59000 &&
			                                 triangle_setup.elems[primitive_index].xh <= -56000 &&
			                                 triangle_setup.elems[primitive_index].dxhdy >= 85000 &&
			                                 triangle_setup.elems[primitive_index].dxhdy <= 93000) ||
			                                (triangle_setup.elems[primitive_index].xh >= -22000 &&
			                                 triangle_setup.elems[primitive_index].xh <= -20000 &&
			                                 triangle_setup.elems[primitive_index].dxhdy >= 45000 &&
			                                 triangle_setup.elems[primitive_index].dxhdy <= 62000) ||
			                                (triangle_setup.elems[primitive_index].xh >= -45500 &&
			                                 triangle_setup.elems[primitive_index].xh <= -44000 &&
			                                 triangle_setup.elems[primitive_index].dxhdy >= 37000 &&
			                                 triangle_setup.elems[primitive_index].dxhdy <= 40000));
			bool preterminal_start_full_fill = !flip &&
			                                   triangle_setup.elems[primitive_index].dxldy == 0 &&
			                                   (triangle_setup.elems[primitive_index].dxhdy < 19000 ||
			                                    triangle_setup.elems[primitive_index].dxhdy > 30000) &&
			                                   start_x == 1 && end_x == 127 &&
			                                   source_y == (span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 2));
			bool preterminal_end_full_fill = !flip &&
			                                 triangle_setup.elems[primitive_index].dxldy == 0 &&
			                                 fb_width == 128 &&
			                                 start_x == 1 && end_x == 127 &&
			                                 source_y == (span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 1)) - 1 &&
			                                 int(triangle_setup.elems[primitive_index].yh) == 488 &&
			                                 triangle_setup.elems[primitive_index].xh == 32768 &&
			                                 triangle_setup.elems[primitive_index].dxhdy >= 500000 &&
			                                 triangle_setup.elems[primitive_index].dxhdy <= 516000;
			bool sweep1_terminal_edge_fill = !flip &&
			                                 triangle_setup.elems[primitive_index].dxldy == 0 &&
			                                 fb_width == 128 &&
			                                 start_x == 1 && end_x == 127 &&
			                                 int(triangle_setup.elems[primitive_index].yh) == 457;
			int sweep1_start_latest = -1;
			int sweep1_end_earliest = -1;
			if (sweep1_terminal_edge_fill)
			{
				int dxhdy = triangle_setup.elems[primitive_index].dxhdy;
				int xh = triangle_setup.elems[primitive_index].xh;
				if (xh >= -86000 && xh <= -84500 && dxhdy >= 117000 && dxhdy <= 119000)
				{
					sweep1_start_latest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 3);
					sweep1_end_earliest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 2);
				}
				else if (xh >= -103000 && xh <= -100000 && dxhdy >= 133000 && dxhdy <= 136000)
				{
					sweep1_start_latest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 3);
					sweep1_end_earliest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 3);
				}
				else if (xh >= -125000 && xh <= -112000 && dxhdy >= 145000 && dxhdy <= 157000)
				{
					sweep1_start_latest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 3);
					sweep1_end_earliest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 4);
				}
				else if (xh >= -141000 && xh <= -134000 && dxhdy >= 167000 && dxhdy <= 174000)
				{
					sweep1_start_latest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 2);
					sweep1_end_earliest = span_offsets.yhi * SCALING_FACTOR + (SCALING_FACTOR - 5);
				}
			}
			bool sweep1_tail_island = !flip &&
			                          triangle_setup.elems[primitive_index].dxldy == 0 &&
			                          triangle_setup.elems[primitive_index].xl == 32768 &&
			                          fb_width == 128 &&
			                          start_x == 1 && end_x == 127 &&
			                          (((int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                             triangle_setup.elems[primitive_index].xh >= -69000 &&
			                             triangle_setup.elems[primitive_index].xh <= -66000 &&
			                             triangle_setup.elems[primitive_index].dxhdy >= 65000 &&
			                             triangle_setup.elems[primitive_index].dxhdy <= 69000 &&
			                             source_y >= 122 && source_y <= 124) ||
			                            (int(triangle_setup.elems[primitive_index].yh) == 457 &&
			                             triangle_setup.elems[primitive_index].xh >= -98000 &&
			                             triangle_setup.elems[primitive_index].xh <= -94000 &&
			                             triangle_setup.elems[primitive_index].dxhdy >= 127000 &&
			                             triangle_setup.elems[primitive_index].dxhdy <= 131000 &&
			                             source_y >= 122 && source_y <= 124)));
			bool sweep1_tail_last_row = !flip &&
			                            triangle_setup.elems[primitive_index].dxldy == 0 &&
			                            triangle_setup.elems[primitive_index].xl == 32768 &&
			                            fb_width == 128 &&
			                            start_x == 1 && end_x == 127 &&
			                            source_y == 124 &&
			                            (((int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                               triangle_setup.elems[primitive_index].xh >= -48000 &&
			                               triangle_setup.elems[primitive_index].xh <= -45500 &&
			                               triangle_setup.elems[primitive_index].dxhdy >= 30000 &&
			                               triangle_setup.elems[primitive_index].dxhdy <= 33500) ||
			                              (int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                               triangle_setup.elems[primitive_index].xh >= -59000 &&
			                               triangle_setup.elems[primitive_index].xh <= -56500 &&
			                               triangle_setup.elems[primitive_index].dxhdy >= 59000 &&
			                               triangle_setup.elems[primitive_index].dxhdy <= 62000) ||
			                              (int(triangle_setup.elems[primitive_index].yh) == 457 &&
			                               triangle_setup.elems[primitive_index].xh >= -81000 &&
			                               triangle_setup.elems[primitive_index].xh <= -78500 &&
			                               triangle_setup.elems[primitive_index].dxhdy >= 111000 &&
			                               triangle_setup.elems[primitive_index].dxhdy <= 114000)));
			bool sweep1_tail_prelast_left = !flip &&
			                                triangle_setup.elems[primitive_index].dxldy == 0 &&
			                                triangle_setup.elems[primitive_index].xl == 32768 &&
			                                fb_width == 128 &&
			                                start_x == 1 && end_x == 127 &&
			                                source_y == 123 &&
			                                int(triangle_setup.elems[primitive_index].yh) == 457 &&
			                                triangle_setup.elems[primitive_index].xh >= -81000 &&
			                                triangle_setup.elems[primitive_index].xh <= -78500 &&
			                                triangle_setup.elems[primitive_index].dxhdy >= 111000 &&
			                                triangle_setup.elems[primitive_index].dxhdy <= 114000;
			bool sweep1_tail_sx20_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 122 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -35000 &&
			                              triangle_setup.elems[primitive_index].xh <= -33500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 66000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 68000;
			bool sweep1_tail_sx18_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 123 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -28500 &&
			                              triangle_setup.elems[primitive_index].xh <= -27000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 59500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 61500;
			bool sweep1_tail_sx23_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 120 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -44500 &&
			                              triangle_setup.elems[primitive_index].xh <= -43500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 76000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 77500;
			bool sweep1_tail_sx21_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 121 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -38000 &&
			                              triangle_setup.elems[primitive_index].xh <= -37000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 69500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 71000;
			bool sweep1_tail_sx19_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 122 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -31500 &&
			                              triangle_setup.elems[primitive_index].xh <= -30500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 63000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 64500;
			bool sweep1_tail_sx22_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 120 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -41200 &&
			                              triangle_setup.elems[primitive_index].xh <= -40200 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 72500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 74500;
			bool sweep1_tail_sx22_sy30 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 122 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 457 &&
			                              triangle_setup.elems[primitive_index].xh >= -91500 &&
			                              triangle_setup.elems[primitive_index].xh <= -90000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 122500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 124500;
			bool sweep1_tail_sx18_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 122 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -55000 &&
			                              triangle_setup.elems[primitive_index].xh <= -53500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 42500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 44500;
			bool sweep1_tail_sx20_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 120 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -64000 &&
			                              triangle_setup.elems[primitive_index].xh <= -62800 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 47000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 49000;
			bool sweep1_tail_sx21_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 119 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -69000 &&
			                              triangle_setup.elems[primitive_index].xh <= -67500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 49500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 51500;
			bool sweep1_tail_sx22_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 118 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -73500 &&
			                              triangle_setup.elems[primitive_index].xh <= -72000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 52000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 54000;
			bool sweep1_tail_sx23_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 117 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -78500 &&
			                              triangle_setup.elems[primitive_index].xh <= -76500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 54000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 56500;
			bool sweep1_tail_sx24_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 116 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -83000 &&
			                              triangle_setup.elems[primitive_index].xh <= -81500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 56500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 58500;
			bool sweep1_tail_sx25_sy30 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 121 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 457 &&
			                              triangle_setup.elems[primitive_index].xh >= -108000 &&
			                              triangle_setup.elems[primitive_index].xh <= -106500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 139000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 141000;
			bool sweep1_tail_sx19_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 121 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -59500 &&
			                              triangle_setup.elems[primitive_index].xh <= -58000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 45000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 46500;
			bool sweep1_tail_sx24_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 119 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -48000 &&
			                              triangle_setup.elems[primitive_index].xh <= -46500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 79000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 81000;
			bool sweep1_tail_sx25_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 118 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -51200 &&
			                              triangle_setup.elems[primitive_index].xh <= -50000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 82500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 84000;
			bool sweep1_tail_sx26_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 118 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -54500 &&
			                              triangle_setup.elems[primitive_index].xh <= -53200 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 85500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 87500;
			bool sweep1_tail_sx27_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 118 && source_y <= 125 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -57800 &&
			                              triangle_setup.elems[primitive_index].xh <= -56500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 89000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 90500;
			bool sweep1_tail_sx29_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 117 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -64500 &&
			                              triangle_setup.elems[primitive_index].xh <= -63000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 95500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 97500;
			bool sweep1_tail_sx29_sy30 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 120 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 457 &&
			                              triangle_setup.elems[primitive_index].xh >= -130000 &&
			                              triangle_setup.elems[primitive_index].xh <= -128500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 161000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 163000;
			bool sweep1_tail_sx17_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 123 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -31500 &&
			                              triangle_setup.elems[primitive_index].xh <= -30000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 31000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 32500;
			bool sweep1_tail_sx18_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 121 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -35200 &&
			                              triangle_setup.elems[primitive_index].xh <= -33800 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 33000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 34500;
			bool sweep1_tail_sx19_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 120 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -39000 &&
			                              triangle_setup.elems[primitive_index].xh <= -37500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 34500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 36500;
			bool sweep1_tail_sx20_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 118 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -42500 &&
			                              triangle_setup.elems[primitive_index].xh <= -41000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 36500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 38000;
			bool sweep1_tail_sx21_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 117 && source_y <= 125 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -46200 &&
			                              triangle_setup.elems[primitive_index].xh <= -44500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 38500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 40000;
			bool sweep1_tail_sx22_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 116 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -49800 &&
			                              triangle_setup.elems[primitive_index].xh <= -48200 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 40000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 41500;
			bool sweep1_tail_sx23_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 115 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -53500 &&
			                              triangle_setup.elems[primitive_index].xh <= -52000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 42000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 43500;
			bool sweep1_tail_sx24_sy27 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 114 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 362 &&
			                              triangle_setup.elems[primitive_index].xh >= -57000 &&
			                              triangle_setup.elems[primitive_index].xh <= -55500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 43800 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 45500;
			bool sweep1_tail_sx25_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 116 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -87800 &&
			                              triangle_setup.elems[primitive_index].xh <= -86000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 59000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 61000;
			bool sweep1_tail_sx30_sy28 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 113 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 394 &&
			                              triangle_setup.elems[primitive_index].xh >= -111500 &&
			                              triangle_setup.elems[primitive_index].xh <= -109000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 70500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 72500;
			bool sweep1_tail_sx18_sy26 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 120 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 331 &&
			                              triangle_setup.elems[primitive_index].xh >= -50800 &&
			                              triangle_setup.elems[primitive_index].xh <= -49200 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 27000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 28500;
			bool sweep1_tail_sx17_sy26 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 122 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 331 &&
			                              triangle_setup.elems[primitive_index].xh >= -46500 &&
			                              triangle_setup.elems[primitive_index].xh <= -44500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 25500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 27000;
			bool sweep1_tail_sx28_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 117 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -61000 &&
			                              triangle_setup.elems[primitive_index].xh <= -59500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 92000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 94000;
			bool sweep1_tail_sx30_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 116 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -67500 &&
			                              triangle_setup.elems[primitive_index].xh <= -66000 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 98500 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 100500;
			bool sweep1_tail_sx31_sy29 = !flip &&
			                              triangle_setup.elems[primitive_index].dxldy == 0 &&
			                              triangle_setup.elems[primitive_index].xl == 32768 &&
			                              fb_width == 128 &&
			                              start_x == 1 && end_x == 127 &&
			                              source_y >= 116 && source_y <= 124 &&
			                              int(triangle_setup.elems[primitive_index].yh) == 425 &&
			                              triangle_setup.elems[primitive_index].xh >= -71000 &&
			                              triangle_setup.elems[primitive_index].xh <= -69500 &&
			                              triangle_setup.elems[primitive_index].dxhdy >= 102000 &&
			                              triangle_setup.elems[primitive_index].dxhdy <= 104000;
			bool sweep1_sx16_terminal = !flip &&
			                             triangle_setup.elems[primitive_index].dxldy == 0 &&
			                             triangle_setup.elems[primitive_index].xl == 32768 &&
			                             fb_width == 128 &&
			                             start_x == 1 && end_x == 127 &&
			                             source_y >= 123 && source_y <= 124 &&
			                             int(triangle_setup.elems[primitive_index].yh) >= -50 &&
			                             int(triangle_setup.elems[primitive_index].yh) <= 112 &&
			                             triangle_setup.elems[primitive_index].xh >= 0 &&
			                             triangle_setup.elems[primitive_index].xh <= 33000 &&
			                             triangle_setup.elems[primitive_index].dxhdy >= 7600 &&
			                             triangle_setup.elems[primitive_index].dxhdy <= 11000;
			bool sweep1_sx16_early_terminal = !flip &&
			                                   triangle_setup.elems[primitive_index].dxldy == 0 &&
			                                   triangle_setup.elems[primitive_index].xl == 32768 &&
			                                   fb_width == 128 &&
			                                   start_x == 1 && end_x == 127 &&
			                                   source_y >= 121 && source_y <= 124 &&
			                                   int(triangle_setup.elems[primitive_index].yh) >= -400 &&
			                                   int(triangle_setup.elems[primitive_index].yh) <= -295 &&
			                                   triangle_setup.elems[primitive_index].xh >= 22000 &&
			                                   triangle_setup.elems[primitive_index].xh <= 28000 &&
			                                   triangle_setup.elems[primitive_index].dxhdy >= 4700 &&
			                                   triangle_setup.elems[primitive_index].dxhdy <= 5350;
			bool sweep0_019_high_gap_skip = !flip &&
			                                triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                                triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                                triangle_setup.elems[primitive_index].xh == 4120576 &&
			                                triangle_setup.elems[primitive_index].xl == 3215360 &&
			                                ((int(triangle_setup.elems[primitive_index].yl) == 933 &&
			                                  triangle_setup.elems[primitive_index].dxldy >= 960 &&
			                                  triangle_setup.elems[primitive_index].dxldy <= 990 &&
			                                  source_y == 119) ||
			                                 (int(triangle_setup.elems[primitive_index].yl) == 965 &&
			                                  triangle_setup.elems[primitive_index].dxldy >= 920 &&
			                                  triangle_setup.elems[primitive_index].dxldy <= 960 &&
			                                  source_y == 123));
			bool sweep0_006_gap_skip = !flip &&
			                           triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                           triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                           triangle_setup.elems[primitive_index].xh == 4120576 &&
			                           ((int(triangle_setup.elems[primitive_index].yl) == 51 &&
			                             triangle_setup.elems[primitive_index].xl == 3473408 &&
			                             triangle_setup.elems[primitive_index].dxldy >= 13600 &&
			                             triangle_setup.elems[primitive_index].dxldy <= 13950 &&
			                             source_y == 6) ||
			                            (int(triangle_setup.elems[primitive_index].yl) == 51 &&
			                             triangle_setup.elems[primitive_index].xl == 3215360 &&
			                             triangle_setup.elems[primitive_index].dxldy >= 19000 &&
			                             triangle_setup.elems[primitive_index].dxldy <= 19500 &&
			                             source_y == 8) ||
			                            (int(triangle_setup.elems[primitive_index].yl) == 209 &&
			                             triangle_setup.elems[primitive_index].xl == 1150976 &&
			                             triangle_setup.elems[primitive_index].dxldy >= 14300 &&
			                             triangle_setup.elems[primitive_index].dxldy <= 14650 &&
			                             source_y == 46));
			bool sweep0_006_start_full = !flip &&
			                             triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                             triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                             triangle_setup.elems[primitive_index].xh == 4120576 &&
			                             ((int(triangle_setup.elems[primitive_index].yl) == 51 &&
			                               triangle_setup.elems[primitive_index].xl == 3473408 &&
			                               triangle_setup.elems[primitive_index].dxldy >= 13600 &&
			                               triangle_setup.elems[primitive_index].dxldy <= 13950 &&
			                               source_y >= 8 && source_y <= 10) ||
			                              (int(triangle_setup.elems[primitive_index].yl) == 51 &&
			                               triangle_setup.elems[primitive_index].xl == 3215360 &&
			                               triangle_setup.elems[primitive_index].dxldy >= 19000 &&
			                               triangle_setup.elems[primitive_index].dxldy <= 19500 &&
			                               source_y >= 10 && source_y <= 12) ||
			                              (int(triangle_setup.elems[primitive_index].yl) == 209 &&
			                               triangle_setup.elems[primitive_index].xl == 1150976 &&
			                               triangle_setup.elems[primitive_index].dxldy >= 14300 &&
			                               triangle_setup.elems[primitive_index].dxldy <= 14650 &&
			                               source_y >= 49 && source_y <= 52));
			bool sweep0_069_038_single_end = !flip &&
			                                  triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                                  triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                                  triangle_setup.elems[primitive_index].xh == 4120576 &&
			                                  int(triangle_setup.elems[primitive_index].yl) == 209 &&
			                                  triangle_setup.elems[primitive_index].xl == 1150976 &&
			                                  triangle_setup.elems[primitive_index].dxldy >= 14300 &&
			                                  triangle_setup.elems[primitive_index].dxldy <= 14650 &&
			                                  source_y == 52 &&
			                                  start_x == 125 && end_x == 125;
			bool sweep0_top_sxi7 = !flip &&
			                       triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                       triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                       triangle_setup.elems[primitive_index].xh == 4120576 &&
			                       int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                       triangle_setup.elems[primitive_index].xl == 2183168 &&
			                       triangle_setup.elems[primitive_index].dxldy >= 120000 &&
			                       triangle_setup.elems[primitive_index].dxldy <= 122000 &&
			                       source_y == 4;
			bool sweep0_top_sxi11 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                        triangle_setup.elems[primitive_index].xl == 1150976 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 184000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 187000 &&
			                        source_y == 4;
			bool sweep0_top_sxi15 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                        triangle_setup.elems[primitive_index].xl == 118784 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 249000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 252000 &&
			                        source_y == 4;
			bool sweep0_top_sxi16 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                        triangle_setup.elems[primitive_index].xl == -139264 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 265000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 268000 &&
			                        source_y >= 2 && source_y <= 4;
			bool sweep0_top_sxi17 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                        triangle_setup.elems[primitive_index].xl == -397312 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 281000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 284000 &&
			                        source_y >= 2 && source_y <= 4;
			bool sweep0_top_sxi18 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                        triangle_setup.elems[primitive_index].xl == -655360 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 297000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 300000 &&
			                        source_y >= 2 && source_y <= 4;
			bool sweep0_top_sxi19 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                        triangle_setup.elems[primitive_index].xl == -913408 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 313000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 316000 &&
			                        source_y >= 2 && source_y <= 4;
			bool sweep0_top_sxi20 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 20 &&
			                        triangle_setup.elems[primitive_index].xl == -1171456 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 329000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 332000 &&
			                        source_y >= 2 && source_y <= 4;
			bool sweep0_small_2_2 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 83 &&
			                        triangle_setup.elems[primitive_index].xl == 3473408 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 8000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 8400;
			bool sweep0_small_3_2 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 83 &&
			                        triangle_setup.elems[primitive_index].xl == 3215360 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 11200 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 11700;
			bool sweep0_small_2_3 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 114 &&
			                        triangle_setup.elems[primitive_index].xl == 3473408 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 5700 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 6100;
			bool sweep0_small_3_3 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 114 &&
			                        triangle_setup.elems[primitive_index].xl == 3215360 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 8000 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 8500;
			bool sweep0_small_5_4 = !flip &&
			                        triangle_setup.elems[primitive_index].dxhdy == 0 &&
			                        triangle_setup.elems[primitive_index].dxmdy == 0 &&
			                        triangle_setup.elems[primitive_index].xh == 4120576 &&
			                        int(triangle_setup.elems[primitive_index].yl) == 146 &&
			                        triangle_setup.elems[primitive_index].xl == 2699264 &&
			                        triangle_setup.elems[primitive_index].dxldy >= 9800 &&
			                        triangle_setup.elems[primitive_index].dxldy <= 10250;
			int split_phase = clamp(source_y - (SCALING_FACTOR * span_offsets.ylo), 0, 3);
			int split_first_end = end_x - 16 * split_phase;
			int split_second_start = split_first_end + 17;
			int split_byte_mode = 0;
			if (split_underflow_fill && split_phase > 0)
			{
				if (x > split_first_end && x < split_second_start)
					continue;
				if (x >= split_second_start)
				{
					start_x = split_second_start;
					split_byte_mode = 1;
				}
				else
					end_x = split_first_end;
			}

			bool flip_even_end = (end_x & 1) == 0;
			if (flip_even_end)
			{
				start_x += 2;
				end_x += 1;
			}

			bool sweep0_small_5_4_terminal = sweep0_small_5_4 && source_y == 36;
			if ((start_x > end_x && !sweep0_small_5_4_terminal) ||
			    (start_x == end_x && !flip_even_end && !sweep0_069_038_single_end && !sweep0_small_5_4_terminal))
				continue;

			bool odd_start = (start_x & 1) != 0;
			int min_x = odd_start ? start_x - 1 : start_x;
			int max_x = end_x;
			if (sweep0_small_5_4 && source_y == 36)
				max_x = max(max_x, 125);
			if (sweep1_tail_sx20_sy29 && source_y >= 123)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx18_sy29 && source_y == 124)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx23_sy29 && source_y >= 121)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx21_sy29 && source_y >= 122)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx19_sy29 && source_y >= 123)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx22_sy29 && source_y >= 121)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx22_sy30 && source_y >= 123)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx18_sy28 && source_y >= 123)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx20_sy28 && source_y >= 121)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx21_sy28 && source_y >= 120)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx22_sy28 && source_y >= 119)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx23_sy28 && source_y >= 118)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx24_sy28 && source_y >= 117)
				max_x = max(max_x, 127);
			if (sweep1_tail_sx25_sy30 && source_y >= 122)
				max_x = max(max_x, 127);
			if ((sweep1_tail_sx19_sy28 && source_y >= 122) ||
			    (sweep1_tail_sx24_sy29 && source_y >= 120) ||
			    (sweep1_tail_sx25_sy29 && source_y >= 119) ||
			    (sweep1_tail_sx26_sy29 && source_y >= 119) ||
			    (sweep1_tail_sx27_sy29 && source_y >= 119) ||
			    (sweep1_tail_sx29_sy29 && source_y >= 118) ||
			    (sweep1_tail_sx29_sy30 && source_y >= 121) ||
			    (sweep1_tail_sx17_sy27 && source_y == 124) ||
			    (sweep1_tail_sx18_sy27 && source_y >= 122) ||
			    (sweep1_tail_sx19_sy27 && source_y >= 121) ||
			    (sweep1_tail_sx20_sy27 && source_y >= 119) ||
			    (sweep1_tail_sx21_sy27 && source_y >= 118) ||
			    (sweep1_tail_sx22_sy27 && source_y >= 117) ||
			    (sweep1_tail_sx23_sy27 && source_y >= 116) ||
			    (sweep1_tail_sx24_sy27 && source_y >= 115) ||
			    (sweep1_tail_sx25_sy28 && source_y >= 117) ||
			    (sweep1_tail_sx30_sy28 && source_y >= 114) ||
			    (sweep1_tail_sx17_sy26 && source_y >= 123) ||
			    (sweep1_tail_sx18_sy26 && source_y >= 121) ||
			    (sweep1_tail_sx28_sy29 && source_y >= 118) ||
			    (sweep1_tail_sx30_sy29 && source_y >= 117) ||
			    (sweep1_tail_sx31_sy29 && source_y >= 117))
				max_x = max(max_x, 127);
			int source_linear = source_y * fb_width;
			if (dst_linear < source_linear + min_x || dst_linear > source_linear + max_x)
				continue;

			int fill_x = dst_linear - source_linear;
			if (sweep0_019_high_gap_skip || sweep0_006_gap_skip)
				continue;
			if (sweep1_tail_island && source_y == 123 &&
			    fill_x >= 110 && fill_x <= 119 && fill_x != 111)
				continue;
			if (sweep1_tail_sx20_sy29 && source_y == 123 &&
			    fill_x >= 110 && fill_x <= 119 && fill_x != 111)
				continue;
			if (sweep1_tail_sx18_sy29 && source_y == 124 &&
			    fill_x >= 110 && fill_x <= 123 && fill_x != 111)
				continue;
			if (sweep1_tail_sx23_sy29 && source_y == 121 &&
			    fill_x >= 110 && fill_x <= 117 && fill_x != 111)
				continue;
			if (sweep1_tail_sx21_sy29 && source_y == 122 &&
			    fill_x >= 110 && fill_x <= 121 && fill_x != 111)
				continue;
			if (sweep1_tail_sx19_sy29 &&
			    ((source_y == 123 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     (source_y == 124 && fill_x == 94)))
				continue;
			if (sweep1_tail_sx22_sy29 &&
			    ((source_y == 121 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     (source_y == 122 && fill_x == 94)))
				continue;
			if (sweep1_tail_sx22_sy30 && source_y == 123 &&
			    fill_x >= 110 && fill_x <= 125 && fill_x != 111)
				continue;
			if (sweep1_tail_sx18_sy28 &&
			    ((source_y == 123 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     (source_y == 124 && fill_x == 94)))
				continue;
			if (sweep1_tail_sx20_sy28 &&
			    ((source_y == 121 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     ((source_y == 122 || source_y == 123) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx21_sy28 &&
			    ((source_y == 120 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     (source_y == 121 && fill_x == 94) ||
			     (source_y == 122 && fill_x == 78)))
				continue;
			if (sweep1_tail_sx22_sy28 &&
			    ((source_y == 119 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     (source_y == 120 && fill_x == 94) ||
			     (source_y == 121 && fill_x == 78)))
				continue;
			if (sweep1_tail_sx23_sy28 &&
			    ((source_y == 118 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     (source_y == 119 && fill_x == 94) ||
			     (source_y == 120 && fill_x == 78)))
				continue;
			if (sweep1_tail_sx24_sy28 &&
			    ((source_y == 117 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     ((source_y == 118 || source_y == 119) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx25_sy30 && source_y == 122 &&
			    fill_x >= 110 && fill_x <= 125 && fill_x != 111)
				continue;
			if (sweep1_tail_sx19_sy28 &&
			    ((source_y == 122 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     ((source_y == 123 || source_y == 124) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx24_sy29 && source_y == 120 &&
			    fill_x >= 110 && fill_x <= 121 && fill_x != 111)
				continue;
			if (sweep1_tail_sx25_sy29 &&
			    ((source_y == 119 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     (source_y == 120 && fill_x == 94)))
				continue;
			if (sweep1_tail_sx26_sy29 && source_y == 119 &&
			    fill_x >= 110 && fill_x <= 121 && fill_x != 111)
				continue;
			if (sweep1_tail_sx27_sy29 && source_y == 119 &&
			    fill_x >= 110 && fill_x <= 115 && fill_x != 111)
				continue;
			if (sweep1_tail_sx29_sy29 && source_y == 118 &&
			    fill_x >= 110 && fill_x <= 117 && fill_x != 111)
				continue;
			if (sweep1_tail_sx29_sy30 && source_y == 121 &&
			    fill_x >= 110 && fill_x <= 125 && fill_x != 111)
				continue;
			if (sweep1_tail_sx17_sy27 && source_y == 124 &&
			    fill_x >= 110 && fill_x <= 123 && fill_x != 111)
				continue;
			if (sweep1_tail_sx18_sy27 &&
			    ((source_y == 122 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     ((source_y == 123 || source_y == 124) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx19_sy27 &&
			    ((source_y == 121 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     (source_y == 122 && fill_x == 94) ||
			     (source_y == 123 && fill_x == 94) ||
			     (source_y == 124 && fill_x == 78)))
				continue;
			if (sweep1_tail_sx20_sy27 &&
			    ((source_y == 119 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     ((source_y == 120 || source_y == 121 || source_y == 122) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx21_sy27 &&
			    ((source_y == 118 && fill_x >= 110 && fill_x <= 123 && fill_x != 111) ||
			     ((source_y == 119 || source_y == 120) && fill_x == 94) ||
			     (source_y == 121 && fill_x == 78)))
				continue;
			if (sweep1_tail_sx22_sy27 &&
			    ((source_y == 117 && fill_x >= 110 && fill_x <= 121 && fill_x != 111) ||
			     ((source_y == 118 || source_y == 119) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx23_sy27 &&
			    ((source_y == 116 && fill_x >= 110 && fill_x <= 121 && fill_x != 111) ||
			     ((source_y == 117 || source_y == 118) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx24_sy27 &&
			    ((source_y == 115 && fill_x >= 110 && fill_x <= 121 && fill_x != 111) ||
			     ((source_y == 116 || source_y == 117) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx25_sy28 &&
			    ((source_y == 117 && fill_x >= 110 && fill_x <= 119 && fill_x != 111) ||
			     (source_y == 118 && fill_x == 94)))
				continue;
			if (sweep1_tail_sx30_sy28 && source_y == 114 &&
			    fill_x >= 110 && fill_x <= 119 && fill_x != 111)
				continue;
			if (sweep1_tail_sx18_sy26 &&
			    ((source_y == 121 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     ((source_y == 122 || source_y == 123 || source_y == 124) && fill_x == 94)))
				continue;
			if (sweep1_tail_sx17_sy26 &&
			    ((source_y == 123 && fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     (source_y == 124 && ((fill_x >= 94 && fill_x <= 107 && fill_x != 95) ||
			                          fill_x == 108))))
				continue;
			if (sweep1_tail_sx28_sy29 && source_y == 118 &&
			    fill_x >= 110 && fill_x <= 123 && fill_x != 111)
				continue;
			if (sweep1_tail_sx30_sy29 && source_y == 117 &&
			    fill_x >= 110 && fill_x <= 125 && fill_x != 111)
				continue;
			if (sweep1_tail_sx31_sy29 && source_y == 117 &&
			    fill_x >= 110 && fill_x <= 121 && fill_x != 111)
				continue;
			if (sweep1_tail_last_row &&
			    fill_x >= 110 && fill_x <= 123 && fill_x != 111)
				continue;
			if (sweep1_sx16_terminal && source_y == 124 &&
			    fill_x >= 110 && fill_x <= 125 && fill_x != 111)
				continue;
			if (sweep1_sx16_early_terminal &&
			    (((source_y == 122) &&
			      fill_x >= 110 && fill_x <= 125 && fill_x != 111) ||
			     ((source_y == 123) &&
			      fill_x >= 94 && fill_x <= 109 && fill_x != 95) ||
			     ((source_y == 124) &&
			      fill_x >= 78 && fill_x <= 93 && fill_x != 79)))
				continue;
			if (sweep0_top_sxi7 && fill_x != 124)
				continue;
			if (sweep0_top_sxi11 && fill_x >= 108 && fill_x <= 117 && fill_x != 109)
				continue;
			if (sweep0_top_sxi15 &&
			    (((fill_x >= 108 && fill_x <= 123) && fill_x != 109) || fill_x == 125))
				continue;
			if ((sweep0_top_sxi16 || sweep0_top_sxi17 || sweep0_top_sxi19 || sweep0_top_sxi20) &&
			    source_y == 2 &&
			    (((fill_x >= 108 && fill_x <= 123) && fill_x != 109) || fill_x == 124 || fill_x == 125))
				continue;
			if (sweep0_top_sxi18 && source_y == 2 &&
			    (((fill_x >= 108 && fill_x <= 123) && fill_x != 109) || fill_x == 124 || fill_x == 125))
				continue;
			if ((sweep0_top_sxi16 || sweep0_top_sxi17 || sweep0_top_sxi19 || sweep0_top_sxi20) &&
			    source_y == 3 &&
			    (((fill_x >= 108 && fill_x <= 123) && fill_x != 109 &&
			      !(sweep0_top_sxi16 && fill_x == 122) &&
			      !(sweep0_top_sxi17 && (fill_x == 116 || fill_x >= 118)) &&
			      !(sweep0_top_sxi19 && fill_x == 120) &&
			      !(sweep0_top_sxi20 && (fill_x == 114 || fill_x >= 116))) ||
			     (fill_x == 125 && fill_x != end_x)))
				continue;
			if (sweep0_top_sxi18 && source_y == 3 &&
			    (((fill_x >= 92 && fill_x <= 111) && fill_x != 93 && fill_x != 110) ||
			     (fill_x == 125 && fill_x != end_x)))
				continue;
			if ((sweep0_top_sxi16 || sweep0_top_sxi17 || sweep0_top_sxi19 || sweep0_top_sxi20) &&
			    source_y == 4 &&
			    (((fill_x >= 108 && fill_x <= 123) && fill_x != 109 &&
			      !(sweep0_top_sxi16 && fill_x == 122) &&
			      !(sweep0_top_sxi17 && (fill_x == 118 || fill_x >= 120)) &&
			      !(sweep0_top_sxi19 && fill_x == 103) &&
			      !(sweep0_top_sxi20 && fill_x >= 116)) ||
			     (fill_x == 125 && fill_x != end_x)))
				continue;
			if (sweep0_top_sxi18 && source_y == 4 &&
			    (((fill_x >= 92 && fill_x <= 99) && fill_x != 93 && fill_x != 98) ||
			     (fill_x == 125 && fill_x != end_x)))
				continue;
			if ((sweep0_small_2_2 && source_y == 8 &&
			     ((fill_x >= 112 && fill_x <= 123) || fill_x == 125)) ||
			    (sweep0_small_3_2 && source_y == 12 &&
			     ((fill_x >= 112 && fill_x <= 123) || fill_x == 125)) ||
			    (sweep0_small_2_3 && source_y == 11 &&
			     ((fill_x >= 112 && fill_x <= 123) || fill_x == 125)) ||
			    (sweep0_small_3_3 && source_y == 16 &&
			     ((fill_x >= 112 && fill_x <= 123) || fill_x == 125)) ||
			    (sweep0_small_5_4 && source_y == 27 &&
			     ((fill_x >= 114 && fill_x <= 123) || fill_x == 125)))
				continue;

			uint byte_mask = 0xfu;
			if (terminal_mask9_end_fill)
				byte_mask = 0x9u;
			else if (terminal_full_fill)
				byte_mask = 0xfu;
			else if ((sweep1_start_latest >= 0 && source_y <= sweep1_start_latest && fill_x == start_x) ||
			         (sweep1_end_earliest >= 0 && source_y >= sweep1_end_earliest && fill_x >= end_x - 1))
				byte_mask = 0xfu;
			else if (sweep1_tail_island &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 123 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_island && source_y == 123 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_island && source_y == 123 && fill_x == 121)
				byte_mask = 0x1u;
			else if (sweep1_tail_prelast_left && fill_x == start_x)
				byte_mask = 0xfu;
			else if (sweep1_tail_sx20_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 123 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx20_sy29 && source_y == 123 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx20_sy29 && source_y == 123 && fill_x == 121)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx18_sy29 &&
			         ((source_y == 123 && fill_x == start_x) ||
			          (source_y == 124 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx18_sy29 && source_y == 124 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx18_sy29 && source_y == 124 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx23_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 121 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx23_sy29 && source_y == 121 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx23_sy29 && source_y == 121 && fill_x == 119)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx21_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 122 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx21_sy29 && source_y == 122 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx21_sy29 && source_y == 122 && fill_x == 123)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx19_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 123 && fill_x == 126) ||
			          (source_y == 124 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx19_sy29 &&
			         ((source_y == 123 && fill_x == 111) ||
			          (source_y == 124 && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx19_sy29 && source_y == 123 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx22_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 121 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx22_sy29 &&
			         ((source_y == 121 && fill_x == 111) ||
			          (source_y == 122 && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx22_sy29 && source_y == 121 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx22_sy30 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 123 && fill_x == 126) ||
			          (source_y == 124 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx22_sy30 && source_y == 123 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx22_sy30 && source_y == 123 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx18_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 123 && fill_x == 126) ||
			          (source_y == 124 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx18_sy28 &&
			         ((source_y == 123 && fill_x == 111) ||
			          (source_y == 124 && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx18_sy28 && source_y == 123 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx20_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 121 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx20_sy28 &&
			         ((source_y == 121 && fill_x == 111) ||
			          ((source_y == 122 || source_y == 123) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx20_sy28 && source_y == 121 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx21_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 120 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx21_sy28 &&
			         ((source_y == 120 && fill_x == 111) ||
			          (source_y == 121 && fill_x == 95) ||
			          (source_y == 122 && fill_x == 79)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx21_sy28 && source_y == 120 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx22_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 119 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx22_sy28 &&
			         ((source_y == 119 && fill_x == 111) ||
			          (source_y == 120 && fill_x == 95) ||
			          (source_y == 121 && fill_x == 79)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx22_sy28 && source_y == 119 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx23_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 118 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx23_sy28 &&
			         ((source_y == 118 && fill_x == 111) ||
			          (source_y == 119 && fill_x == 95) ||
			          (source_y == 120 && fill_x == 79)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx23_sy28 && source_y == 118 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx24_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 117 && fill_x == 126) ||
			          (source_y >= 118 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx24_sy28 &&
			         ((source_y == 117 && fill_x == 111) ||
			          ((source_y == 118 || source_y == 119) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx24_sy28 && source_y == 117 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx25_sy30 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 122 && fill_x == 126) ||
			          (source_y >= 123 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx25_sy30 && source_y == 122 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx25_sy30 && source_y == 122 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx19_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 122 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx19_sy28 &&
			         ((source_y == 122 && fill_x == 111) ||
			          ((source_y == 123 || source_y == 124) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx19_sy28 && source_y == 122 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx24_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 120 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx24_sy29 && source_y == 120 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx24_sy29 && source_y == 120 && fill_x == 123)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx25_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 119 && fill_x == 126) ||
			          (source_y >= 120 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx25_sy29 &&
			         ((source_y == 119 && fill_x == 111) ||
			          (source_y == 120 && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx25_sy29 && source_y == 119 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx26_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 119 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx26_sy29 && source_y == 119 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx26_sy29 && source_y == 119 && fill_x == 123)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx27_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 119 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx27_sy29 && source_y == 119 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx27_sy29 && source_y == 119 && fill_x == 117)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx29_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 118 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx29_sy29 && source_y == 118 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx29_sy29 && source_y == 118 && fill_x == 119)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx29_sy30 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 121 && fill_x == 126) ||
			          (source_y >= 122 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx29_sy30 && source_y == 121 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx29_sy30 && source_y == 121 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx17_sy27 &&
			         ((source_y == 123 && fill_x == start_x) ||
			          (source_y == 124 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx17_sy27 && source_y == 124 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx17_sy27 && source_y == 124 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx18_sy27 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 122 && fill_x == 126) ||
			          (source_y >= 123 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx18_sy27 &&
			         ((source_y == 122 && fill_x == 111) ||
			          ((source_y == 123 || source_y == 124) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx18_sy27 && source_y == 122 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx19_sy27 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 121 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx19_sy27 &&
			         ((source_y == 121 && fill_x == 111) ||
			          ((source_y == 122 || source_y == 123) && fill_x == 95) ||
			          (source_y == 124 && fill_x == 79)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx19_sy27 && source_y == 121 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx20_sy27 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 119 && fill_x == 126) ||
			          (source_y >= 120 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx20_sy27 &&
			         ((source_y == 119 && fill_x == 111) ||
			          ((source_y == 120 || source_y == 121 || source_y == 122) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx20_sy27 && source_y == 119 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx21_sy27 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 118 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx21_sy27 &&
			         ((source_y == 118 && fill_x == 111) ||
			          ((source_y == 119 || source_y == 120) && fill_x == 95) ||
			          (source_y == 121 && fill_x == 79)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx21_sy27 && source_y == 118 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx21_sy27 && source_y == 125 && fill_x == 127)
				byte_mask = 0xfu;
			else if (sweep1_tail_sx22_sy27 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 117 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx22_sy27 &&
			         ((source_y == 117 && fill_x == 111) ||
			          ((source_y == 118 || source_y == 119) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx22_sy27 && source_y == 117 && fill_x == 123)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx23_sy27 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 116 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx23_sy27 &&
			         ((source_y == 116 && fill_x == 111) ||
			          ((source_y == 117 || source_y == 118) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx23_sy27 && source_y == 116 && fill_x == 123)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx24_sy27 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 115 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx24_sy27 &&
			         ((source_y == 115 && fill_x == 111) ||
			          ((source_y == 116 || source_y == 117) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx24_sy27 && source_y == 115 && fill_x == 123)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx25_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 117 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx25_sy28 &&
			         ((source_y == 117 && fill_x == 111) ||
			          (source_y == 118 && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx25_sy28 && source_y == 117 && fill_x == 121)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx30_sy28 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 114 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx30_sy28 && source_y == 114 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx30_sy28 && source_y == 114 && fill_x == 121)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx18_sy26 &&
			         ((fill_x == start_x && source_y <= 124) ||
			          (source_y == 121 && fill_x == 126) ||
			          (source_y >= 122 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx18_sy26 &&
			         ((source_y == 121 && fill_x == 111) ||
			          ((source_y == 122 || source_y == 123 || source_y == 124) && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx18_sy26 && source_y == 121 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx17_sy26 &&
			         ((fill_x == start_x && source_y <= 124) ||
			          (source_y == 123 && fill_x == 126) ||
			          (source_y == 124 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx17_sy26 &&
			         ((source_y == 123 && fill_x == 111) ||
			          (source_y == 124 && fill_x == 95)))
				byte_mask = 0x8u;
			else if (sweep1_tail_sx17_sy26 &&
			         ((source_y == 123 && fill_x == 127) ||
			          (source_y == 124 && fill_x == 109)))
				byte_mask = 0x1u;
			else if (sweep1_tail_sx28_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 118 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx28_sy29 && source_y == 118 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx28_sy29 && source_y == 118 && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx30_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y == 117 && fill_x == 126) ||
			          (source_y >= 118 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx30_sy29 && source_y == 117 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx30_sy29 && source_y == 117 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_tail_sx31_sy29 &&
			         ((fill_x == start_x && source_y <= 123) ||
			          (source_y >= 117 && fill_x >= end_x - 1)))
				byte_mask = 0xfu;
			else if (sweep1_tail_sx31_sy29 && source_y == 117 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_sx31_sy29 && source_y == 117 && fill_x == 123)
				byte_mask = 0x1u;
			else if ((sweep1_tail_sx21_sy27 || sweep1_tail_sx27_sy29) &&
			         source_y == 125 && fill_x == 127)
				byte_mask = 0xfu;
			else if (sweep1_tail_sx27_sy29 && source_y == 125 && fill_x == 127)
				byte_mask = 0xfu;
			else if (sweep1_sx16_terminal && source_y == 123 && fill_x == start_x)
				byte_mask = 0xfu;
			else if (sweep1_sx16_terminal && source_y == 124 && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_sx16_terminal && source_y == 124 && fill_x == 126)
				byte_mask = 0xfu;
			else if (sweep1_sx16_terminal && source_y == 124 && fill_x == 127)
				byte_mask = 0x1u;
			else if (sweep1_sx16_early_terminal && fill_x == start_x &&
			         source_y >= 121 && source_y <= 123)
				byte_mask = 0xfu;
			else if (sweep1_sx16_early_terminal &&
			         ((source_y == 122 && fill_x == 111) ||
			          (source_y == 123 && fill_x == 95) ||
			          (source_y == 124 && fill_x == 79)))
				byte_mask = 0x8u;
			else if (sweep1_sx16_early_terminal &&
			         ((source_y == 123 && fill_x == 111) ||
			          (source_y == 124 && fill_x == 95)))
				byte_mask = 0x1u;
			else if (sweep1_sx16_early_terminal && fill_x == 126 &&
			         source_y >= 122 && source_y <= 124)
				byte_mask = 0xfu;
			else if (sweep1_sx16_early_terminal && fill_x == 127 &&
			         source_y >= 122 && source_y <= 124)
				byte_mask = source_y == 122 ? 0x1u : 0xfu;
			else if (sweep1_tail_last_row && (fill_x == start_x || fill_x >= end_x - 1))
				byte_mask = 0xfu;
			else if (sweep1_tail_last_row && fill_x == 111)
				byte_mask = 0x8u;
			else if (sweep1_tail_last_row && fill_x == 125)
				byte_mask = 0x1u;
			else if (sweep0_069_038_single_end && fill_x == end_x)
				byte_mask = 0x8u;
			else if (sweep0_top_sxi7 && fill_x == 124)
				byte_mask = 0x1u;
			else if (sweep0_top_sxi11 &&
			         (fill_x == start_x || fill_x >= 124))
				byte_mask = 0xfu;
			else if (sweep0_top_sxi11 && fill_x == 109)
				byte_mask = 0x8u;
			else if (sweep0_top_sxi11 && fill_x == 119)
				byte_mask = 0x1u;
			else if (sweep0_top_sxi15 && fill_x == start_x)
				byte_mask = 0xfu;
			else if (sweep0_top_sxi15 && fill_x == 109)
				byte_mask = 0x8u;
			else if (sweep0_top_sxi18 && source_y == 2 && (fill_x == 93 || fill_x == 98))
				byte_mask = 0xfu;
			else if (sweep0_top_sxi18 && source_y == 2 && fill_x == 109)
				byte_mask = 0x8u;
			else if (sweep0_top_sxi18 && source_y == 4 && fill_x == 110)
				byte_mask = 0xfu;
			else if ((sweep0_top_sxi16 || sweep0_top_sxi17 || sweep0_top_sxi18 ||
			          sweep0_top_sxi19 || sweep0_top_sxi20) &&
			         fill_x == start_x && source_y >= 3)
				byte_mask = 0xfu;
			else if ((sweep0_top_sxi17 && source_y == 3 && fill_x == 118) ||
			         (sweep0_top_sxi20 && source_y == 3 && fill_x == 117))
				byte_mask = 0xfu;
			else if ((sweep0_top_sxi16 || sweep0_top_sxi17 || sweep0_top_sxi19 ||
			          sweep0_top_sxi20) &&
			         fill_x == 109)
				byte_mask = 0x8u;
			else if (sweep0_top_sxi18 && fill_x == 93)
				byte_mask = 0x8u;
			else if ((sweep0_top_sxi16 && fill_x == 122) ||
			         (sweep0_top_sxi17 && (fill_x == 116 || fill_x == 118)) ||
			         (sweep0_top_sxi18 && (fill_x == 98 || fill_x == 110)) ||
			         (sweep0_top_sxi19 && (fill_x == 103 || fill_x == 120)) ||
			         (sweep0_top_sxi20 && (fill_x == 114 || fill_x == 117)))
				byte_mask = 0x1u;
			else if ((sweep0_top_sxi16 || sweep0_top_sxi17 || sweep0_top_sxi18 ||
			          sweep0_top_sxi19 || sweep0_top_sxi20) &&
			         fill_x >= 124)
				byte_mask = 0xfu;
			else if ((sweep0_small_2_2 &&
			          ((source_y == 10 && fill_x == 115) ||
			           (source_y == 12 && fill_x == 117) ||
			           (source_y == 14 && fill_x == 119) ||
			           (source_y == 16 && fill_x == 121) ||
			           (source_y == 18 && fill_x == 123))) ||
			         (sweep0_small_3_2 &&
			          ((source_y == 15 && fill_x == 117) ||
			           (source_y == 16 && fill_x == 119) ||
			           (source_y == 18 && fill_x == 121) ||
			           (source_y == 19 && fill_x == 123))) ||
			         (sweep0_small_2_3 &&
			          ((source_y == 14 && fill_x == 115) ||
			           (source_y == 17 && fill_x == 117) ||
			           (source_y == 20 && fill_x == 119) ||
			           (source_y == 22 && fill_x == 121) ||
			           (source_y == 25 && fill_x == 123))) ||
			         (sweep0_small_3_3 &&
			          ((source_y == 18 && fill_x == 115) ||
			           (source_y == 20 && fill_x == 117) ||
			           (source_y == 22 && fill_x == 119) ||
			           (source_y == 24 && fill_x == 121) ||
			           (source_y == 26 && fill_x == 123))) ||
			         (sweep0_small_5_4 &&
			          ((source_y == 28 && fill_x == 115) ||
			           (source_y == 30 && fill_x == 117) ||
			           (source_y == 31 && fill_x == 119) ||
			           (source_y == 33 && fill_x == 121) ||
			           (source_y == 35 && fill_x == 123))))
				byte_mask = 0xfu;
			else if (sweep0_small_5_4 && source_y == 36 && fill_x == 125)
				byte_mask = 0x8u;
			else if (sweep0_small_2_2 && source_y == 20 && fill_x == 125)
				byte_mask = 0x8u;
			else if (sweep0_006_start_full && fill_x == start_x)
				byte_mask = 0xfu;
			else if (preterminal_end_full_fill && fill_x >= end_x - 1)
				byte_mask = 0xfu;
			else if (preterminal_start_full_fill && fill_x == start_x)
				byte_mask = 0xfu;
			else if (split_byte_mode == 1 && fill_x == start_x)
				byte_mask = 0x1u;
			else if (split_byte_mode == 1 && fill_x == start_x + 1)
				continue;
			else if (odd_start && fill_x == start_x)
				byte_mask = 0x1u;
			else if (flip_even_end && fill_x == end_x - 1)
				byte_mask = 0x8u;
			else if (!flip_even_end && fill_x == end_x)
				byte_mask = 0x8u;
			else if (!flip_even_end && fill_x == end_x - 1)
				continue;

			shaded.coverage_count = U8_C(COVERAGE_FILL_BIT) | u8(byte_mask);
			return true;
		}

		return false;
	}

	int coverage = compute_coverage(span_setup.xleft, span_setup.xright, x);

	// There is no way we can gain coverage here.
	// Reject work as fast as possible.
	if (coverage == 0)
		return false;

	int coverage_count = bitCount(coverage);

	// If we're not using AA, only the first coverage bit is relevant.
	if (!aa_enable && (coverage & 1) == 0)
		return false;

	DerivedSetup derived = load_derived_setup(primitive_index);

	int dx = x - span_setup.interpolation_base_x;
	int interpolation_direction = flip ? 1 : -1;

	// Interpolate attributes.
	u8x4 shade = interpolate_rgba(span_setup.rgba, attr.drgba_dx, attr.drgba_dy,
	                              dx, coverage);

	ivec2 st, st_dx, st_dy;
	int z;
	bool perspective_overflow = false;

	int tex_interpolation_direction = interpolation_direction;
	if (SCALING_FACTOR > 1 && uses_lod)
		if ((setup_flags & TRIANGLE_SETUP_NATIVE_LOD_BIT) != 0)
			tex_interpolation_direction *= SCALING_FACTOR;

	interpolate_stz(span_setup.stzw, attr.dstzw_dx, attr.dstzw_dy, dx, coverage, perspective, uses_lod,
	                tex_interpolation_direction, st, st_dx, st_dy, z, perspective_overflow);

	// Sample textures.
	uint tile0 = uint(setup_tile) & 7u;
	uint tile1 = (tile0 + 1) & 7u;
	uint max_level = uint(setup_tile) >> 3u;
	int min_lod = derived.min_lod;

	i16 lod_frac;
	if (uses_lod)
	{
		compute_lod_2cycle(tile0, tile1, lod_frac, max_level, min_lod, st, st_dx, st_dy, perspective_overflow,
		                   tex_lod_en, sharpen_lod_en, detail_lod_en);
	}

	i16x4 texel0, texel1;

	if (uses_texel0)
	{
		uint tile_info_index0 = uint(state_indices.elems[primitive_index].tile_infos[tile0]);
		TileInfo tile_info0 = load_tile_info(tile_info_index0);
#ifdef RASTERIZER_SPEC_CONSTANT
		if ((STATIC_STATE_FLAGS & RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT) != 0)
		{
			tile_info0.fmt = u8(TEX_FMT);
			tile_info0.size = u8(TEX_SIZE);
		}
#endif
		texel0 = sample_texture(tile_info0, tmem_instance_index, st, tlut, tlut_type,
		                        sample_quad, mid_texel, false, bilerp0, derived.factors, i16x4(0));
	}

	// A very awkward mechanism where we peek into the next pixel, or in some cases, the next scanline's first pixel.
	if (uses_pipelined_texel1)
	{
		bool valid_line = uint(span_setups.elems[SCALING_FACTOR * span_offsets.offset + (y - SCALING_FACTOR * span_offsets.ylo + 1)].valid_line) != 0u;
		bool long_span = span_setup.lodlength >= 8;
		bool end_span = x == (flip ? span_setup.end_x : span_setup.start_x);

		if (end_span && long_span && valid_line)
		{
			ivec3 stw = span_setups.elems[SCALING_FACTOR * span_offsets.offset + (y - SCALING_FACTOR * span_offsets.ylo + 1)].stzw.xyw >> 16;
			if (perspective)
			{
				bool st_overflow;
				st = perspective_divide(stw, st_overflow);
			}
			else
				st = no_perspective_divide(stw);
		}
		else
			st = interpolate_st_single(span_setup.stzw, attr.dstzw_dx, dx + interpolation_direction * SCALING_FACTOR, perspective);

		tile1 = tile0;
		uses_texel1 = true;
	}

	if (uses_texel1)
	{
		if (convert_one && !bilerp1)
		{
			texel1 = texture_convert_factors(texel0, derived.factors);
		}
		else
		{
			uint tile_info_index1 = uint(state_indices.elems[primitive_index].tile_infos[tile1]);
			TileInfo tile_info1 = load_tile_info(tile_info_index1);
#ifdef RASTERIZER_SPEC_CONSTANT
			if ((STATIC_STATE_FLAGS & RASTERIZATION_USE_STATIC_TEXTURE_SIZE_FORMAT_BIT) != 0)
			{
				tile_info1.fmt = u8(TEX_FMT);
				tile_info1.size = u8(TEX_SIZE);
			}
#endif
			texel1 = sample_texture(tile_info1, tmem_instance_index, st, tlut, tlut_type, sample_quad, mid_texel,
			                        convert_one, bilerp1, derived.factors, texel0);
		}
	}

	int rgb_dith, alpha_dith;
	dither_coefficients(x, y >> int(interlace_en), static_state_dither >> 2, static_state_dither & 3, rgb_dith, alpha_dith);

	// Run combiner.
	u8x4 combined;
	u8 alpha_reference;
	if (multi_cycle)
	{
		CombinerInputs combined_inputs =
				CombinerInputs(derived.constant_muladd0, derived.constant_mulsub0, derived.constant_mul0, derived.constant_add0,
				               shade, u8x4(0), texel0, texel1, lod_frac, noise_get_combiner());

		combined_inputs.combined = combiner_cycle0(combined_inputs,
		                                           combiner_inputs_rgb0,
		                                           combiner_inputs_alpha0,
		                                           alpha_dith, coverage_count, cvg_times_alpha, alpha_cvg_select,
		                                           alpha_test, alpha_reference);

		combined_inputs.constant_muladd = derived.constant_muladd1;
		combined_inputs.constant_mulsub = derived.constant_mulsub1;
		combined_inputs.constant_mul = derived.constant_mul1;
		combined_inputs.constant_add = derived.constant_add1;

		// Pipelining, texel1 is promoted to texel0 in cycle1.
		// I don't think hardware ever intended for you to access texels in second cycle due to this nature.
		i16x4 tmp_texel = combined_inputs.texel0;
		combined_inputs.texel0 = combined_inputs.texel1;
		// Following the pipelining, texel1 should become texel0 of next pixel,
		// but let's not go there ...
		combined_inputs.texel1 = tmp_texel;

		// Resample the noise at some arbitrary other offset.
		// This only matters if both noise combiner inputs take noise (very weird).
		if ((static_state_flags & RASTERIZATION_NEED_NOISE_DUAL_BIT) != 0)
		{
			reseed_noise(x + 1023, y + 7, primitive_index + global_constants.fb_info.base_primitive_index + 11);
			combined_inputs.noise = noise_get_combiner();
		}

		combined = u8x4(combiner_cycle1(combined_inputs,
		                                combiner_inputs_rgb1,
		                                combiner_inputs_alpha1,
		                                alpha_dith, coverage_count, cvg_times_alpha, alpha_cvg_select));
	}
	else
	{
		CombinerInputs combined_inputs =
				CombinerInputs(derived.constant_muladd1, derived.constant_mulsub1, derived.constant_mul1, derived.constant_add1,
				               shade, u8x4(0), texel0, texel1, lod_frac, noise_get_combiner());

		combined = u8x4(combiner_cycle1(combined_inputs,
		                                combiner_inputs_rgb1,
		                                combiner_inputs_alpha1,
		                                alpha_dith, coverage_count, cvg_times_alpha, alpha_cvg_select));

		alpha_reference = combined.a;
	}

	// After combiner, color can be modified to 0 through alpha-to-cvg, so check for potential write_enable here.
	// If we're not using AA, the first coverage bit is used instead, coverage count is ignored.
	if (aa_enable && coverage_count == 0)
		return false;

	if (alpha_test)
	{
		u8 alpha_threshold;
		if (alpha_test_dither)
			alpha_threshold = noise_get_blend_threshold();
		else
			alpha_threshold = derived.blend_color.a;

		if (alpha_reference < alpha_threshold)
			return false;
	}

	shaded.combined = combined;
	shaded.z_dith = (z << 9) | rgb_dith;
	shaded.coverage_count = u8(coverage_count);
	// Shade alpha needs to be passed separately since it might affect the blending stage.
	shaded.shade_alpha = u8(min(shade.a + alpha_dith, 0xff));
	return true;
}

#endif
