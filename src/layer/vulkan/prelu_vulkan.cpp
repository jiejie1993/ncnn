// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "prelu_vulkan.h"
#include <algorithm>

namespace ncnn {

DEFINE_LAYER_CREATOR(PReLU_vulkan)

PReLU_vulkan::PReLU_vulkan()
{
    support_vulkan = true;

    pipeline_prelu = 0;
    pipeline_prelu_pack4 = 0;
    pipeline_prelu_pack8 = 0;
}

int PReLU_vulkan::create_pipeline(const Option& opt)
{
    const Mat& shape = top_shapes.empty() ? Mat() : top_shapes[0];

    int elempack = 1;
    if (shape.dims == 0) elempack = opt.use_shader_pack8 && num_slope % 8 == 0 ? 8 : num_slope % 4 == 0 ? 4 : 1;
    if (shape.dims == 1) elempack = opt.use_shader_pack8 && shape.w % 8 == 0 ? 8 : shape.w % 4 == 0 ? 4 : 1;
    if (shape.dims == 2) elempack = opt.use_shader_pack8 && shape.h % 8 == 0 ? 8 : shape.h % 4 == 0 ? 4 : 1;
    if (shape.dims == 3) elempack = opt.use_shader_pack8 && shape.c % 8 == 0 ? 8 : shape.c % 4 == 0 ? 4 : 1;

    Mat shape_packed;
    convert_shape_packing(shape, shape_packed, elempack);

    std::vector<vk_specialization_type> specializations(1 + 5);
    specializations[0].i = num_slope;
    specializations[1 + 0].i = shape_packed.dims;
    specializations[1 + 1].i = shape_packed.w;
    specializations[1 + 2].i = shape_packed.h;
    specializations[1 + 3].i = shape_packed.c;
    specializations[1 + 4].i = shape_packed.cstep;

    Mat local_size_xyz(4, 4, std::min(4, num_slope / elempack), (void*)0);
    if (shape_packed.dims == 1)
    {
        local_size_xyz.w = std::min(64, shape_packed.w);
        local_size_xyz.h = 1;
        local_size_xyz.c = 1;
    }
    if (shape_packed.dims == 2)
    {
        local_size_xyz.w = std::min(8, shape_packed.w);
        local_size_xyz.h = std::min(8, shape_packed.h);
        local_size_xyz.c = 1;
    }
    if (shape_packed.dims == 3)
    {
        local_size_xyz.w = std::min(4, shape_packed.w);
        local_size_xyz.h = std::min(4, shape_packed.h);
        local_size_xyz.c = std::min(4, shape_packed.c);
    }

    // pack1
    if (num_slope == 1 || elempack == 1)
    {
        pipeline_prelu = new Pipeline(vkdev);
        pipeline_prelu->set_optimal_local_size_xyz(local_size_xyz);
        pipeline_prelu->create("prelu", opt, specializations, 2, 5);
    }

    // pack4
    if (num_slope == 1 || elempack == 4)
    {
        pipeline_prelu_pack4 = new Pipeline(vkdev);
        pipeline_prelu_pack4->set_optimal_local_size_xyz(local_size_xyz);
        pipeline_prelu_pack4->create("prelu_pack4", opt, specializations, 2, 5);
    }

    // pack8
    if (num_slope == 1 || elempack == 8)
    {
        pipeline_prelu_pack8 = new Pipeline(vkdev);
        pipeline_prelu_pack8->set_optimal_local_size_xyz(local_size_xyz);
        pipeline_prelu_pack8->create("prelu_pack8", opt, specializations, 2, 5);
    }

    return 0;
}

int PReLU_vulkan::destroy_pipeline(const Option& /*opt*/)
{
    delete pipeline_prelu;
    pipeline_prelu = 0;

    delete pipeline_prelu_pack4;
    pipeline_prelu_pack4 = 0;

    delete pipeline_prelu_pack8;
    pipeline_prelu_pack8 = 0;

    return 0;
}

int PReLU_vulkan::upload_model(VkTransfer& cmd, const Option& opt)
{
    if (num_slope == 1)
    {
        // dup4 for pack4
        Mat slope_data4(4);
        slope_data4.fill(slope_data[0]);
        cmd.record_upload(slope_data4, slope_data_gpu, opt);
    }
    else
    {
        cmd.record_upload(slope_data, slope_data_gpu, opt);
    }

    return 0;
}

int PReLU_vulkan::forward_inplace(VkMat& bottom_top_blob, VkCompute& cmd, const Option& /*opt*/) const
{
    int elempack = bottom_top_blob.elempack;

    std::vector<VkMat> bindings(2);
    bindings[0] = bottom_top_blob;
    bindings[1] = slope_data_gpu;

    std::vector<vk_constant_type> constants(5);
    constants[0].i = bottom_top_blob.dims;
    constants[1].i = bottom_top_blob.w;
    constants[2].i = bottom_top_blob.h;
    constants[3].i = bottom_top_blob.c;
    constants[4].i = bottom_top_blob.cstep;

    const Pipeline* pipeline = elempack == 8 ? pipeline_prelu_pack8
                             : elempack == 4 ? pipeline_prelu_pack4
                             : pipeline_prelu;

    cmd.record_pipeline(pipeline, bindings, constants, bottom_top_blob);

    return 0;
}

} // namespace ncnn
