#include "model/GltfLoader.h"
#include "core/AssetStore.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <stb_image.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

namespace model {
namespace {

using DataHandle = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

cgltf_result assetRead(const cgltf_memory_options* memory,
                       const cgltf_file_options*,const char* path,
                       cgltf_size* size,void** data) {
    try { const auto bytes=AssetStore::readPath(std::filesystem::u8path(path));
        const cgltf_size requested=*size;
        if(requested&&bytes.size()<requested)return cgltf_result_data_too_short;
        void* result=memory->alloc_func?memory->alloc_func(memory->user_data,bytes.size()):std::malloc(bytes.size());
        if(!result&& !bytes.empty())return cgltf_result_out_of_memory;
        if(!bytes.empty())std::memcpy(result,bytes.data(),bytes.size());
        *size=bytes.size();*data=result;return cgltf_result_success;
    } catch(const std::exception&){return cgltf_result_file_not_found;}
}
void assetRelease(const cgltf_memory_options* memory,const cgltf_file_options*,void* data){
    if(memory->free_func)memory->free_func(memory->user_data,data);else std::free(data);
}

LoadResult failure(std::string error) {
    LoadResult result;
    result.error = std::move(error);
    return result;
}

const char* resultName(cgltf_result result) {
    switch (result) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF";
    default: return "unknown error";
    }
}

bool checkedAdd(std::size_t left, std::size_t right, std::size_t& result) {
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

bool checkAccessorBounds(const cgltf_accessor& accessor, std::string& error) {
    if (!accessor.buffer_view || !accessor.buffer_view->buffer || !accessor.buffer_view->buffer->data) {
        error = "accessor has no loaded buffer view";
        return false;
    }
    if (accessor.is_sparse) {
        error = "sparse accessor is unsupported";
        return false;
    }
    const std::size_t elementSize = cgltf_calc_size(accessor.type, accessor.component_type);
    const std::size_t stride = accessor.stride == 0 ? elementSize : accessor.stride;
    if (elementSize == 0 || stride < elementSize) {
        error = "accessor has an invalid element stride";
        return false;
    }
    const cgltf_buffer_view& view = *accessor.buffer_view;
    std::size_t viewEnd = 0;
    if (!checkedAdd(view.offset, view.size, viewEnd) || viewEnd > view.buffer->size) {
        error = "buffer view exceeds its buffer";
        return false;
    }
    if (accessor.count == 0) {
        error = "accessor range has zero elements";
        return false;
    }
    std::size_t tail = 0;
    if (accessor.count - 1 > (std::numeric_limits<std::size_t>::max() - elementSize) / stride ||
        !checkedAdd((accessor.count - 1) * stride, elementSize, tail) ||
        accessor.offset > view.size || tail > view.size - accessor.offset) {
        error = "accessor range exceeds its buffer view";
        return false;
    }
    return true;
}

bool prevalidateCgltfRanges(const cgltf_data& data, std::string& error) {
    for (cgltf_size index = 0; index < data.buffer_views_count; ++index) {
        const cgltf_buffer_view& view = data.buffer_views[index];
        if (!view.buffer || !view.buffer->data) {
            error = "buffer view " + std::to_string(index) + " has no loaded buffer";
            return false;
        }
        std::size_t viewEnd = 0;
        if (!checkedAdd(view.offset, view.size, viewEnd) || viewEnd > view.buffer->size) {
            error = "buffer view " + std::to_string(index) + " range exceeds its buffer";
            return false;
        }
        if (view.has_meshopt_compression) {
            error = "compressed buffer views are unsupported";
            return false;
        }
    }
    for (cgltf_size index = 0; index < data.accessors_count; ++index) {
        if (!checkAccessorBounds(data.accessors[index], error)) {
            error = "accessor range " + std::to_string(index) + " is invalid: " + error;
            return false;
        }
    }
    return true;
}

bool expectAccessor(const cgltf_accessor* accessor, cgltf_type type, std::size_t count,
                    const char* label, std::string& error) {
    if (!accessor) {
        error = std::string(label) + " accessor is missing";
        return false;
    }
    if (accessor->type != type || accessor->count != count || !checkAccessorBounds(*accessor, error)) {
        error = std::string(label) + " accessor is invalid: " + error;
        return false;
    }
    return true;
}

bool isUnsignedJointComponent(cgltf_component_type type) {
    return type == cgltf_component_type_r_8u || type == cgltf_component_type_r_16u;
}

bool isUnsignedIndexComponent(cgltf_component_type type) {
    return type == cgltf_component_type_r_8u || type == cgltf_component_type_r_16u ||
           type == cgltf_component_type_r_32u;
}

bool isValidWeightComponent(const cgltf_accessor& accessor) {
    return accessor.component_type == cgltf_component_type_r_32f ||
           (accessor.normalized && (accessor.component_type == cgltf_component_type_r_8u ||
                                   accessor.component_type == cgltf_component_type_r_16u));
}

int pointerIndex(const cgltf_data& data, const cgltf_node* node) {
    if (!node || node < data.nodes || node >= data.nodes + data.nodes_count) return -1;
    return static_cast<int>(node - data.nodes);
}

int materialIndex(const cgltf_data& data, const cgltf_material* material) {
    if (!material || material < data.materials || material >= data.materials + data.materials_count)
        return -1;
    return static_cast<int>(material - data.materials);
}

int skinIndex(const cgltf_data& data, const cgltf_skin* skin) {
    if (!skin || skin < data.skins || skin >= data.skins + data.skins_count) return -1;
    return static_cast<int>(skin - data.skins);
}

int imageIndex(const cgltf_data& data, const cgltf_image* image) {
    if (!image || image < data.images || image >= data.images + data.images_count) return -1;
    return static_cast<int>(image - data.images);
}

AlphaMode alphaMode(cgltf_alpha_mode mode) {
    switch (mode) {
    case cgltf_alpha_mode_mask: return AlphaMode::Mask;
    case cgltf_alpha_mode_blend: return AlphaMode::Blend;
    default: return AlphaMode::Opaque;
    }
}

bool convertImage(const cgltf_image& source, ImageData& destination, std::string& error) {
    if (!source.buffer_view || !source.buffer_view->buffer || !source.buffer_view->buffer->data ||
        source.buffer_view->size == 0) {
        error = "image has no embedded loaded data";
        return false;
    }
    const auto* encoded = static_cast<const stbi_uc*>(cgltf_buffer_view_data(source.buffer_view));
    if (!encoded || source.buffer_view->size > static_cast<cgltf_size>(std::numeric_limits<int>::max())) {
        error = "image data is unavailable or too large";
        return false;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(encoded, static_cast<int>(source.buffer_view->size),
                                            &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        error = std::string("image decode failed") +
                (stbi_failure_reason() ? ": " + std::string(stbi_failure_reason()) : "");
        stbi_image_free(pixels);
        return false;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4) {
        stbi_image_free(pixels);
        error = "image dimensions overflow";
        return false;
    }
    destination.width = width;
    destination.height = height;
    destination.channels = 4;
    destination.pixels.assign(pixels, pixels + pixelCount * 4);
    stbi_image_free(pixels);
    return true;
}

bool convertPrimitive(const cgltf_data& data, const cgltf_primitive& source, Primitive& destination,
                      std::string& error) {
    if (source.type != cgltf_primitive_type_triangles) {
        error = "primitive mode must be TRIANGLES";
        return false;
    }
    const cgltf_accessor* positions = cgltf_find_accessor(&source, cgltf_attribute_type_position, 0);
    if (!positions || positions->type != cgltf_type_vec3 ||
        positions->component_type != cgltf_component_type_r_32f || !checkAccessorBounds(*positions, error)) {
        error = "POSITION accessor is invalid: " + error;
        return false;
    }
    if (positions->count == 0) {
        error = "POSITION accessor is empty";
        return false;
    }
    const cgltf_accessor* normals = cgltf_find_accessor(&source, cgltf_attribute_type_normal, 0);
    if (normals && (!expectAccessor(normals, cgltf_type_vec3, positions->count, "NORMAL", error) ||
                    normals->component_type != cgltf_component_type_r_32f)) {
        error = "NORMAL accessor is invalid";
        return false;
    }
    const cgltf_accessor* uvs = cgltf_find_accessor(&source, cgltf_attribute_type_texcoord, 0);
    if (uvs && (!expectAccessor(uvs, cgltf_type_vec2, positions->count, "TEXCOORD_0", error) ||
                uvs->component_type != cgltf_component_type_r_32f)) {
        error = "TEXCOORD_0 accessor is invalid";
        return false;
    }
    const cgltf_accessor* joints = cgltf_find_accessor(&source, cgltf_attribute_type_joints, 0);
    const cgltf_accessor* weights = cgltf_find_accessor(&source, cgltf_attribute_type_weights, 0);
    if (static_cast<bool>(joints) != static_cast<bool>(weights)) {
        error = "JOINTS_0 and WEIGHTS_0 must be supplied together";
        return false;
    }
    if (joints && (!expectAccessor(joints, cgltf_type_vec4, positions->count, "JOINTS_0", error) ||
                   !expectAccessor(weights, cgltf_type_vec4, positions->count, "WEIGHTS_0", error) ||
                   !isUnsignedJointComponent(joints->component_type) ||
                   !isValidWeightComponent(*weights))) {
        error = "WEIGHTS_0 or JOINTS_0 skinning accessor is invalid";
        return false;
    }
    destination.material = materialIndex(data, source.material);
    destination.vertices.resize(positions->count);
    bool hasBounds = false;
    for (cgltf_size index = 0; index < positions->count; ++index) {
        Vertex& vertex = destination.vertices[index];
        float values[4]{};
        if (!cgltf_accessor_read_float(positions, index, values, 3)) {
            error = "POSITION accessor read failed";
            return false;
        }
        vertex.position = {values[0], values[1], values[2]};
        if (normals) {
            if (!cgltf_accessor_read_float(normals, index, values, 3)) {
                error = "NORMAL accessor read failed";
                return false;
            }
            vertex.normal = {values[0], values[1], values[2]};
        }
        if (uvs) {
            if (!cgltf_accessor_read_float(uvs, index, values, 2)) {
                error = "TEXCOORD_0 accessor read failed";
                return false;
            }
            vertex.uv = {values[0], values[1]};
        }
        if (joints) {
            cgltf_uint jointValues[4]{};
            if (!cgltf_accessor_read_uint(joints, index, jointValues, 4) ||
                !cgltf_accessor_read_float(weights, index, values, 4)) {
                error = "skinning accessor read failed";
                return false;
            }
            vertex.joints = {jointValues[0], jointValues[1], jointValues[2], jointValues[3]};
            vertex.weights = {values[0], values[1], values[2], values[3]};
            normalizeWeights(vertex);
        }
        if (!hasBounds) {
            destination.boundsMin = destination.boundsMax = vertex.position;
            hasBounds = true;
        } else {
            destination.boundsMin = glm::min(destination.boundsMin, vertex.position);
            destination.boundsMax = glm::max(destination.boundsMax, vertex.position);
        }
    }
    if (source.indices) {
        if (source.indices->type != cgltf_type_scalar || !isUnsignedIndexComponent(source.indices->component_type) ||
            !checkAccessorBounds(*source.indices, error)) {
            error = "index accessor is invalid: " + error;
            return false;
        }
        destination.indices.reserve(source.indices->count);
        for (cgltf_size index = 0; index < source.indices->count; ++index) {
            const cgltf_size value = cgltf_accessor_read_index(source.indices, index);
            if (value >= destination.vertices.size()) {
                error = "index accessor references a vertex out of range";
                return false;
            }
            destination.indices.push_back(static_cast<uint32_t>(value));
        }
    } else {
        destination.indices.resize(destination.vertices.size());
        for (std::size_t index = 0; index < destination.indices.size(); ++index)
            destination.indices[index] = static_cast<uint32_t>(index);
    }
    if (destination.indices.size() % 3 != 0) {
        error = "triangle primitive has a non-triangle index count";
        return false;
    }
    return true;
}

bool convertSkins(const cgltf_data& data, ModelAsset& asset, std::string& error) {
    asset.skins.reserve(data.skins_count);
    for (cgltf_size skinIndexValue = 0; skinIndexValue < data.skins_count; ++skinIndexValue) {
        const cgltf_skin& source = data.skins[skinIndexValue];
        if (source.joints_count > MAX_JOINTS) {
            error = "skin has " + std::to_string(source.joints_count) + " joints; limit is " +
                    std::to_string(MAX_JOINTS);
            return false;
        }
        Skin skin;
        skin.joints.reserve(source.joints_count);
        for (cgltf_size joint = 0; joint < source.joints_count; ++joint) {
            const int node = pointerIndex(data, source.joints[joint]);
            if (node < 0) {
                error = "skin has an invalid joint node";
                return false;
            }
            skin.joints.push_back(node);
        }
        if (source.inverse_bind_matrices) {
            if (!expectAccessor(source.inverse_bind_matrices, cgltf_type_mat4, source.joints_count,
                                "inverse bind matrix", error) ||
                source.inverse_bind_matrices->component_type != cgltf_component_type_r_32f) {
                error = "inverse bind matrix accessor is invalid";
                return false;
            }
            skin.inverseBindMatrices.resize(source.joints_count);
            for (cgltf_size index = 0; index < source.joints_count; ++index) {
                float values[16]{};
                if (!cgltf_accessor_read_float(source.inverse_bind_matrices, index, values, 16)) {
                    error = "inverse bind matrix accessor read failed";
                    return false;
                }
                skin.inverseBindMatrices[index] = glm::make_mat4(values);
            }
        } else {
            skin.inverseBindMatrices.assign(source.joints_count, glm::mat4(1.0f));
        }
        asset.skins.push_back(std::move(skin));
    }
    return true;
}

bool convertAnimations(const cgltf_data& data, ModelAsset& asset, std::string& error) {
    asset.animations.reserve(data.animations_count);
    for (cgltf_size animationIndex = 0; animationIndex < data.animations_count; ++animationIndex) {
        const cgltf_animation& source = data.animations[animationIndex];
        AnimationClip clip;
        if (source.name) clip.name = source.name;
        for (cgltf_size channelIndex = 0; channelIndex < source.channels_count; ++channelIndex) {
            const cgltf_animation_channel& sourceChannel = source.channels[channelIndex];
            if (!sourceChannel.sampler || !sourceChannel.target_node) {
                error = "animation channel has no sampler or target node";
                return false;
            }
            const int node = pointerIndex(data, sourceChannel.target_node);
            if (node < 0) {
                error = "animation channel targets an invalid node";
                return false;
            }
            if (asset.nodes[static_cast<std::size_t>(node)].usesMatrix) {
                error = "animation channel targets a matrix-authored node";
                return false;
            }
            Channel channel;
            channel.node = node;
            switch (sourceChannel.target_path) {
            case cgltf_animation_path_type_translation: channel.path = ChannelPath::Translation; break;
            case cgltf_animation_path_type_rotation: channel.path = ChannelPath::Rotation; break;
            case cgltf_animation_path_type_scale: channel.path = ChannelPath::Scale; break;
            default:
                error = "animation channel has an unsupported path";
                return false;
            }
            const cgltf_animation_sampler& sampler = *sourceChannel.sampler;
            if (!sampler.input || sampler.input->type != cgltf_type_scalar ||
                sampler.input->component_type != cgltf_component_type_r_32f || sampler.input->count == 0 ||
                !checkAccessorBounds(*sampler.input, error)) {
                error = "animation input accessor is invalid";
                return false;
            }
            switch (sampler.interpolation) {
            case cgltf_interpolation_type_step: channel.interpolation = Interpolation::Step; break;
            case cgltf_interpolation_type_linear: channel.interpolation = Interpolation::Linear; break;
            case cgltf_interpolation_type_cubic_spline: channel.interpolation = Interpolation::CubicSpline; break;
            default:
                error = "animation interpolation is unsupported";
                return false;
            }
            const cgltf_type valueType = channel.path == ChannelPath::Rotation ? cgltf_type_vec4 : cgltf_type_vec3;
            const cgltf_size valueCount = sampler.input->count *
                (channel.interpolation == Interpolation::CubicSpline ? 3 : 1);
            if (!sampler.output || sampler.output->type != valueType || sampler.output->count != valueCount ||
                sampler.output->component_type != cgltf_component_type_r_32f || !checkAccessorBounds(*sampler.output, error)) {
                error = "animation output accessor is invalid";
                return false;
            }
            channel.times.resize(sampler.input->count);
            for (cgltf_size index = 0; index < sampler.input->count; ++index) {
                if (!cgltf_accessor_read_float(sampler.input, index, &channel.times[index], 1) ||
                    (index > 0 && channel.times[index] < channel.times[index - 1])) {
                    error = "animation keyframe times are invalid";
                    return false;
                }
            }
            channel.values.resize(valueCount);
            for (cgltf_size index = 0; index < valueCount; ++index) {
                float values[4]{};
                if (!cgltf_accessor_read_float(sampler.output, index, values,
                                               valueType == cgltf_type_vec4 ? 4 : 3)) {
                    error = "animation output accessor read failed";
                    return false;
                }
                channel.values[index] = {values[0], values[1], values[2], values[3]};
            }
            clip.duration = std::max(clip.duration, channel.times.back());
            clip.channels.push_back(std::move(channel));
        }
        asset.animations.push_back(std::move(clip));
    }
    return true;
}

} // namespace

LoadResult loadGltf(const std::filesystem::path& path) {
    cgltf_options options{};
    options.file.read=&assetRead;options.file.release=&assetRelease;
    cgltf_data* parsed = nullptr;
    const std::string nativePath = path.string();
    cgltf_result result = cgltf_parse_file(&options, nativePath.c_str(), &parsed);
    if (result != cgltf_result_success) return failure("glTF parse failed: " + std::string(resultName(result)));
    DataHandle data(parsed, &cgltf_free);
    result = cgltf_load_buffers(&options, data.get(), nativePath.c_str());
    if (result != cgltf_result_success) return failure("glTF buffer load failed: " + std::string(resultName(result)));
    for (cgltf_size index = 0; index < data->extensions_required_count; ++index) {
        return failure("required glTF extension is unsupported: " +
                       std::string(data->extensions_required[index] ? data->extensions_required[index] : "unnamed"));
    }
    for (cgltf_size index = 0; index < data->skins_count; ++index) {
        if (data->skins[index].joints_count > MAX_JOINTS) {
            return failure("skin has " + std::to_string(data->skins[index].joints_count) +
                           " joints; limit is " + std::to_string(MAX_JOINTS));
        }
    }
    std::string error;
    if (!prevalidateCgltfRanges(*data, error)) return failure(std::move(error));
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success)
        return failure("glTF accessor or buffer validation failed: " + std::string(resultName(result)));

    auto asset = std::make_shared<ModelAsset>();
    if (!convertSkins(*data, *asset, error)) return failure(std::move(error));

    asset->materials.reserve(data->materials_count);
    for (cgltf_size index = 0; index < data->materials_count; ++index) {
        const cgltf_material& source = data->materials[index];
        Material material;
        material.alphaMode = alphaMode(source.alpha_mode);
        material.alphaCutoff = source.alpha_cutoff;
        material.doubleSided = source.double_sided != 0;
        if (source.has_pbr_metallic_roughness) {
            material.baseColor = {source.pbr_metallic_roughness.base_color_factor[0],
                                  source.pbr_metallic_roughness.base_color_factor[1],
                                  source.pbr_metallic_roughness.base_color_factor[2],
                                  source.pbr_metallic_roughness.base_color_factor[3]};
            if (source.pbr_metallic_roughness.base_color_texture.texture)
                material.image = imageIndex(*data, source.pbr_metallic_roughness.base_color_texture.texture->image);
        }
        asset->materials.push_back(material);
    }
    asset->images.reserve(data->images_count);
    for (cgltf_size index = 0; index < data->images_count; ++index) {
        ImageData image;
        if (!convertImage(data->images[index], image, error)) return failure(std::move(error));
        asset->images.push_back(std::move(image));
    }

    std::vector<std::vector<int>> meshPrimitives(data->meshes_count);
    bool hasBounds = false;
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        const cgltf_mesh& mesh = data->meshes[meshIndex];
        meshPrimitives[meshIndex].reserve(mesh.primitives_count);
        for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
            Primitive primitive;
            if (!convertPrimitive(*data, mesh.primitives[primitiveIndex], primitive, error)) return failure(std::move(error));
            if (!hasBounds) {
                asset->boundsMin = primitive.boundsMin;
                asset->boundsMax = primitive.boundsMax;
                hasBounds = true;
            } else {
                asset->boundsMin = glm::min(asset->boundsMin, primitive.boundsMin);
                asset->boundsMax = glm::max(asset->boundsMax, primitive.boundsMax);
            }
            meshPrimitives[meshIndex].push_back(static_cast<int>(asset->primitives.size()));
            asset->primitives.push_back(std::move(primitive));
        }
    }

    asset->nodes.resize(data->nodes_count);
    constexpr int UNBOUND_PRIMITIVE = -3;
    constexpr int CONFLICTING_PRIMITIVE = -2;
    std::vector<int> primitiveSkinBindings(
        asset->primitives.size(), UNBOUND_PRIMITIVE);
    for (cgltf_size index = 0; index < data->nodes_count; ++index) {
        const cgltf_node& source = data->nodes[index];
        Node& node = asset->nodes[index];
        if (source.name) node.name = source.name;
        node.translation = {source.translation[0], source.translation[1], source.translation[2]};
        node.scale = {source.scale[0], source.scale[1], source.scale[2]};
        node.rotation = glm::quat(source.rotation[3], source.rotation[0], source.rotation[1], source.rotation[2]);
        node.usesMatrix = source.has_matrix != 0;
        if (node.usesMatrix) node.matrix = glm::make_mat4(source.matrix);
        node.skin = skinIndex(*data, source.skin);
        if (source.mesh) {
            const cgltf_size meshIndex = cgltf_mesh_index(data.get(), source.mesh);
            if (meshIndex >= meshPrimitives.size()) return failure("node references an invalid mesh");
            node.primitives = meshPrimitives[meshIndex];
            for (int primitiveIndex : node.primitives) {
                Primitive& primitive = asset->primitives[primitiveIndex];
                int& binding = primitiveSkinBindings[
                    static_cast<std::size_t>(primitiveIndex)];
                if (binding == UNBOUND_PRIMITIVE) binding = node.skin;
                else if (binding != node.skin) binding = CONFLICTING_PRIMITIVE;
                if (node.skin >= 0) {
                    const std::size_t jointCount = asset->skins[static_cast<std::size_t>(node.skin)].joints.size();
                    for (const Vertex& vertex : primitive.vertices) {
                        if (vertex.joints.x >= jointCount || vertex.joints.y >= jointCount ||
                            vertex.joints.z >= jointCount || vertex.joints.w >= jointCount) {
                            return failure("vertex joint index is outside its skin joint range");
                        }
                    }
                }
            }
        }
        node.children.reserve(source.children_count);
        for (cgltf_size childIndex = 0; childIndex < source.children_count; ++childIndex) {
            const int child = pointerIndex(*data, source.children[childIndex]);
            if (child < 0 || asset->nodes[child].parent >= 0) return failure("node hierarchy is invalid");
            node.children.push_back(child);
            asset->nodes[child].parent = static_cast<int>(index);
        }
    }
    for (std::size_t index = 0; index < asset->primitives.size(); ++index) {
        const int binding = primitiveSkinBindings[index];
        asset->primitives[index].skin = binding >= 0 ? binding : -1;
    }
    if (data->scene) {
        for (cgltf_size index = 0; index < data->scene->nodes_count; ++index) {
            const int root = pointerIndex(*data, data->scene->nodes[index]);
            if (root < 0) return failure("scene has an invalid root node");
            asset->sceneRoots.push_back(root);
        }
    } else {
        for (std::size_t index = 0; index < asset->nodes.size(); ++index)
            if (asset->nodes[index].parent < 0) asset->sceneRoots.push_back(static_cast<int>(index));
    }
    if (!convertAnimations(*data, *asset, error)) return failure(std::move(error));

    LoadResult success;
    success.asset = std::move(asset);
    return success;
}

} // namespace model
