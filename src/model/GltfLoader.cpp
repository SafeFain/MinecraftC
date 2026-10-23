#include "model/GltfLoader.h"
#include "core/AssetStore.h"
#include "core/AllocationBudget.h"

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

void budgetRequire(bool ok, const char* label) {
    if (!ok) throw std::runtime_error(std::string("model budget exceeded: ") + label);
}
void consume(size_t& total, size_t amount, size_t maximum, const char* label) {
    budgetRequire(total <= maximum && amount <= maximum - total, label);
    total += amount;
}
struct LoadBudget {
    const LoadLimits& limits;
    size_t encoded = 0, decoded = 0, converted = 0;
    void output(size_t count, size_t width) {
        budgetRequire(converted <= limits.convertedBytes &&
            (!width || count <= (limits.convertedBytes - converted) / width), "converted bytes");
        converted += count * width;
    }
};

// All counts are inspected before conversion resizes any output vector. Shared
// accessors/channels are charged for every output copy, including sparse bases.
void validateBudgets(const cgltf_data& data, LoadBudget& budget) {
    const auto& l = budget.limits;
    budgetRequire(data.meshes_count <= l.primitives, "meshes");
    budgetRequire(data.nodes_count <= l.nodes, "nodes");
    budgetRequire(data.skins_count <= l.skins, "skins");
    budgetRequire(data.animations_count <= l.animations, "animations");
    budgetRequire(data.materials_count <= l.materials, "materials");
    budgetRequire(data.images_count <= l.images, "images");
    budgetRequire(data.textures_count <= l.textures, "textures");
    budgetRequire(data.samplers_count <= l.samplers, "samplers");
    budgetRequire(data.buffers_count <= l.buffers, "buffers");
    budgetRequire(data.buffer_views_count <= l.bufferViews, "buffer views");
    budgetRequire(data.accessors_count <= l.accessors, "accessors");
    const auto* scene=data.scene?data.scene:(data.scenes_count?&data.scenes[0]:nullptr);
    budgetRequire(!scene || scene->nodes_count<=l.nodes,"scene roots");
    budget.output(scene?scene->nodes_count:data.nodes_count,sizeof(int));
    budget.output(data.extensions_used_count,sizeof(std::string)+32);
    for(size_t i=0;i<data.extensions_used_count;++i)
        if(data.extensions_used[i]) budget.output(std::strlen(data.extensions_used[i]),1);
    budget.output(data.nodes_count, sizeof(Node) + 2 * sizeof(glm::mat4));
    budget.output(data.skins_count, sizeof(Skin));
    budget.output(data.animations_count, sizeof(AnimationClip));
    budget.output(data.materials_count, sizeof(Material));
    budget.output(data.images_count, sizeof(ImageData));
    budget.output(data.textures_count, sizeof(Texture));
    budget.output(data.samplers_count, sizeof(Sampler));
    size_t primitives=0, vertices=0, indices=0, bindings=0, boundVertices=0;
    for(size_t m=0;m<data.meshes_count;++m) {
        consume(primitives,data.meshes[m].primitives_count,l.primitives,"primitives");
        for(size_t p=0;p<data.meshes[m].primitives_count;++p) {
            const auto& primitive=data.meshes[m].primitives[p];
            const auto* position=cgltf_find_accessor(&primitive,cgltf_attribute_type_position,0);
            const size_t vertexCount=position?position->count:0;
            budgetRequire(vertexCount<=l.primitiveVertices,"primitive vertices");
            const size_t indexCount=primitive.indices?primitive.indices->count:vertexCount;
            budgetRequire(indexCount<=l.primitiveIndices,"primitive indices");
            consume(vertices,vertexCount,l.vertices,"total vertices");
            consume(indices,indexCount,l.indices,"total indices");
            budget.output(vertexCount,sizeof(Vertex));budget.output(indexCount,sizeof(uint32_t));
        }
    }
    budget.output(primitives,sizeof(Primitive)+sizeof(int));
    budget.output(data.meshes_count,sizeof(std::vector<int>));
    for(size_t n=0;n<data.nodes_count;++n) {
        const auto& node=data.nodes[n];
        budgetRequire(node.children_count<=l.nodes,"node children");
        budget.output(node.children_count,sizeof(int));
        if(node.name) budget.output(std::strlen(node.name),1);
        if(!node.mesh) continue;
        consume(bindings,node.mesh->primitives_count,l.bindings,"draw bindings");
        for(size_t p=0;p<node.mesh->primitives_count;++p) {
            const auto& primitive=node.mesh->primitives[p];
            const auto* position=cgltf_find_accessor(&primitive,cgltf_attribute_type_position,0);
            if(position) consume(boundVertices,position->count,l.boundVertices,"bound vertices");
        }
    }
    budget.output(bindings,sizeof(DrawBinding)+sizeof(int));
    for(size_t s=0;s<data.skins_count;++s) {
        if(data.skins[s].joints_count>MAX_JOINTS) throw std::runtime_error("skin has " +
            std::to_string(data.skins[s].joints_count) + " joints; limit is " + std::to_string(MAX_JOINTS));
        budget.output(data.skins[s].joints_count,sizeof(int)+sizeof(glm::mat4));
    }
    size_t channels=0,keys=0;
    for(size_t a=0;a<data.animations_count;++a) {
        const auto& animation=data.animations[a];
        consume(channels,animation.channels_count,l.channels,"animation channels");
        budgetRequire(animation.samplers_count<=l.channels,"animation samplers");
        if(animation.name) budget.output(std::strlen(animation.name),2);
        for(size_t c=0;c<animation.channels_count;++c) {
            const auto* sampler=animation.channels[c].sampler;
            if(!sampler || !sampler->input || !sampler->output) continue;
            consume(keys,sampler->input->count,l.keyframes,"animation keyframes");
            budgetRequire(sampler->input->count<=l.keyframes &&
                sampler->output->count/3<=l.keyframes,"animation values");
            budget.output(sampler->input->count,sizeof(float));
            budget.output(sampler->output->count,sizeof(glm::vec4));
        }
    }
    budget.output(channels,sizeof(Channel));
}

void validateJsonDepth(const std::vector<uint8_t>& bytes, size_t maximum) {
    size_t begin=0,end=bytes.size();
    if(bytes.size()>=20 && std::memcmp(bytes.data(),"glTF",4)==0) {
        const auto* p=bytes.data()+12;
        const uint32_t length=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
        if(length>bytes.size()-20) return; // cgltf reports the malformed container.
        begin=20;end=begin+length;
    }
    size_t depth=0;bool string=false,escaped=false;
    for(size_t i=begin;i<end;++i) {
        const auto c=bytes[i];
        if(string) {if(escaped) escaped=false;else if(c=='\\') escaped=true;else if(c=='"') string=false;continue;}
        if(c=='"') string=true;
        else if(c=='{' || c=='[') {budgetRequire(depth<maximum,"JSON nesting");++depth;}
        else if((c=='}' || c==']') && depth) --depth;
    }
}

struct ReadContext {
    const AssetSource* source;
    std::filesystem::path model;
    LoadBudget* budget;
};

std::filesystem::path confinedPath(const AssetSource& source, const std::filesystem::path& path) {
    const auto normalized = path.lexically_normal();
    const auto relative = normalized.is_absolute()
        ? normalized.lexically_relative(source.root.lexically_normal()) : normalized;
    if (!AssetStore::validPath(relative.generic_u8string()))
        throw std::runtime_error("asset dependency escapes source root: " + path.generic_u8string());
    // Filesystem sources also reject symlinks that escape their declared root.
    if (!source.read) {
        std::error_code ec;
        const auto canonicalRoot = std::filesystem::weakly_canonical(source.root, ec);
        if (!ec) {
            const auto canonical = std::filesystem::weakly_canonical(source.root / relative, ec);
            if (!ec && !AssetStore::validPath(canonical.lexically_relative(canonicalRoot).generic_u8string()))
                throw std::runtime_error("asset symlink escapes source root");
        }
    }
    return relative;
}

std::vector<uint8_t> readResource(const AssetSource& source, const std::filesystem::path& path, LoadBudget& budget) {
    const auto relative = confinedPath(source, path);
    const auto maximum=std::min(budget.limits.fileBytes,budget.limits.encodedBytes-budget.encoded);
    auto bytes=source.read ? source.read(relative,maximum) : AssetStore::readPath(source.root / relative,maximum);
    budgetRequire(bytes.size()<=maximum,"resource bytes");
    consume(budget.encoded,bytes.size(),budget.limits.encodedBytes,"total encoded bytes");
    return bytes;
}

std::vector<uint8_t> decodeDataUri(const std::string& uri, LoadBudget& budget) {
    const auto comma = uri.find(',');
    if (comma == std::string::npos || comma < 7 || uri.substr(comma - 7, 7) != ";base64")
        throw std::runtime_error("unsupported data URI encoding");
    std::vector<uint8_t> bytes;
    const auto length=uri.size()-comma-1;
    if(length%4) throw std::runtime_error("truncated base64 data");
    const auto groups=length/4;
    const size_t paddingBytes=length && uri.back()=='=' ? (length>1 && uri[uri.size()-2]=='=' ? 2 : 1) : 0;
    const auto maximum=std::min(budget.limits.fileBytes,budget.limits.encodedBytes-budget.encoded);
    budgetRequire(groups<=std::numeric_limits<size_t>::max()/3,"data URI size");
    const size_t decodedSize=groups*3-paddingBytes;
    budgetRequire(decodedSize<=maximum,"data URI bytes");
    bytes.reserve(decodedSize);
    uint32_t value = 0; int bits = 0; bool padding = false; unsigned pads = 0;
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = comma + 1; i < uri.size(); ++i) {
        const char c = uri[i];
        if (c == '=') { padding = true; ++pads; if (pads > 2) throw std::runtime_error("invalid base64 padding"); continue; }
        const auto digit = alphabet.find(c);
        if (padding || digit == std::string::npos) throw std::runtime_error("invalid base64 data");
        value = (value << 6) | static_cast<uint32_t>(digit); bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back(static_cast<uint8_t>(value >> bits)); }
    }
    if ((uri.size() - comma - 1) % 4 || (bits && (value & ((1u << bits) - 1))) ||
        (pads == 1 && bits != 2) || (pads == 2 && bits != 4))
        throw std::runtime_error("truncated base64 data");
    consume(budget.encoded,bytes.size(),budget.limits.encodedBytes,"total encoded bytes");
    return bytes;
}

std::vector<uint8_t> readUri(const ReadContext& context, const char* uri) {
    const std::string encoded(uri ? uri : "");
    if (encoded.rfind("data:", 0) == 0) return decodeDataUri(encoded,*context.budget);
    std::string decoded = encoded;
    decoded.resize(cgltf_decode_uri(decoded.data()));
    if (decoded.find(':') != std::string::npos || decoded.find('\\') != std::string::npos ||
        decoded.find('\0') != std::string::npos || std::filesystem::u8path(decoded).is_absolute())
        throw std::runtime_error("unsupported or absolute asset URI");
    return readResource(*context.source, context.model.parent_path() / std::filesystem::u8path(decoded),*context.budget);
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

bool checkRange(const cgltf_buffer_view* view, std::size_t offset, std::size_t count,
                std::size_t stride, std::size_t elementSize, std::string& error) {
    if (!view || !view->buffer || !view->buffer->data || !count || !elementSize || stride < elementSize) {
        error = "accessor has no loaded buffer or invalid count/stride"; return false;
    }
    std::size_t viewEnd = 0;
    if (!checkedAdd(view->offset, view->size, viewEnd) || viewEnd > view->buffer->size ||
        count - 1 > (std::numeric_limits<std::size_t>::max() - elementSize) / stride ||
        offset > view->size || (count - 1) * stride + elementSize > view->size - offset) {
        error = "accessor range exceeds its buffer view"; return false;
    }
    return true;
}

uint32_t readUnsigned(const uint8_t* p, cgltf_component_type type) {
    if (type == cgltf_component_type_r_8u) return *p;
    if (type == cgltf_component_type_r_16u) { uint16_t v; std::memcpy(&v,p,2); return v; }
    uint32_t v; std::memcpy(&v,p,4); return v;
}

bool checkAccessorBounds(const cgltf_accessor& accessor, std::string& error) {
    const auto size = cgltf_calc_size(accessor.type, accessor.component_type);
    const auto stride = accessor.stride ? accessor.stride : size;
    const auto componentSize=cgltf_component_size(accessor.component_type);
    if(!componentSize || accessor.offset%componentSize || stride%componentSize ||
       (accessor.buffer_view && accessor.buffer_view->offset%componentSize)) {error="unaligned accessor";return false;}
    if (!size || !accessor.count || accessor.count > std::numeric_limits<std::size_t>::max() / (16 * sizeof(float))) {
        error = "invalid accessor size/count"; return false;
    }
    if (accessor.buffer_view && !checkRange(accessor.buffer_view, accessor.offset, accessor.count, stride, size, error)) return false;
    if (!accessor.buffer_view && !accessor.is_sparse) { error = "accessor has no loaded buffer view"; return false; }
    if (accessor.is_sparse) {
        const auto& sparse = accessor.sparse;
        const auto component = sparse.indices_component_type;
        if (component != cgltf_component_type_r_8u && component != cgltf_component_type_r_16u && component != cgltf_component_type_r_32u) {
            error = "invalid sparse index component"; return false;
        }
        const auto indexSize = cgltf_component_size(component);
        if(!sparse.indices_buffer_view || !sparse.values_buffer_view ||
           sparse.indices_byte_offset%indexSize || sparse.indices_buffer_view->offset%indexSize ||
           sparse.values_byte_offset%componentSize || sparse.values_buffer_view->offset%componentSize) {error="unaligned sparse accessor";return false;}
        if (sparse.count > accessor.count || !checkRange(sparse.indices_buffer_view, sparse.indices_byte_offset, sparse.count, indexSize, indexSize, error) ||
            !checkRange(sparse.values_buffer_view, sparse.values_byte_offset, sparse.count, size, size, error)) return false;
        const auto* indices = cgltf_buffer_view_data(sparse.indices_buffer_view) + sparse.indices_byte_offset;
        uint32_t previous = 0;
        for (std::size_t i = 0; i < sparse.count; ++i) {
            const auto value = readUnsigned(indices + i * indexSize, component);
            if (value >= accessor.count || (i && value <= previous)) { error = "sparse indices out of range or unordered"; return false; }
            previous = value;
        }
    }
    return true;
}

// cgltf 1.15 element readers reject sparse accessors. Resolve one overlay element
// before invoking its normal conversion, using tightly packed sparse values.
cgltf_accessor elementAccessor(const cgltf_accessor& source, std::size_t& index, cgltf_buffer_view& view) {
    cgltf_accessor result = source;
    result.is_sparse = false;
    if (source.is_sparse) {
        const auto& sparse = source.sparse;
        const auto width = cgltf_component_size(sparse.indices_component_type);
        const auto* indices = cgltf_buffer_view_data(sparse.indices_buffer_view) + sparse.indices_byte_offset;
        std::size_t lo = 0, hi = sparse.count;
        while (lo < hi) { const auto mid = lo + (hi-lo)/2;
            if (readUnsigned(indices + mid*width, sparse.indices_component_type) < index) lo = mid+1; else hi = mid; }
        if (lo < sparse.count && readUnsigned(indices + lo*width, sparse.indices_component_type) == index) {
            view = *sparse.values_buffer_view;
            result.buffer_view = &view;
            result.offset = sparse.values_byte_offset;
            result.stride = cgltf_calc_size(source.type, source.component_type);
            index = lo;
        }
    }
    return result;
}

bool readFloat(const cgltf_accessor* source, std::size_t index, float* out, std::size_t count) {
    cgltf_buffer_view view{}; auto accessor = elementAccessor(*source, index, view);
    if (!cgltf_accessor_read_float(&accessor, index, out, count)) return false;
    for (std::size_t i=0;i<count;++i) if (!std::isfinite(out[i])) return false;
    return true;
}
bool readUint(const cgltf_accessor* source, std::size_t index, cgltf_uint* out, std::size_t count) {
    cgltf_buffer_view view{}; auto accessor = elementAccessor(*source, index, view);
    return cgltf_accessor_read_uint(&accessor, index, out, count) != 0;
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

bool convertImage(const cgltf_image& source, const ReadContext& context, ImageData& destination, std::string& error) {
    std::vector<uint8_t> external;
    if (source.uri) external = readUri(context, source.uri);
    if (!source.uri && (!source.buffer_view || !source.buffer_view->buffer || !source.buffer_view->buffer->data ||
        source.buffer_view->size == 0)) {
        error = "image has no embedded loaded data";
        return false;
    }
    const auto* encoded = source.uri ? external.data() : static_cast<const stbi_uc*>(cgltf_buffer_view_data(source.buffer_view));
    const auto encodedSize = source.uri ? external.size() : source.buffer_view->size;
    if (!encoded || encodedSize > static_cast<cgltf_size>(std::numeric_limits<int>::max())) {
        error = "image data is unavailable or too large";
        return false;
    }
    const bool png=encodedSize>=8 && std::memcmp(encoded,"\x89PNG\r\n\x1a\n",8)==0;
    const bool jpeg=encodedSize>=3 && encoded[0]==0xff && encoded[1]==0xd8 && encoded[2]==0xff;
    if(!png && !jpeg) {error="only PNG/JPEG model images are supported";return false;}
    int width = 0;
    int height = 0;
    int channels = 0;
    AllocationBudget allocations(context.budget->limits.decoderBytes);
    DecoderAllocationScope allocationScope(allocations);
    if (!stbi_info_from_memory(encoded, static_cast<int>(encodedSize), &width, &height, &channels) ||
        width <= 0 || height <= 0) { error="invalid image header";return false; }
    const auto& limits=context.budget->limits;
    budgetRequire(static_cast<size_t>(width)<=limits.imageDimension &&
        static_cast<size_t>(height)<=limits.imageDimension,"image dimensions");
    budgetRequire(static_cast<size_t>(width)<=limits.imagePixels/static_cast<size_t>(height),"image pixels");
    const size_t expectedPixels=static_cast<size_t>(width)*static_cast<size_t>(height);
    budgetRequire(expectedPixels<=std::numeric_limits<size_t>::max()/4,"image bytes");
    consume(context.budget->decoded,expectedPixels*4,limits.decodedBytes,"total decoded bytes");
    context.budget->output(expectedPixels,4);
    const int expectedWidth=width,expectedHeight=height;
    stbi_uc* pixels = stbi_load_from_memory(encoded, static_cast<int>(encodedSize),
                                            &width, &height, &channels, 4);
    const std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> ownedPixels(pixels,&stbi_image_free);
    if (!pixels || width <= 0 || height <= 0) {
        error = std::string(allocations.rejected?"model budget exceeded: image decoder":"image decode failed") +
                (stbi_failure_reason() ? ": " + std::string(stbi_failure_reason()) : "");
        return false;
    }
    if(width!=expectedWidth || height!=expectedHeight) {error="image dimensions changed after preflight";return false;}
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4) {
        error = "image dimensions overflow";
        return false;
    }
    destination.width = width;
    destination.height = height;
    destination.channels = 4;
    destination.pixels.assign(pixels, pixels + pixelCount * 4);
    return true;
}

bool convertPrimitive(const cgltf_data& data, const cgltf_primitive& source, Primitive& destination,
                      std::string& error) {
    if(source.targets_count || source.has_draco_mesh_compression) {error="morph targets and compressed primitives are unsupported";return false;}
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
    if (positions->count == 0 || positions->count > UINT32_MAX) {
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
                !isValidWeightComponent(*uvs))) {
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
        if (!readFloat(positions, index, values, 3)) {
            error = "POSITION accessor read failed";
            return false;
        }
        vertex.position = {values[0], values[1], values[2]};
        if (normals) {
            if (!readFloat(normals, index, values, 3)) {
                error = "NORMAL accessor read failed";
                return false;
            }
            vertex.normal = {values[0], values[1], values[2]};
        }
        if (uvs) {
            if (!readFloat(uvs, index, values, 2)) {
                error = "TEXCOORD_0 accessor read failed";
                return false;
            }
            vertex.uv = {values[0], values[1]};
        }
        if (joints) {
            cgltf_uint jointValues[4]{};
            if (!readUint(joints, index, jointValues, 4) ||
                !readFloat(weights, index, values, 4)) {
                error = "skinning accessor read failed";
                return false;
            }
            vertex.joints = {jointValues[0], jointValues[1], jointValues[2], jointValues[3]};
            vertex.weights = {values[0], values[1], values[2], values[3]};
            if (glm::any(glm::lessThan(vertex.weights, glm::vec4(0)))) { error = "negative skin weight"; return false; }
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
            cgltf_uint value = 0;
            if (!readUint(source.indices, index, &value, 1)) { error = "index read failed"; return false; }
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
    if(!normals) {
        for(std::size_t i=0;i<destination.indices.size();i+=3) {
            auto& a=destination.vertices[destination.indices[i]];auto& b=destination.vertices[destination.indices[i+1]];auto& c=destination.vertices[destination.indices[i+2]];
            const auto normal=glm::cross(b.position-a.position,c.position-a.position);
            a.normal+=normal;b.normal+=normal;c.normal+=normal;
        }
        for(auto& vertex:destination.vertices) {
            const float length=glm::length(vertex.normal);
            vertex.normal=length>0?vertex.normal/length:glm::vec3(0,1,0);
        }
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
                if (!readFloat(source.inverse_bind_matrices, index, values, 16)) {
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
            if (sampler.input->count > std::numeric_limits<std::size_t>::max()/3) { error = "animation count overflow"; return false; }
            const cgltf_size valueCount = sampler.input->count *
                (channel.interpolation == Interpolation::CubicSpline ? 3 : 1);
            if (!sampler.output || sampler.output->type != valueType || sampler.output->count != valueCount ||
                sampler.output->component_type != cgltf_component_type_r_32f || !checkAccessorBounds(*sampler.output, error)) {
                error = "animation output accessor is invalid";
                return false;
            }
            channel.times.resize(sampler.input->count);
            for (cgltf_size index = 0; index < sampler.input->count; ++index) {
                if (!readFloat(sampler.input, index, &channel.times[index], 1) ||
                    (index > 0 && channel.times[index] <= channel.times[index - 1])) {
                    error = "animation keyframe times are invalid";
                    return false;
                }
            }
            channel.values.resize(valueCount);
            for (cgltf_size index = 0; index < valueCount; ++index) {
                float values[4]{};
                if (!readFloat(sampler.output, index, values,
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

static LoadResult loadSourceGltf(const AssetSource& assetSource, const std::filesystem::path& relative, const LoadLimits& limits) {
    try {
    LoadBudget budget{limits};
    const auto modelPath = confinedPath(assetSource, relative);
    ReadContext context{&assetSource, modelPath, &budget};
    const auto bytes = readResource(assetSource, modelPath,budget);
    validateJsonDepth(bytes,limits.jsonDepth);
    AllocationBudget parser(limits.parserBytes);
    cgltf_options options{};
    options.memory.user_data=&parser;
    options.memory.alloc_func=[](void* owner,cgltf_size size) {return AllocationBudget::allocate(size,static_cast<AllocationBudget*>(owner));};
    options.memory.free_func=[](void*,void* pointer) {AllocationBudget::release(pointer);};
    cgltf_data* parsed = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &parsed);
    if (result != cgltf_result_success) return failure(modelPath.generic_u8string() +
        (parser.rejected?": model budget exceeded: parser: ":": glTF parse failed: ") + resultName(result));
    DataHandle data(parsed, &cgltf_free);
    validateBudgets(*data,budget);
    // Own dependencies until conversion finishes; cgltf references these bytes.
    std::vector<std::vector<uint8_t>> buffers(data->buffers_count);
    for (std::size_t i=0;i<data->buffers_count;++i) {
        auto& buffer = data->buffers[i];
        if (buffer.uri) {
            buffers[i] = readUri(context, buffer.uri);
            if (buffers[i].size() < buffer.size) return failure("glTF buffer load failed: data too short");
            buffer.data = buffers[i].data();
        } else if (i == 0 && data->bin && data->bin_size >= buffer.size) buffer.data = const_cast<void*>(data->bin);
        else return failure("glTF buffer load failed: missing buffer");
        buffer.data_free_method = cgltf_data_free_method_none;
    }
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
                {
                const auto* texture = source.pbr_metallic_roughness.base_color_texture.texture;
                material.image = imageIndex(*data, texture->image);
                material.texture = static_cast<int>(texture - data->textures);
                if (source.pbr_metallic_roughness.base_color_texture.texcoord != 0 || source.pbr_metallic_roughness.base_color_texture.has_transform)
                    return failure("only untransformed TEXCOORD_0 base-color textures are supported");
            }
        }
        asset->materials.push_back(material);
    }
    asset->images.reserve(data->images_count);
    for (cgltf_size index = 0; index < data->images_count; ++index) {
        ImageData image;
        const auto& source=data->images[index];
        const std::string location="image["+std::to_string(index)+"]" +
            (source.uri ? " " + (std::strncmp(source.uri,"data:",5)==0 ? std::string("<data URI>") : std::string(std::string_view(source.uri).substr(0,256))) : " buffer view");
        try {
            if (!convertImage(source, context, image, error)) return failure(location+": "+error);
        } catch(const std::exception& exception) {return failure(location+": "+exception.what());}
        asset->images.push_back(std::move(image));
    }

    for (std::size_t i=0;i<data->samplers_count;++i) {
        const auto& sampler = data->samplers[i];
        const int min=static_cast<int>(sampler.min_filter),mag=static_cast<int>(sampler.mag_filter);
        const auto validWrap=[](int wrap){return wrap==10497 || wrap==33071 || wrap==33648;};
        if((min && min!=9728 && min!=9729 && (min<9984 || min>9987)) || (mag && mag!=9728 && mag!=9729) ||
            !validWrap(sampler.wrap_s) || !validWrap(sampler.wrap_t)) return failure("invalid texture sampler");
        asset->samplers.push_back({sampler.min_filter ? static_cast<int>(sampler.min_filter) : 9728,
            sampler.mag_filter ? static_cast<int>(sampler.mag_filter) : 9728,
            static_cast<int>(sampler.wrap_s), static_cast<int>(sampler.wrap_t)});
    }
    for (std::size_t i=0;i<data->textures_count;++i) {
        const auto& texture = data->textures[i];
        if(imageIndex(*data,texture.image)<0) return failure("texture has no supported image");
        asset->textures.push_back({imageIndex(*data, texture.image), texture.sampler ? static_cast<int>(texture.sampler-data->samplers) : -1});
    }
    // GPU upload builds one texture per texture/sampler relation. Count every
    // relation, including repeated references to one decoded image and its mips.
    size_t textureBytes=0;
    const size_t textureCount=asset->textures.empty()?asset->images.size():asset->textures.size();
    for(size_t i=0;i<textureCount;++i) {
        const Texture texture=asset->textures.empty()?Texture{static_cast<int>(i),-1}:asset->textures[i];
        const auto& image=asset->images[texture.image];
        consume(textureBytes,image.pixels.size(),limits.textureBytes,"texture bytes");
        const int filter=texture.sampler>=0?asset->samplers[texture.sampler].minFilter:9728;
        if(filter<9984 || filter>9987) continue;
        size_t w=static_cast<size_t>(image.width),h=static_cast<size_t>(image.height);
        while(w>1 || h>1) {
            w=std::max(size_t{1},w/2);h=std::max(size_t{1},h/2);
            budgetRequire(w<=limits.textureBytes/4/h,"texture mip bytes");
            consume(textureBytes,w*h*4,limits.textureBytes,"texture mip bytes");
        }
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
    const auto* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size index = 0; index < scene->nodes_count; ++index) {
            const int root = pointerIndex(*data, scene->nodes[index]);
            if (root < 0) return failure("scene has an invalid root node");
            asset->sceneRoots.push_back(root);
        }
    } else {
        for (std::size_t index = 0; index < asset->nodes.size(); ++index)
            if (asset->nodes[index].parent < 0) asset->sceneRoots.push_back(static_cast<int>(index));
    }
    if (!convertAnimations(*data, *asset, error)) return failure(std::move(error));

    if (!prepareAsset(*asset, error)) return failure(std::move(error));
    LoadResult success;
    success.asset = std::move(asset);
    for(std::size_t i=0;i<data->extensions_used_count;++i)
        success.warnings.push_back(std::string("optional glTF extension ignored: ")+data->extensions_used[i]);
    return success;
    } catch (const std::exception& error) {
        return failure(relative.generic_u8string() + ": " + error.what());
    }
}

LoadResult loadGltf(const AssetSource& source,const std::filesystem::path& relative,const LoadLimits& limits) {
    auto result=loadSourceGltf(source,relative,limits);
    if(!result) result.error=source.namespaceKey+":"+relative.generic_u8string()+": "+result.error;
    return result;
}

LoadResult loadGltf(const std::filesystem::path& path,const LoadLimits& limits) {
    const auto normalized=path.lexically_normal();
    return loadGltf(AssetSource{"filesystem", normalized.parent_path(), {}}, normalized.filename(),limits);
}

} // namespace model
