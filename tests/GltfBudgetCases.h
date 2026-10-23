// Small fixtures exercise allocation rejection without exhausting the machine.
#include "core/AllocationBudget.h"

void budgetCases() {
    const auto binary=modelBuffer(0);
    const auto json=modelJson(binary.size(),true,false,false,false);
    std::map<std::filesystem::path,std::vector<uint8_t>> files;
    const auto reset=[&] {files.clear();files["model.gltf"]={json.begin(),json.end()};files["external.bin"]=binary;};
    reset();
    size_t copies=0;
    model::AssetSource source{"budget-fixture","virtual",[&](const auto& path,size_t maximum) {
        const auto& data=files.at(path);
        if(data.size()>maximum) throw std::runtime_error("fixture byte budget exceeded before copy");
        ++copies;return data;
    }};
    const auto reject=[&](const model::LoadLimits& limits,const char* label) {
        const auto result=model::loadGltf(source,"model.gltf",limits);
        if(result || !contains(result.error,label)) std::cerr<<"Expected "<<label<<": "<<result.error<<'\n';
        require(!result && contains(result.error,label),"budget rejection missing/incorrect");
    };
    require(bool(model::loadGltf(source,"model.gltf")),"budget baseline invalid");
    model::LoadLimits limits;
    copies=0;limits.fileBytes=json.size()-1;reject(limits,"byte budget");
    require(copies==0,"oversized source was copied before rejection");
    limits={};limits.encodedBytes=json.size()+binary.size()-1;reject(limits,"byte budget");
    limits={};limits.parserBytes=1;reject(limits,"parser");
    limits={};limits.decoderBytes=1;reject(limits,"image decoder");
    limits={};limits.decodedBytes=3;reject(limits,"total decoded");
    limits={};limits.imagePixels=0;reject(limits,"image pixels");
    limits={};limits.imageDimension=0;reject(limits,"image dimensions");
    limits={};limits.convertedBytes=1;reject(limits,"converted bytes");
    limits={};limits.textureBytes=3;reject(limits,"texture bytes");
    limits={};limits.primitiveVertices=2;reject(limits,"primitive vertices");
    limits={};limits.vertices=2;reject(limits,"total vertices");
    limits={};limits.primitiveIndices=2;reject(limits,"primitive indices");
    limits={};limits.indices=2;reject(limits,"total indices");
    limits={};limits.primitives=0;reject(limits,"meshes");
    limits={};limits.nodes=1;reject(limits,"nodes");
    limits={};limits.skins=0;reject(limits,"skins");
    limits={};limits.images=0;reject(limits,"images");
    limits={};limits.materials=0;reject(limits,"materials");
    limits={};limits.textures=0;reject(limits,"textures");
    limits={};limits.accessors=0;reject(limits,"accessors");
    limits={};limits.buffers=0;reject(limits,"buffers");
    limits={};limits.bufferViews=0;reject(limits,"buffer views");
    limits={};limits.bindings=0;reject(limits,"draw bindings");
    limits={};limits.boundVertices=2;reject(limits,"bound vertices");

    const auto put=[&](const std::string& text){files["model.gltf"]={text.begin(),text.end()};};
    auto withSampler=json;replaceOnce(withSampler,"\"textures\":[","\"samplers\":[{}],\"textures\":[");put(withSampler);
    limits={};limits.samplers=0;reject(limits,"samplers");
    put(R"({"asset":{"version":"2.0"},"meshes":[{"primitives":[{},{}]}]})");
    limits={};limits.primitives=1;reject(limits,"primitives");
    auto roots=json;replaceOnce(roots,"\"scenes\":[{\"nodes\":[0,2]}]","\"scenes\":[{\"nodes\":[0,2,0,2]}]");put(roots);
    limits={};limits.nodes=3;reject(limits,"scene roots");
    put("{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\""+std::string(2048,'X')+"\"]}");
    limits={};limits.convertedBytes=1024;reject(limits,"converted bytes");
    const auto animationBinary=animatedBuffer();files["external.bin"]=animationBinary;
    const auto animated=modelJson(animationBinary.size(),true,false,false,false,true);
    put(animated);
    require(bool(model::loadGltf(source,"model.gltf")),"animated budget baseline invalid");
    limits={};limits.animations=0;reject(limits,"animations");
    limits={};limits.channels=0;reject(limits,"animation channels");
    limits={};limits.keyframes=1;reject(limits,"animation keyframes");
    auto repeated=animated;
    // Sharing sampler data still duplicates converted per-channel arrays.
    const auto start=repeated.find("\"channels\":[{");
    require(start!=std::string::npos,"animation channel fixture missing");
    const auto finish=repeated.find("}]",start);
    const auto channel=repeated.substr(start+12,finish+1-(start+12));
    limits={};limits.keyframes=6;
    require(bool(model::loadGltf(source,"model.gltf",limits)),"exact keyframe budget rejected");
    repeated.insert(finish+1,","+channel);put(repeated);
    reject(limits,"animation keyframes");

    reset();auto imageJson=json;
    replaceOnce(imageJson,"\"bufferView\":7,\"mimeType\":\"image/png\"","\"uri\":\"color.png\"");
    std::vector<uint8_t> png(std::begin(PNG_1X1_RGBA),std::end(PNG_1X1_RGBA));
    // Only the PNG header changes; info preflight rejects before IDAT decoding.
    png[16]=0;png[17]=0;png[18]=0x20;png[19]=0;
    files["color.png"]=png;put(imageJson);limits={};reject(limits,"image dimensions");
    png.assign(std::begin(PNG_1X1_RGBA),std::end(PNG_1X1_RGBA));files["color.png"]=png;
    auto twoImages=imageJson;replaceOnce(twoImages,"\"images\":[{\"uri\":\"color.png\"}]","\"images\":[{\"uri\":\"color.png\"},{\"uri\":\"color.png\"}]");put(twoImages);
    limits={};limits.decodedBytes=4;reject(limits,"total decoded bytes");
    auto twoTextures=imageJson;replaceOnce(twoTextures,"\"textures\":[{\"source\":0}]","\"textures\":[{\"source\":0},{\"source\":0}]");put(twoTextures);
    limits={};limits.textureBytes=4;reject(limits,"texture bytes");
    // Original solid-red2x2 PNG, generated using Python zlib/struct.
    files["color.png"]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,17,73,68,65,84,120,156,99,248,207,192,240,31,132,25,96,12,0,71,202,7,249,103,89,110,183,0,0,0,0,73,69,78,68,174,66,96,130};
    auto mipJson=imageJson;replaceOnce(mipJson,"\"textures\":[{\"source\":0}]","\"samplers\":[{\"minFilter\":9987}],\"textures\":[{\"source\":0,\"sampler\":0}]");put(mipJson);
    limits={};limits.textureBytes=19;reject(limits,"texture mip bytes");
    limits.textureBytes=20;require(bool(model::loadGltf(source,"model.gltf",limits)),"exact mip budget rejected");
    files["color.png"]=png;
    auto sparse=json;
    replaceOnce(sparse,"\"bufferView\":0,\"componentType\":5126,\"count\":3", "\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":5,\"componentType\":5123},\"values\":{\"bufferView\":0}},\"componentType\":5126,\"count\":1000000");
    put(sparse);limits={};limits.primitiveVertices=32;reject(limits,"primitive vertices");
    auto embedded=json;replaceOnce(embedded,"external.bin","data:application/octet-stream;base64,"+fixtureBase64(binary));put(embedded);
    limits={};limits.encodedBytes=embedded.size()+binary.size()-1;reject(limits,"data URI bytes");
    limits={};limits.encodedBytes=embedded.size()+binary.size();
    require(bool(model::loadGltf(source,"model.gltf",limits)),"exact base64 byte budget rejected");
    put("{\"asset\":{\"version\":\"2.0\"},\"extras\":"+std::string(70,'[')+"0"+std::string(70,']')+"}");
    limits={};reject(limits,"JSON nesting");

    const auto path=std::filesystem::temp_directory_path()/"minecraftc-budget-model.gltf";
    writeText(path,json);limits={};limits.fileBytes=json.size()-1;
    const auto fileResult=model::loadGltf(path,limits);
    require(!fileResult && contains(fileResult.error,"byte budget"),"filesystem read was not bounded");
    std::filesystem::remove(path);

    AllocationBudget allocation(256);
    void* p=AllocationBudget::allocate(64,&allocation);
    require(p && allocation.live>=64,"allocation budget did not track memory");
    require(!AllocationBudget::resize(p,256,&allocation),"realloc exceeded budget");
    AllocationBudget::release(p);require(allocation.live==0,"allocation budget leaked after failed realloc");

    // Deterministic bounded mutation fuzz: glTF JSON, PNG and GLB corpus; every
    // iteration has tiny source/decoder/conversion ceilings, no unbounded reads.
    limits={};limits.fileBytes=32768;limits.encodedBytes=65536;limits.parserBytes=1024*1024;
    limits.decoderBytes=65536;limits.convertedBytes=65536;limits.decodedBytes=4096;
    limits.imageDimension=16;limits.imagePixels=256;limits.primitiveVertices=32;limits.vertices=64;
    limits.primitiveIndices=96;limits.indices=192;limits.nodes=32;limits.channels=32;limits.keyframes=128;
    std::vector<uint8_t> glbJson(json.begin(),json.end());while(glbJson.size()%4)glbJson.push_back(' ');
    auto paddedBinary=binary;align4(paddedBinary);
    std::vector<uint8_t> glb;
    appendU32(glb,0x46546c67);appendU32(glb,2);appendU32(glb,static_cast<uint32_t>(28+glbJson.size()+paddedBinary.size()));
    appendU32(glb,static_cast<uint32_t>(glbJson.size()));appendU32(glb,0x4e4f534a);glb.insert(glb.end(),glbJson.begin(),glbJson.end());
    appendU32(glb,static_cast<uint32_t>(paddedBinary.size()));appendU32(glb,0x004e4942);glb.insert(glb.end(),paddedBinary.begin(),paddedBinary.end());
    uint32_t random=0x984ad821u;
    for(size_t i=0;i<512;++i) {
        reset();
        auto corpus=i%3==0?glb:std::vector<uint8_t>(imageJson.begin(),imageJson.end());
        auto image=png;
        for(size_t n=0;n<1+i%4;++n) {
            random=random*1664525u+1013904223u;
            auto& target=i%3==2?image:corpus;
            target[random%target.size()]^=static_cast<uint8_t>((random>>16)|1);
        }
        files["model.gltf"]=corpus;files["color.png"]=image;
        const auto result=model::loadGltf(source,"model.gltf",limits);
        if(result) {
            size_t vertexCount=0;for(const auto& primitive:result.asset->primitives)vertexCount+=primitive.vertices.size();
            require(vertexCount<=limits.vertices && result.asset->nodes.size()<=limits.nodes,"fuzz result exceeded limits");
        } else require(!result.error.empty(),"fuzz failure lost diagnostic");
    }
    reset();require(bool(model::loadGltf(source,"model.gltf")),"decoder/parser scopes leaked after rejection");
}
