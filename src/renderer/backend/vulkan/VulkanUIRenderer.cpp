#include "ui/UIRenderer.h"

#include "core/AssetStore.h"
#include "game/SurvivalRules.h"
#include "game/Utf8.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"

#include <algorithm>
#include <regex>
#include <set>
#include <stb_image.h>
#include <stb_truetype.h>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>

namespace {
TextureData decodeTexture(const std::filesystem::path& path) {
    const auto encoded=AssetStore::readPath(path);
    int width=0,height=0,channels=0;
    stbi_uc* pixels=stbi_load_from_memory(encoded.data(),static_cast<int>(encoded.size()),
                                          &width,&height,&channels,4);
    if(!pixels||width<=0||height<=0){stbi_image_free(pixels);throw std::runtime_error(
        "Could not decode UI texture: "+path.string());}
    TextureData data;data.width=static_cast<uint32_t>(width);
    data.height=static_cast<uint32_t>(height);
    data.pixels.assign(pixels,pixels+static_cast<size_t>(width)*height*4u);
    stbi_image_free(pixels);return data;
}

class VulkanUIBackend final:public IUIRenderBackend{
    static constexpr float FONT_LINE_HEIGHT = 14.0f;
    static constexpr float FONT_RASTER_HEIGHT = 24.0f;
    static constexpr float FONT_RENDER_SCALE =
        FONT_LINE_HEIGHT / FONT_RASTER_HEIGHT;
    struct Batch{std::vector<UiMeshVertex> vertices;std::vector<uint32_t> indices;
        RenderMaterialHandle material{};};
    struct Glyph{float u0=0,v0=0,u1=0,v1=0;float width=0,height=0,xoff=0,yoff=0,advance=0;};
public:
    VulkanUIBackend(VulkanRenderer& renderer,RenderTextureHandle blocks,
                    const std::filesystem::path& root):m_renderer(renderer),
                    m_blockAtlasTilesPerSide(renderer.blockAtlasTilesPerSide()){
        if(m_blockAtlasTilesPerSide<=0)
            throw std::runtime_error("Vulkan block atlas is not initialized");
        TextureData white;white.width=white.height=1;white.pixels={255,255,255,255};
        m_whiteTexture=m_renderer.createTexture(white,{});
        m_whiteMaterial=material(m_whiteTexture);
        m_blockMaterial=material(blocks);
        loadItems(root);
        loadFont(root);
    }
    ~VulkanUIBackend()override{
        m_renderer.waitIdle();
        if(m_fontMaterial)m_renderer.destroyMaterial(m_fontMaterial);
        if(m_itemMaterial)m_renderer.destroyMaterial(m_itemMaterial);
        if(m_blockMaterial)m_renderer.destroyMaterial(m_blockMaterial);
        if(m_whiteMaterial)m_renderer.destroyMaterial(m_whiteMaterial);
        if(m_fontTexture)m_renderer.destroyTexture(m_fontTexture);
        if(m_itemTexture)m_renderer.destroyTexture(m_itemTexture);
        if(m_whiteTexture)m_renderer.destroyTexture(m_whiteTexture);
    }
    void beginUIFrame(int width,int height)override{
        m_batches.clear();
        const float fullW=m_canvasSize.x>0?m_canvasSize.x:static_cast<float>(width);
        const float fullH=m_canvasSize.y>0?m_canvasSize.y:static_cast<float>(height);
        m_projection=clipSpaceCorrection(GraphicsApi::Vulkan)*glm::ortho(
            -m_canvasOrigin.x,fullW-m_canvasOrigin.x,-m_canvasOrigin.y,
            fullH-m_canvasOrigin.y);
    }
    void setCanvas(float x,float y,float w,float h)override{
        m_canvasOrigin={x,y};m_canvasSize={w,h};
    }
    void endUIFrame()override{
        for(const Batch& batch:m_batches)
            m_renderer.queueUiBatch(batch.vertices,batch.indices,batch.material,m_projection);
    }
    void drawRect(float x,float y,float w,float h,const glm::vec4& color)override{
        quad(x,y,w,h,{0,0,1,1},color,m_whiteMaterial);
    }
    void drawBlockIcon(float x,float y,float w,float h,BlockId block)override{
        if (block == BlockId::AIR) return;
        FaceDir face = FaceDir::TOP;
        if(block==BlockId::WOOD||block==BlockId::BIRCH_WOOD||
           block==BlockId::SPRUCE_WOOD||block==BlockId::JUNGLE_WOOD||
           block==BlockId::ACACIA_WOOD||getBlockProps(block).shape==RenderShape::Cross)
            face=FaceDir::FRONT;
        const int tile=static_cast<int>(getFaceTextureIndex(block,face));
        const int side=m_blockAtlasTilesPerSide;const float s=static_cast<float>(side);
        const float inset=.5f/(16.0f*s);
        quad(x,y,w,h,{tile%side/s+inset,tile/side/s+inset,
             (tile%side+1)/s-inset,(tile/side+1)/s-inset},glm::vec4(1),m_blockMaterial);
    }
    void drawItemIcon(float x,float y,float w,float h,const ItemStack& stack)override{
        if(stack.empty())return;
        std::string name=getItemProps(stack.id).name;
        for(char& c:name)c=std::isalnum(static_cast<unsigned char>(c))?
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))):'_';
        if(const auto found=m_itemIndices.find(name);found!=m_itemIndices.end()&&m_itemMaterial){
            const int tile=found->second;const float cols=static_cast<float>(m_itemColumns);
            const float rows=static_cast<float>(m_itemRows);
            const float u0=tile%m_itemColumns/cols;
            const float v0=tile/m_itemColumns/rows;
            const float u1=(tile%m_itemColumns+1)/cols;
            const float v1=(tile/m_itemColumns+1)/rows;
            quad(x,y,w,h,{u0,v1,u1,v0},
                glm::vec4(1),m_itemMaterial);return;
        }
        const auto& props=getItemProps(stack.id);
        if(props.placedBlock){drawBlockIcon(x,y,w,h,*props.placedBlock);return;}
        drawRect(x+w*.24f,y+h*.24f,w*.52f,h*.52f,{.72f,.72f,.72f,1});
    }
    void drawDurability(float x,float y,float w,const ItemStack& stack)override{
        const auto& props=stack.empty()?getItemProps(ItemId::EMPTY):getItemProps(stack.id);
        if (!props.maxDurability || !stack.damage) return;
        const float value = durabilityRemaining(stack);
        drawRect(x,y,w,4,{.02f,.02f,.02f,.95f});
        drawRect(x+1,y+1,(w-2)*value,2,{1-value,value,.08f,1});
    }
    void drawPanel(float x,float y,float w,float h,const glm::vec4& fill)override{
        drawRect(x,y,w,h,{.02f,.02f,.025f,fill.a});
        drawRect(x+2,y+2,w-4,h-4,{.48f,.48f,.52f,fill.a});
        drawRect(x+4,y+4,w-8,h-8,fill);
    }
    void drawTooltip(float x,float y,const ItemStack& stack)override{
        if (stack.empty()) return;
        const auto& props = getItemProps(stack.id);
        std::string text=m_localization?m_localization->itemName(stack.id):props.name;
        if(stack.count>1)text+=" x"+std::to_string(stack.count);
        const auto size=measureText(text,.9f);drawPanel(x,y,size.x+14,size.y+12,{.08f,.05f,.12f,.97f});
        renderText(text,x+7,y+6,.9f,{.95f,.90f,1});
    }
    void renderText(const std::string& text,float x,float y,float scale,
                    const glm::vec3& color)override{
        float cursor=x;for(uint32_t cp:decodeUtf8(text)){
            if(cp=='\n'){cursor=x;y-=FONT_LINE_HEIGHT*scale;continue;}
            const auto found=m_glyphs.find(cp);if(found==m_glyphs.end())continue;
            const Glyph& g=found->second;
            const float renderedHeight = g.height * FONT_RENDER_SCALE;
            if(g.width>0&&g.height>0)quad(
                cursor + g.xoff * FONT_RENDER_SCALE * scale,
                y + (FONT_LINE_HEIGHT - renderedHeight) * 0.5f * scale,
                g.width * FONT_RENDER_SCALE * scale,
                renderedHeight * scale,{g.u0,g.v0,g.u1,g.v1},
                glm::vec4(color,1),m_fontMaterial);
            cursor+=g.advance*FONT_RENDER_SCALE*scale;
        }
    }
    glm::vec2 measureText(const std::string& text,float scale)override{
        float width=0,line=0;int lines=1;for(uint32_t cp:decodeUtf8(text)){
            if(cp=='\n'){width=std::max(width,line);line=0;++lines;continue;}
            if(const auto found=m_glyphs.find(cp);found!=m_glyphs.end())
                line+=found->second.advance*FONT_RENDER_SCALE*scale;
        }return {std::max(width,line),FONT_LINE_HEIGHT*scale*lines};
    }
    void setLocalization(const Localization* value)override{m_localization=value;}
private:
    VulkanRenderer& m_renderer;const Localization* m_localization=nullptr;
    glm::vec2 m_canvasOrigin{0},m_canvasSize{0};glm::mat4 m_projection{1};
    std::vector<Batch> m_batches;
    RenderTextureHandle m_whiteTexture{},m_itemTexture{},m_fontTexture{};
    RenderMaterialHandle m_whiteMaterial{},m_blockMaterial{},m_itemMaterial{},m_fontMaterial{};
    int m_blockAtlasTilesPerSide=0,m_itemColumns=0,m_itemRows=0;
    std::unordered_map<std::string,int>m_itemIndices;
    std::unordered_map<uint32_t,Glyph>m_glyphs;
    RenderMaterialHandle material(RenderTextureHandle texture){MaterialDesc d;
        d.pipeline=MaterialPipeline::UiTextured;d.baseColorTexture=texture;
        return m_renderer.createMaterial(d);}
    Batch& batch(RenderMaterialHandle value){
        if(m_batches.empty()||!(m_batches.back().material==value))m_batches.push_back({{}, {}, value});
        return m_batches.back();
    }
    void quad(float x,float y,float w,float h,const glm::vec4& uv,
              const glm::vec4& color,RenderMaterialHandle materialHandle){
        Batch& b=batch(materialHandle);const uint32_t base=static_cast<uint32_t>(b.vertices.size());
        b.vertices.insert(b.vertices.end(),{{{x,y},{uv.x,uv.y},color},
            {{x+w,y},{uv.z,uv.y},color},{{x+w,y+h},{uv.z,uv.w},color},
            {{x,y+h},{uv.x,uv.w},color}});
        b.indices.insert(b.indices.end(),{base,base+1,base+2,base,base+2,base+3});
    }
    void loadItems(const std::filesystem::path& root){
        const std::string metadata=AssetStore::readTextPath(root/"textures/generated/items_atlas.json");
        std::smatch match;if(std::regex_search(metadata,match,std::regex(R"("columns"\s*:\s*(\d+))")))m_itemColumns=std::stoi(match[1]);
        if(std::regex_search(metadata,match,std::regex(R"("rows"\s*:\s*(\d+))")))m_itemRows=std::stoi(match[1]);
        const std::regex entry(R"REGEX("([^"]+)"\s*:\s*\{[^{}]*"index"\s*:\s*(\d+))REGEX");
        for(std::sregex_iterator it(metadata.begin(),metadata.end(),entry),end;it!=end;++it)
            m_itemIndices[(*it)[1].str()]=std::stoi((*it)[2]);
        TextureSamplerDesc sampler;sampler.addressU=sampler.addressV=TextureAddressMode::ClampToEdge;
        m_itemTexture=m_renderer.createTexture(decodeTexture(root/"textures/generated/items_atlas.png"),sampler);
        m_itemMaterial=material(m_itemTexture);
    }
    void loadFont(const std::filesystem::path& root){
        constexpr int atlasSize=2048;
        constexpr int pixelHeight=static_cast<int>(FONT_RASTER_HEIGHT);
        const auto bytes=AssetStore::readPath(root/"fonts/noto/NotoSansCJKsc-Regular.otf");
        stbtt_fontinfo font{};if(bytes.empty()||!stbtt_InitFont(&font,bytes.data(),0))return;
        std::set<uint32_t> codepoints;for(uint32_t cp=32;cp<127;++cp)codepoints.insert(cp);
        for(const char* file:{"en_us.json","zh_cn.json"}){
            const std::string text=AssetStore::readTextPath(root/"lang"/file);
            for(uint32_t cp:decodeUtf8(text))if(cp>=32)codepoints.insert(cp);
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(atlasSize)*atlasSize*4u,0);
        const float fontScale=stbtt_ScaleForPixelHeight(&font,pixelHeight);
        int shelfX=1,shelfY=1,shelfHeight=0;
        for(uint32_t cp:codepoints){int advance=0,bearing=0;
            stbtt_GetCodepointHMetrics(&font,static_cast<int>(cp),&advance,&bearing);
            int x0=0,y0=0,x1=0,y1=0;stbtt_GetCodepointBitmapBox(&font,static_cast<int>(cp),
                fontScale,fontScale,&x0,&y0,&x1,&y1);const int w=std::max(0,x1-x0),h=std::max(0,y1-y0);
            if(shelfX+w+2>=atlasSize){shelfX=1;shelfY+=shelfHeight+2;shelfHeight=0;}
            if(shelfY+h+2>=atlasSize)break;
            if(w&&h){std::vector<uint8_t> bitmap(static_cast<size_t>(w)*h);
                stbtt_MakeCodepointBitmap(&font,bitmap.data(),w,h,w,fontScale,fontScale,static_cast<int>(cp));
                for(int py=0;py<h;++py)for(int px=0;px<w;++px){const uint8_t a=bitmap[py*w+px];
                    const size_t dst=(static_cast<size_t>(shelfY+py)*atlasSize+shelfX+px)*4u;
                    pixels[dst]=pixels[dst+1]=pixels[dst+2]=255;pixels[dst+3]=a;}}
            m_glyphs[cp]={shelfX/static_cast<float>(atlasSize),(shelfY+h)/static_cast<float>(atlasSize),
                (shelfX+w)/static_cast<float>(atlasSize),shelfY/static_cast<float>(atlasSize),
                static_cast<float>(w),static_cast<float>(h),static_cast<float>(x0),
                static_cast<float>(-y0),advance*fontScale};
            shelfX+=w+2;shelfHeight=std::max(shelfHeight,h);
        }
        TextureData data;data.width=data.height=atlasSize;data.pixels=std::move(pixels);
        TextureSamplerDesc sampler;sampler.addressU=sampler.addressV=TextureAddressMode::ClampToEdge;
        m_fontTexture=m_renderer.createTexture(data,sampler);m_fontMaterial=material(m_fontTexture);
    }
};
}

std::unique_ptr<IUIRenderBackend> createVulkanUIBackend(
    IGameRenderer& renderer,RenderTextureHandle blocks,const std::filesystem::path& root){
    auto* vulkan=dynamic_cast<VulkanRenderer*>(&renderer);
    if(!vulkan)throw std::invalid_argument("Vulkan UI requires VulkanRenderer");
    return std::make_unique<VulkanUIBackend>(*vulkan,blocks,root);
}
