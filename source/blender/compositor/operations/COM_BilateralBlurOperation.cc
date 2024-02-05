/* SPDX-FileCopyrightText: 2011 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "COM_BilateralBlurOperation.h"

namespace blender::compositor {

BilateralBlurOperation::BilateralBlurOperation()
{
  this->add_input_socket(DataType::Color);
  this->add_input_socket(DataType::Color);
  this->add_output_socket(DataType::Color);
  flags_.complex = true;
  flags_.can_be_constant = true;

  input_color_program_ = nullptr;
  input_determinator_program_ = nullptr;
}

void BilateralBlurOperation::init_execution()
{
  input_color_program_ = get_input_socket_reader(0);
  input_determinator_program_ = get_input_socket_reader(1);
  QualityStepHelper::init_execution(COM_QH_INCREASE);
}

void BilateralBlurOperation::execute_pixel(float output[4], int x, int y, void *data)
{
  /* Read the determinator color at x, y,
   * this will be used as the reference color for the determinator. */
  float determinator_reference_color[4];
  float determinator[4];
  float temp_color[4];
  float blur_color[4];
  float blur_divider;
  float sigmacolor = data_->sigma_color;
  float delta_color;
  input_determinator_program_->read(determinator_reference_color, x, y, data);

  zero_v4(blur_color);
  blur_divider = 0.0f;
  /* TODO(sergey): This isn't really good bilateral filter, it should be
   * using gaussian bell for weights. Also sigma_color doesn't seem to be
   * used correct at all.
   */
  for (int yi = -radius_; yi <= radius_; yi += QualityStepHelper::get_step()) {
    for (int xi = -radius_; xi <= radius_; xi += QualityStepHelper::get_step()) {
      /* Read determinator. */
      input_determinator_program_->read_clamped(determinator, x + xi, y + yi, data);
      delta_color = (fabsf(determinator_reference_color[0] - determinator[0]) +
                     fabsf(determinator_reference_color[1] - determinator[1]) +
                     /* Do not take the alpha channel into account. */
                     fabsf(determinator_reference_color[2] - determinator[2]));
      if (delta_color < sigmacolor) {
        /* Add this to the blur. */
        input_color_program_->read_clamped(temp_color, x + xi, y + yi, data);
        add_v4_v4(blur_color, temp_color);
        blur_divider += 1.0f;
      }
    }
  }

  if (blur_divider > 0.0f) {
    mul_v4_v4fl(output, blur_color, 1.0f / blur_divider);
  }
  else {
    output[0] = 0.0f;
    output[1] = 0.0f;
    output[2] = 0.0f;
    output[3] = 1.0f;
  }
}

void BilateralBlurOperation::deinit_execution()
{
  input_color_program_ = nullptr;
  input_determinator_program_ = nullptr;
}

bool BilateralBlurOperation::determine_depending_area_of_interest(
    rcti *input, ReadBufferOperation *read_operation, rcti *output)
{
  rcti new_input;
  int add = radius_ + 1;

  new_input.xmax = input->xmax + (add);
  new_input.xmin = input->xmin - (add);
  new_input.ymax = input->ymax + (add);
  new_input.ymin = input->ymin - (add);

  return NodeOperation::determine_depending_area_of_interest(&new_input, read_operation, output);
}

void BilateralBlurOperation::get_area_of_interest(const int /*input_idx*/,
                                                  const rcti &output_area,
                                                  rcti &r_input_area)
{
  const int add = radius_ + 1;

  r_input_area.xmax = output_area.xmax + (add);
  r_input_area.xmin = output_area.xmin - (add);
  r_input_area.ymax = output_area.ymax + (add);
  r_input_area.ymin = output_area.ymin - (add);
}

struct PixelCursor {
  MemoryBuffer *input_determinator;
  MemoryBuffer *input_color;
  int step;
  float sigma_color;
  const float *determ_reference_color;
  float temp_color[4];
  float *out;
  int radius;
  int x;
  int y;
};

static void blur_pixel(PixelCursor &p)
{
  float blur_divider = 0.0f;
  zero_v4(p.out);

  /* TODO(sergey): This isn't really good bilateral filter, it should be
   * using gaussian bell for weights. Also sigma_color doesn't seem to be
   * used correct at all.
   */
  for (int yi = -p.radius; yi <= p.radius; yi += p.step) {
    for (int xi = -p.radius; xi <= p.radius; xi += p.step) {
      p.input_determinator->read_elem_clamped(p.x + xi, p.y + yi, p.temp_color);
      /* Do not take the alpha channel into account. */
      const float delta_color = (fabsf(p.determ_reference_color[0] - p.temp_color[0]) +
                                 fabsf(p.determ_reference_color[1] - p.temp_color[1]) +
                                 fabsf(p.determ_reference_color[2] - p.temp_color[2]));
      if (delta_color < p.sigma_color) {
        /* Add this to the blur. */
        p.input_color->read_elem_clamped(p.x + xi, p.y + yi, p.temp_color);
        add_v4_v4(p.out, p.temp_color);
        blur_divider += 1.0f;
      }
    }
  }

  if (blur_divider > 0.0f) {
    mul_v4_fl(p.out, 1.0f / blur_divider);
  }
  else {
    copy_v4_v4(p.out, COM_COLOR_BLACK);
  }
}

void BilateralBlurOperation::update_memory_buffer_partial(MemoryBuffer *output,
                                                          const rcti &area,
                                                          Span<MemoryBuffer *> inputs)
{
  PixelCursor p = {};
  p.step = QualityStepHelper::get_step();
  p.sigma_color = data_->sigma_color;
  p.radius = radius_;
  p.input_color = inputs[0];
  p.input_determinator = inputs[1];
  for (int y = area.ymin; y < area.ymax; y++) {
    p.out = output->get_elem(area.xmin, y);
    p.y = y;
    for (int x = area.xmin; x < area.xmax; x++) {
      p.x = x;
      /* This will be used as the reference color for the determinator. */
      p.determ_reference_color = p.input_determinator->get_elem(x, y);
      blur_pixel(p);
      p.out += output->elem_stride;
    }
  }
}

}  // namespace blender::compositor
