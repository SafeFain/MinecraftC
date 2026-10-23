// Included by GltfLoaderTests.cpp to reuse its GLB fixture builders.
#include "model/ModelTexture.h"
#include <limits>

std::string fixtureBase64(const std::vector<uint8_t>& bytes) {
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for(size_t i=0;i<bytes.size();i+=3) {
        const uint32_t value=(uint32_t(bytes[i])<<16)|(i+1<bytes.size()?uint32_t(bytes[i+1])<<8:0)|(i+2<bytes.size()?bytes[i+2]:0);
        output+=alphabet[(value>>18)&63];output+=alphabet[(value>>12)&63];
        output+=i+1<bytes.size()?alphabet[(value>>6)&63]:'=';output+=i+2<bytes.size()?alphabet[value&63]:'=';
    }
    return output;
}

void upgradedCases() {
    auto bytes=modelBuffer(0);
    auto json=modelJson(bytes.size(),true,false,false,false);
    std::map<std::filesystem::path,std::vector<uint8_t>> files;
    const auto putJson=[&](const std::string& text) { files["models/model.gltf"]={text.begin(),text.end()}; };
    files["models/external.bin"]=bytes;
    files["textures/color.png"]={std::begin(PNG_1X1_RGBA),std::end(PNG_1X1_RGBA)};
    int reads=0;
    model::AssetSource source{"fixture","pack",[&](const auto& path,size_t maximum){
        ++reads;const auto& data=files.at(path);
        if(data.size()>maximum) throw std::runtime_error("fixture byte budget exceeded");
        return data;
    }};
    auto external=json;
    replaceOnce(external,"\"bufferView\":7,\"mimeType\":\"image/png\"","\"uri\":\"../textures/color.png\"");
    replaceOnce(external,"\"textures\":[{\"source\":0}]","\"samplers\":[{\"minFilter\":9987,\"magFilter\":9729,\"wrapS\":33648,\"wrapT\":33071}],\"textures\":[{\"source\":0,\"sampler\":0}]");
    putJson(external);
    auto loaded=model::loadGltf(source,"models/model.gltf");
    require(loaded && loaded.asset->samplers[0].minFilter==9987 && loaded.asset->textures[0].sampler==0,
            "rooted virtual source or external image/sampler failed");

    // Original one-pixel RGB JPEG, generated with Pillow for this fixture.
    const std::vector<uint8_t> jpeg={255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,255,219,0,67,0,8,6,6,7,6,5,8,7,7,7,9,9,8,10,12,20,13,12,11,11,12,25,18,19,15,20,29,26,31,30,29,26,28,28,32,36,46,39,32,34,44,35,28,28,40,55,41,44,48,49,52,52,52,31,39,57,61,56,50,60,46,51,52,50,255,219,0,67,1,9,9,9,12,11,12,24,13,13,24,50,33,28,33,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,255,192,0,17,8,0,1,0,1,3,1,34,0,2,17,1,3,17,1,255,196,0,31,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,16,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,49,65,6,19,81,97,7,34,113,20,50,129,145,161,8,35,66,177,193,21,82,209,240,36,51,98,114,130,9,10,22,23,24,25,26,37,38,39,40,41,42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,225,226,227,228,229,230,231,232,233,234,241,242,243,244,245,246,247,248,249,250,255,196,0,31,1,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,255,196,0,181,17,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119,0,1,2,3,17,4,5,33,49,6,18,65,81,7,97,113,19,34,50,129,8,20,66,145,161,177,193,9,35,51,82,240,21,98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,40,41,42,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,226,227,228,229,230,231,232,233,234,242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,2,17,3,17,0,63,0,167,69,20,87,201,159,106,127,255,217};
    files["textures/color.jpg"]=jpeg;
    auto jpegJson=external;replaceOnce(jpegJson,"../textures/color.png","../textures/color.jpg");putJson(jpegJson);
    const auto jpegModel=model::loadGltf(source,"models/model.gltf");
    require(jpegModel && jpegModel.asset->images[0].width==1 && jpegModel.asset->images[0].pixels[0]>150,"external JPEG failed");
    auto embedded=external;
    replaceOnce(embedded,"../textures/color.png","data:image/png;base64,"+fixtureBase64(files["textures/color.png"]));
    replaceOnce(embedded,"external.bin","data:application/octet-stream;base64,"+fixtureBase64(bytes));
    putJson(embedded);
    require(bool(model::loadGltf(source,"models/model.gltf")),"data URI buffer/image failed");
    auto unsafe=external;replaceOnce(unsafe,"../textures/color.png","../../outside.png");putJson(unsafe);
    require(!model::loadGltf(source,"models/model.gltf"),"dependency escaped declared source root");
    unsafe=external;replaceOnce(unsafe,"../textures/color.png","%2e%2e/%2e%2e/outside.png");putJson(unsafe);
    require(!model::loadGltf(source,"models/model.gltf"),"encoded dependency escaped source root");
    putJson(external);
    model::ModelAssetCache cache;
    const auto first=cache.load(source,"models/model.gltf").asset;
    const auto count=reads;
    cache.alias("registered-model.glb",cache.load(source,"models/model.gltf"));
    require(cache.load("registered-model.glb").asset==first,"package model cache alias reloaded the asset");
    require(cache.load(source,"models/model.gltf").asset==first && reads==count,"shared source cache reloaded an asset");
    const auto& failed=cache.load(source,"models/missing.gltf");const auto failedReads=reads;
    require(!failed && !cache.load(source,"models/missing.gltf") && reads==failedReads,"failed cache entry lost its diagnostics");
    auto other=source;other.namespaceKey="other";
    require(cache.load(other,"models/model.gltf").asset!=first,"asset namespace cache collision");

    auto normalized=json;
    replaceOnce(normalized,"\"bufferView\":2,\"componentType\":5126","\"bufferView\":2,\"componentType\":5121,\"normalized\":true");
    replaceOnce(normalized,"\"bufferView\":4,\"componentType\":5126","\"bufferView\":4,\"componentType\":5121,\"normalized\":true");
    auto normalizedBytes=bytes;
    const uint8_t uv[]={0,0,255,0,0,255};std::copy(std::begin(uv),std::end(uv),normalizedBytes.begin()+72);
    for(int i=0;i<12;++i) normalizedBytes[120+i]=i%4==0?255:0;
    files["models/external.bin"]=normalizedBytes;putJson(normalized);
    loaded=model::loadGltf(source,"models/model.gltf");
    require(loaded && loaded.asset->primitives[0].vertices[1].uv.x==1 && loaded.asset->primitives[0].vertices[2].weights.x==1,
            "normalized UV/weights conversion failed");

    auto sparseBytes=bytes;align4(sparseBytes);
    const auto indexOffset=sparseBytes.size();sparseBytes.push_back(1);align4(sparseBytes);
    const auto valueOffset=sparseBytes.size();for(float v:{9.f,0.f,0.f}) appendFloat(sparseBytes,v);
    auto sparse=modelJson(sparseBytes.size(),true,false,false,false);
    const std::string addedViews=",{\"buffer\":0,\"byteOffset\":"+std::to_string(indexOffset)+",\"byteLength\":1},{\"buffer\":0,\"byteOffset\":"+std::to_string(valueOffset)+",\"byteLength\":12}";
    replaceOnce(sparse,"}],\"accessors\"","}"+addedViews+"],\"accessors\"");
    replaceOnce(sparse,"\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"",
        "\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":8,\"componentType\":5121},\"values\":{\"bufferView\":9}}");
    files["models/external.bin"]=sparseBytes;putJson(sparse);
    loaded=model::loadGltf(source,"models/model.gltf");
    require(loaded && loaded.asset->primitives[0].vertices[1].position.x==9,"sparse overlay failed");
    auto sparseZero=sparse;replaceOnce(sparseZero,"\"bufferView\":0,\"componentType\":5126","\"componentType\":5126");putJson(sparseZero);
    loaded=model::loadGltf(source,"models/model.gltf");
    require(loaded && loaded.asset->primitives[0].vertices[0].position==glm::vec3(0) && loaded.asset->primitives[0].vertices[1].position.x==9,
            "sparse zero base failed");
    sparseBytes[indexOffset]=3;files["models/external.bin"]=sparseBytes;putJson(sparse);
    require(!model::loadGltf(source,"models/model.gltf"),"sparse out-of-range index accepted");
    files["models/external.bin"]=bytes;
    auto inactive=json;replaceOnce(inactive,"\"nodes\":[0,2]","\"nodes\":[2]");putJson(inactive);
    loaded=model::loadGltf(source,"models/model.gltf");
    require(loaded && loaded.asset->bindings.empty(),"inactive scene geometry was rendered");
    auto cycle=json;replaceOnce(cycle,"\"name\":\"mesh\",\"mesh\":0","\"name\":\"mesh\",\"children\":[0],\"mesh\":0");putJson(cycle);
    require(!model::loadGltf(source,"models/model.gltf"),"node hierarchy cycle accepted");
    auto nanBytes=bytes;const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(nanBytes.data(),&nan,4);
    files["models/external.bin"]=nanBytes;putJson(json);
    require(!model::loadGltf(source,"models/model.gltf"),"nonfinite vertex accepted");
    files["models/external.bin"]=bytes;
    for(int joints:{64,65,256,257}) {
        auto skeleton=json;
        replaceOnce(skeleton,",\"inverseBindMatrices\":6","");
        std::string list="[";for(int i=0;i<joints;++i) list+=(i?",":"")+std::to_string(i);list+="]";
        replaceOnce(skeleton,"\"joints\":[0,1]","\"joints\":"+list);
        const auto begin=skeleton.find("\"nodes\":[{");const auto end=skeleton.find("],\"skins\"",begin);
        std::string nodes="\"nodes\":[{\"mesh\":0,\"skin\":0}";for(int i=1;i<joints;++i) nodes+=",{}";nodes+="]";
        skeleton.replace(begin,end+1-begin,nodes);putJson(skeleton);
        loaded=model::loadGltf(source,"models/model.gltf");
        require(bool(loaded)==(joints<=256),"256-joint boundary incorrect");
    }
    model::ImageData image;image.width=image.height=2;image.pixels={255,0,0,255,0,0,255,0,255,0,0,255,0,0,255,0};
    model::Sampler sampler;sampler.minFilter=9987;
    const auto mip=model::modelTexture(image,sampler);
    require(mip.mipLevels.size()==1 && mip.mipLevels[0].pixels[0]==255 && mip.mipLevels[0].pixels[2]==0 && mip.mipLevels[0].pixels[3]==128,
            "model mip alpha-weighted linear filtering failed");
}
