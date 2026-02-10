//
// SPDX-License-Identifier: Apache-2.0
//

// Copyright 2022 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/// @file constant_strings.h
///
/// File holding shared, constant definitions of AtString instances.
///
/// Defining EXPAND_ARNOLD_USD_STRINGS before including constant_strings.h will not only
/// declare but also define the AtString instances.
#pragma once

#include <ai_string.h>

#include <pxr/base/tf/token.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace str {

#ifdef EXPAND_ARNOLD_USD_STRINGS
#define ASTR(x)                 \
    extern const AtString x;    \
    const AtString x(#x);       \
    extern const TfToken t_##x; \
    const TfToken t_##x(#x)
#define ASTR2(x, y)             \
    extern const AtString x;    \
    const AtString x(y);        \
    extern const TfToken t_##x; \
    const TfToken t_##x(y)
#else
#define ASTR(x)              \
    extern const AtString x; \
    extern const TfToken t_##x
#define ASTR2(x, y)          \
    extern const AtString x; \
    extern const TfToken t_##x
#endif

ASTR2(_default, "default");
ASTR2(_max, "max");
ASTR2(_min, "min");
ASTR2(_auto, "auto");
ASTR2(arnold__attributes, "arnold::attributes");
ASTR2(arnold_camera, "arnold:camera");
ASTR2(arnold_color_space, "arnold:color_space");
ASTR2(arnold_filename, "arnold:filename");
ASTR2(arnold_image, "arnold:image");
ASTR2(arnold_light, "arnold:light");
ASTR2(arnold_node_entry, "arnold:node_entry");
ASTR2(arnold_prefix, "arnold:");
ASTR2(arnold_relative_path, "arnold_relative_path");
ASTR2(autobump_visibility_prefix, "autobump_visibility:");
ASTR2(b_spline, "b-spline");
ASTR2(catmull_rom, "catmull-rom");
ASTR2(constantArray, "constant ARRAY");
ASTR2(constantArrayFloat, "CONSTANT ARRAY FLOAT");
ASTR2(constantInt, "constant INT");
ASTR2(constantString, "constant STRING");
ASTR2(deformKeys, "arnold:deform_keys");
ASTR2(houdiniFps, "houdini:fps");
ASTR2(houdiniFrame, "houdini:frame");
ASTR2(houdiniCopTextureChanged, "houdini:cop_texture_changed");
ASTR2(hydraPrimId, "hydra_primId");
ASTR2(inputs_code, "inputs:code");
ASTR2(inputs_file, "inputs:file");
ASTR2(inputs_intensity, "inputs:intensity");
ASTR2(primvars_arnold, "primvars:arnold");
ASTR2(primvars_arnold_light, "primvars:arnold:light");
ASTR2(log_file, "log:file");
ASTR2(log_verbosity, "log:verbosity");
ASTR2(profile_file, "profile:file");
ASTR2(report_file, "report:file");
ASTR2(stats_file, "stats:file");
ASTR2(stats_mode, "stats:mode");
ASTR2(outputs_color, "outputs:color");
ASTR2(outputs_out, "outputs:out");
ASTR2(_operator, "operator");
ASTR2(primvars_arnold_camera, "primvars:arnold:camera");
ASTR2(primvars_arnold_light_shaders, "primvars:arnold:light:shaders");
ASTR2(primvars_arnold_shaders, "primvars:arnold:shaders");
ASTR2(primvars_arnold_smoothing, "primvars:arnold:smoothing");
ASTR2(primvars_arnold_subdiv_iterations, "primvars:arnold:subdiv_iterations");
ASTR2(primvars_arnold_subdiv_type, "primvars:arnold:subdiv_type");
ASTR2(primvars_arnold_name, "primvars:arnold:name");
ASTR2(primvars_arnold_normalize, "primvars:arnold:normalize");
ASTR2(renderPassAOVDriver, "HdArnoldRenderPass_aov_driver");
ASTR2(renderPassCamera, "HdArnoldRenderPass_camera");
ASTR2(renderPassClosestFilter, "HdArnoldRenderPass_closestFilter");
ASTR2(renderPassFilter, "HdArnoldRenderPass_beautyFilter");
ASTR2(renderPassMainDriver, "HdArnoldRenderPass_main_driver");
ASTR2(renderPassPrimIdReader, "HdArnoldRenderPass_prim_id_reader");
ASTR2(renderPassPrimIdWriter, "HdArnoldRenderPass_prim_id_writer");
ASTR2(render_context, "RENDER_CONTEXT");
ASTR2(sidedness_prefix, "sidedness:");
ASTR2(transformKeys, "arnold:transform_keys");
ASTR2(ui_groups, "ui.groups");
ASTR2(usd_hide, "usd.hide");
ASTR2(usdlux_setting, "arnold:global:usdlux_version");
ASTR2(visibility_prefix, "visibility:");
ASTR2(visibilityCamera, "arnold:visibility:camera");

ASTR(AA_sample_clamp);
ASTR(AA_sample_clamp_affects_aovs);
ASTR(AA_samples);
ASTR(AA_samples_max);
ASTR(AA_seed);
ASTR(ArnoldUsd);
ASTR(ArnoldNodeGraph);
ASTR(ArnoldOptions);
ASTR(ArnoldMarkPrimsDirty);
ASTR(binary);
ASTR(BOOL);
ASTR(BYTE);
ASTR(colorSpace);
ASTR(command_line);
ASTR(CPU);
ASTR(camera_projection);
ASTR(crypto_asset);
ASTR(crypto_material);
ASTR(crypto_object);
ASTR(custom_attributes);
ASTR(debug);
ASTR(desc);
ASTR(deprecated);
ASTR(driver_exr);
ASTR(FLOAT);
ASTR(GI_diffuse_depth);
ASTR(GI_diffuse_samples);
ASTR(GI_specular_depth);
ASTR(GI_specular_samples);
ASTR(GI_sss_samples);
ASTR(GI_total_depth);
ASTR(GI_transmission_depth);
ASTR(GI_transmission_samples);
ASTR(GI_volume_depth);
ASTR(GI_volume_samples);
ASTR(GPU);
ASTR(help);
ASTR(HdArnoldDriverAOV);
ASTR(HdArnoldDriverMain);
ASTR(INT);
ASTR(P);
ASTR(RGB);
ASTR(RGBA);
ASTR(STRING);
ASTR(UINT);
ASTR(UsdPreviewSurface);
ASTR(UsdPrimvarReader_);
ASTR(UsdPrimvarReader_float);
ASTR(UsdPrimvarReader_float2);
ASTR(UsdPrimvarReader_float3);
ASTR(UsdPrimvarReader_float4);
ASTR(UsdPrimvarReader_int);
ASTR(UsdPrimvarReader_normal);
ASTR(UsdPrimvarReader_point);
ASTR(UsdPrimvarReader_string);
ASTR(UsdPrimvarReader_vector);
ASTR(UsdTransform2d);
ASTR(UsdUVTexture);
ASTR(VECTOR);
ASTR(VECTOR2);
ASTR(abort_on_error);
ASTR(abort_on_license_fail);
ASTR(accelerations);
ASTR(ai_default_color_manager_ocio);
ASTR(all_attributes);
ASTR(ambocc);
ASTR(angle);
ASTR(angle_scale);
ASTR(angular);
ASTR(aov_input);
ASTR(aov_name);
ASTR(aov_pointer);
ASTR(aov_read_float);
ASTR(aov_read_int);
ASTR(aov_read_rgb);
ASTR(aov_read_rgba);
ASTR(aov_shaders);
ASTR(aov_write_float);
ASTR(aov_write_int);
ASTR(aov_write_rgb);
ASTR(aov_write_rgba);
ASTR(aov_write_vector);
ASTR(aperture_size);
ASTR(append);
ASTR(arnold);
ASTR(asset_searchpath);
ASTR(assignment);
ASTR(atmosphere);
ASTR(attribute);
ASTR(auto_transparency_depth);
ASTR(autobump_visibility);
ASTR(background);
ASTR(barndoor);
ASTR(barndoor_bottom_edge);
ASTR(barndoor_bottom_left);
ASTR(barndoor_bottom_right);
ASTR(barndoor_left_bottom);
ASTR(barndoor_left_edge);
ASTR(barndoor_left_top);
ASTR(barndoor_right_bottom);
ASTR(barndoor_right_edge);
ASTR(barndoor_right_top);
ASTR(barndoor_top_edge);
ASTR(barndoor_top_left);
ASTR(barndoor_top_right);
ASTR(base);
ASTR(base_color);
ASTR(basis);
ASTR(bezier);
ASTR(bias);
ASTR(black);
ASTR(blend_opacity);
ASTR(bottom);
ASTR(box_filter);
ASTR(bucket_scanning);
ASTR(bucket_size);
ASTR(buffer_names);
ASTR(buffer_pointers);
ASTR(cache_id);
ASTR(camera);
ASTR(cast_shadows);
ASTR(catclark);
ASTR(clamp);
ASTR(clearcoat);
ASTR(clearcoatRoughness);
ASTR(closest_filter);
ASTR(coat);
ASTR(coat_roughness);
ASTR(code);
ASTR(color);
ASTR(color_manager);
ASTR(color_manager_ocio);
ASTR(color_space);
ASTR(color_space_linear);
ASTR(color_space_narrow);
ASTR(color_mode);
ASTR(color_pointer);
ASTR(color_to_signed);
ASTR(cone_angle);
ASTR(config);
ASTR(constant);
ASTR(cosine_power);
ASTR(crease_idxs);
ASTR(crease_sharpness);
ASTR(curves);
ASTR(cylinder_light);
ASTR(dcc);
ASTR(defaultPrim);
ASTR(depth_half_precision);
ASTR(depth_pointer);
ASTR(depth_tolerance);
ASTR(diffuse);
ASTR(diffuseColor);
ASTR(diffuse_reflect);
ASTR(diffuse_transmit);
ASTR(disk_light);
ASTR(disp_map);
ASTR(displacement);
ASTR(displayColor);
ASTR(distant_light);
ASTR(drivers);
ASTR(driver_deepexr);
ASTR(emission);
ASTR(emission_color);
ASTR(emissiveColor);
ASTR(enable_adaptive_sampling);
ASTR(enable_dependency_graph);
ASTR(enable_dithered_sampling);
ASTR(enable_gpu_rendering);
ASTR(enable_new_point_light_sampler);
ASTR(enable_new_quad_light_sampler);
ASTR(enable_procedural_cache);
ASTR(enable_progressive_pattern);
ASTR(enable_progressive_render);
ASTR(exposure);
ASTR(fallback);
ASTR(far_clip);
ASTR(file);
ASTR(filename);
ASTR(filtermap);
ASTR(filter);
ASTR(filters);
ASTR(filterwidth);
ASTR(flat);
ASTR(focus_distance);
ASTR(format);
ASTR(fov);
ASTR(fps);
ASTR(frame);
ASTR(gaussian_filter);
ASTR(geometry);
ASTR(ginstance);
ASTR(grids);
ASTR(half_precision);
ASTR(hide);
ASTR(husk);
ASTR(hydra);
ASTR(id);
ASTR(id_pointer);
ASTR(ies_normalize);
ASTR(ignore_atmosphere);
ASTR(ignore_bump);
ASTR(ignore_displacement);
ASTR(ignore_dof);
ASTR(ignore_lights);
ASTR(ignore_missing_textures);
ASTR(ignore_motion);
ASTR(ignore_motion_blur);
ASTR(ignore_operators);
ASTR(ignore_shaders);
ASTR(ignore_shadows);
ASTR(ignore_smoothing);
ASTR(ignore_sss);
ASTR(ignore_subdivision);
ASTR(ignore_textures);
ASTR(image);
ASTR(imager);
ASTR(in);
ASTR(indexed);
ASTR(indirect_sample_clamp);
ASTR(inherit_xform);
ASTR(input);
ASTR(inputs);
ASTR(input1);
ASTR(input2);
ASTR(instance);
ASTR(instance_crypto_object_offset);
ASTR(instance_inherit_xform);
ASTR(instance_intensity);
ASTR(instance_matrix);
ASTR(instance_motion_end);
ASTR(instance_motion_start);
ASTR(instance_shader);
ASTR(instance_visibility);
ASTR(instancer);
ASTR(intensity);
ASTR(interactive);
ASTR(interactive_fps_min);
ASTR(interactive_target_fps);
ASTR(interactive_target_fps_min);
ASTR(ior);
ASTR(label);
ASTR(latlong);
ASTR(layer_enable_filtering);
ASTR(layer_half_precision);
ASTR(layer_name);
ASTR(layer_tolerance);
ASTR(light_filter);
ASTR(light_group);
ASTR(light_path_expressions);
ASTR(linear);
ASTR(linkable);
ASTR(log_flags_console);
ASTR(log_flags_file);
ASTR(mask);
ASTR(MATERIALX_NODE_DEFINITIONS);
ASTR(material_surface);
ASTR(material_displacement);
ASTR(material_volume);
ASTR(matrix);
ASTR(matrix_multiply_vector);
ASTR(matte);
ASTR(mesh);
ASTR(mesh_light);
ASTR(metallic);
ASTR(metalness);
ASTR(mirror);
ASTR(mirrored_ball);
ASTR(missing);
ASTR(missing_texture_color);
ASTR(mode);
ASTR(motion_end);
ASTR(motion_start);
ASTR(mtl_scope);
ASTR(mtlx);
ASTR(multiply);
ASTR(name);
ASTR(ND_standard_surface_surfaceshader);
ASTR(near_clip);
ASTR(nidxs);
ASTR(nlist);
ASTR(node);
ASTR(node_def);
ASTR(node_entry);
ASTR(node_idxs);
ASTR(nodes);
ASTR(none);
ASTR(normal);
ASTR(normal_nonexistant_rename);
ASTR(normalize);
ASTR(nsides);
ASTR(num_points);
ASTR(object_path);
ASTR(offset);
ASTR(opacity);
ASTR(opaque);
ASTR(options);
ASTR(orientations);
ASTR(oriented);
ASTR(ortho_camera);
ASTR(osl);
ASTR(osl_includepath);
ASTR(osl_struct);
ASTR(outputs);
ASTR(overrides);
ASTR(parallel_node_init);
ASTR(param_colorspace);
ASTR(param_filename);
ASTR(param_shader_file);
ASTR(parent_instance);
ASTR(path);
ASTR(penumbra_angle);
ASTR(periodic);
ASTR(persp_camera);
ASTR(photometric_light);
ASTR(pin_threads);
ASTR(pinned);
ASTR(pixel_aspect_ratio);
ASTR(plugin_searchpath);
ASTR(point_light);
ASTR(points);
ASTR(polymesh);
ASTR(procedural_custom);
ASTR(procedural_searchpath);
ASTR(progressive);
ASTR(progressive_min_AA_samples);
ASTR(progressive_show_all_outputs);
ASTR(projMtx);
ASTR(quad_light);
ASTR(radius);
ASTR(reference_time);
ASTR(region_max_x);
ASTR(region_max_y);
ASTR(region_min_x);
ASTR(region_min_y);
ASTR(render_device);
ASTR(render_outputs);
ASTR(render_settings);
ASTR(request_imager_update);
ASTR(repeat);
ASTR(rotation);
ASTR(roughness);
ASTR(scale);
ASTR(scope);
ASTR(screen_window_max);
ASTR(screen_window_min);
ASTR(set_parameter);
ASTR(sflip);
ASTR(shade_mode);
ASTR(shader);
ASTR(shader_override);
ASTR(shader_prefix);
ASTR(shadow);
ASTR(shadow_color);
ASTR(shadow_group);
ASTR(shidxs);
ASTR(shutter_end);
ASTR(shutter_start);
ASTR(sidedness);
ASTR(skydome_light);
ASTR(smoothing);
ASTR(softmax);
ASTR(softmin);
ASTR(sourceColorSpace);
ASTR(specular);
ASTR(src_image_node);
ASTR(specularColor);
ASTR(specular_IOR);
ASTR(specular_color);
ASTR(specular_reflect);
ASTR(specular_roughness);
ASTR(specular_transmit);
ASTR(spot_light);
ASTR(st);
ASTR(standard_surface);
ASTR(standard_volume);
ASTR(step_size);
ASTR(subdiv_dicing_camera);
ASTR(subdiv_frustum_culling);
ASTR(subdiv_frustum_padding);
ASTR(subdiv_iterations);
ASTR(subdiv_type);
ASTR(subsurface);
ASTR(surface);
ASTR(swrap);
ASTR(texture_accept_unmipped);
ASTR(texture_accept_untiled);
ASTR(texture_auto_generate_tx);
ASTR(texture_automip);
ASTR(texture_autotile);
ASTR(texture_conservative_lookups);
ASTR(texture_failure_retries);
ASTR(texture_max_open_files);
ASTR(texture_per_file_stats);
ASTR(texture_searchpath);
ASTR(tflip);
ASTR(thread_priority);
ASTR(threads);
ASTR(top);
ASTR(total_progress);
ASTR(translation);
ASTR(transmission);
ASTR(twrap);
ASTR(type);
ASTR(uniform);
ASTR(usd);
ASTR(usd_legacy_distant_light_normalize);
ASTR(usdlux_version);
ASTR(usd_legacy_translation);
ASTR(usd_override_double_sided);
ASTR(useMetadata);
ASTR(useSpecularWorkflow);
ASTR(use_light_group);
ASTR(use_shadow_group);
ASTR(user_data_float);
ASTR(user_data_int);
ASTR(user_data_rgb);
ASTR(user_data_rgba);
ASTR(user_data_string);
ASTR(utility);
ASTR(uv);
ASTR(uv_remap);
ASTR(uvcoords);
ASTR(uvidxs);
ASTR(uvlist);
ASTR(uvs);
ASTR(uvset);
ASTR(varname);
ASTR(varying);
ASTR(velocities);
ASTR(vertices);
ASTR(vidxs);
ASTR(viewMtx);
ASTR(visibility);
ASTR(vlist);
ASTR(volume);
ASTR(width);
ASTR(wrap_mode);
ASTR(wrapS);
ASTR(wrapT);
ASTR(xres);
ASTR(yres);
ASTR(Z);

#undef ASTR
#undef ASTR2

} // namespace str

PXR_NAMESPACE_CLOSE_SCOPE
