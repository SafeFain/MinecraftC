#include "plugins/ContentQueries.h"
#include "renderer/BlockAtlasData.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "ui/UIGeometry.h"
#include "ui/UIColor.h"

#include "core/AssetStore.h"
#include "game/Language.h"
#include "game/SurvivalRules.h"
#include "core/Utf8.h"
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
    static constexpr int FONT_ATLAS_SIZE = 2048;
    static constexpr float FONT_LINE_HEIGHT = 14.0f;
    static constexpr float FONT_RASTER_HEIGHT = 36.0f;
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
        releaseRetiredFonts();
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
        if(!m_retiredFonts.empty()) {
            m_renderer.waitIdle();
            releaseRetiredFonts();
        }
        m_uiWidth=width;m_uiHeight=height;
        const float fullW=m_canvasSize.x>0?m_canvasSize.x:static_cast<float>(width);
        const float fullH=m_canvasSize.y>0?m_canvasSize.y:static_cast<float>(height);
        m_projection=clipSpaceCorrection()*glm::ortho(
            -m_canvasOrigin.x,fullW-m_canvasOrigin.x,-m_canvasOrigin.y,
            fullH-m_canvasOrigin.y);
    }
    int canvasWidth() const { return m_uiWidth; }
    int canvasHeight() const { return m_uiHeight; }
    void setCanvas(float x,float y,float w,float h)override{
        m_canvasOrigin={x,y};m_canvasSize={w,h};
    }
    UiFrameStats frameStats() const override {
        UiFrameStats stats;stats.batches=m_batches.size();
        for (const auto& b:m_batches) { stats.vertices+=b.vertices.size();stats.indices+=b.indices.size(); }
        return stats;
    }
    void endUIFrame()override{
        uploadFont();
        for(const Batch& batch:m_batches)
            m_renderer.queueUiBatch(batch.vertices,batch.indices,batch.material,m_projection);
    }
    void drawRect(float x,float y,float w,float h,const glm::vec4& color)override{
        quad(x,y,w,h,{0,0,1,1},color,m_whiteMaterial);
    }
    void setOpacity(float opacity) override { m_opacity = std::clamp(opacity, 0.0f, 1.0f); }
    void drawRoundedRect(float x,float y,float w,float h,float radius,
                         const glm::vec4& color) override {
        Batch& b = batch(m_whiteMaterial);
        const glm::vec4 tinted=uiLinearColor(color,m_opacity);
        appendRoundedRect(b.vertices,b.indices,x,y,w,h,radius,tinted);
    }
    void drawAtlasIcon(float x,float y,float w,float h,uint16_t tile,const glm::vec4& color)override{
        const int side=m_blockAtlasTilesPerSide;const float s=static_cast<float>(side);
        const float inset=.5f/(16.0f*s);
        quad(x,y,w,h,{tile%side/s+inset,tile/side/s+inset,(tile%side+1)/s-inset,(tile/side+1)/s-inset},color,m_blockMaterial);
    }
    void drawBlockIcon(float x,float y,float w,float h,BlockId block)override{
        if (block == BlockId::AIR) return;
        FaceDir face = FaceDir::TOP;
        if(block==BlockId::WOOD||block==BlockId::BIRCH_WOOD||
           block==BlockId::SPRUCE_WOOD||block==BlockId::JUNGLE_WOOD||
           block==BlockId::ACACIA_WOOD||getBlockProps(block).shape==RenderShape::Cross||
           getBlockProps(block).shape==RenderShape::CeilingCross)
            face=FaceDir::FRONT;
        const auto shape=getBlockProps(block).shape;
        if(shape==RenderShape::Slab||shape==RenderShape::Stair||shape==RenderShape::Door||shape==RenderShape::Button||shape==RenderShape::Boxes) {
            int horizontal=0;ArchitecturalBlockState architecture;
            if(shape==RenderShape::Stair&&decodeArchitecturalBlock(block,architecture)&&static_cast<uint8_t>(architecture.direction)%2==0)horizontal=2;
            const auto boxes=blockModelBoxes(block);const int tile=static_cast<int>(getFaceTextureIndex(block,horizontal==2?FaceDir::RIGHT:FaceDir::FRONT));
            const float side=static_cast<float>(m_blockAtlasTilesPerSide),inset=.5f/(16*side);
            for(uint8_t i=0;i<boxes.count;++i) {
                const auto& box=boxes.boxes[i];const bool top=shape==RenderShape::Button;
                const float lo=top?box.min.z:box.min.y,hi=top?box.max.z:box.max.y;
                quad(x+w*box.min[horizontal],y+h*lo,w*(box.max[horizontal]-box.min[horizontal]),h*(hi-lo),{tile%m_blockAtlasTilesPerSide/side+inset,tile/m_blockAtlasTilesPerSide/side+inset,(tile%m_blockAtlasTilesPerSide+1)/side-inset,(tile/m_blockAtlasTilesPerSide+1)/side-inset},glm::vec4(1),m_blockMaterial);
            }
            return;
        }
        const int tile=static_cast<int>(getFaceTextureIndex(block,face));
        const int side=m_blockAtlasTilesPerSide;const float s=static_cast<float>(side);
        const float inset=.5f/(16.0f*s);
        quad(x,y,w,h,{tile%side/s+inset,tile/side/s+inset,
             (tile%side+1)/s-inset,(tile/side+1)/s-inset},glm::vec4(1),m_blockMaterial);
    }
    void drawItemIcon(float x,float y,float w,float h,const ItemStack& stack)override{
        if(stack.empty())return;
        if(const auto* item=Plugins::pluginItem(stack.id);item&&item->icon.empty()) {
            if(item->properties.placedBlock) {
                const auto shape=getBlockProps(*item->properties.placedBlock).shape;
                if(shape==RenderShape::Slab||shape==RenderShape::Stair||shape==RenderShape::Door||shape==RenderShape::Button||shape==RenderShape::Boxes){drawBlockIcon(x,y,w,h,*item->properties.placedBlock);return;}
            }
            drawAtlasIcon(x,y,w,h,item->tile,glm::vec4(1));return;
        }
        std::string name=stack.id>=ItemId::NETHERRACK&&stack.id<ItemId::COUNT?itemCommandName(stack.id):getItemProps(stack.id).name;
        if(const auto* item=Plugins::pluginItem(stack.id);item&&!item->icon.empty())name=item->key;
        if(name.find(':')==std::string::npos)for(char& c:name)c=std::isalnum(static_cast<unsigned char>(c))?
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
        UiTheme::rect(*this,x,y,w,4,UiTheme::INK);
        UiTheme::rect(*this,x+1,y+1,w-2,2,{.02f,.02f,.02f,.95f});
        UiTheme::rect(*this,x+1,y+1,(w-2)*value,2,{1-value,value,.08f,1});
    }
    void drawPanel(float x,float y,float w,float h,const glm::vec4& fill)override{
        UiTheme::panel(*this,x,y,w,h,fill);
    }
    void drawTooltip(float x,float y,const ItemStack& stack)override{
        if (stack.empty()) return;
        UiTheme::tooltip(*this,x,y,UiTheme::tooltipDetail(stack,m_localization));
    }
    void renderText(const std::string& text,float x,float y,float scale,
                    const glm::vec3& color)override{
        renderTextAlpha(text,x,y,scale,color,1.0f);
    }
    void renderTextAlpha(const std::string& text,float x,float y,float scale,
                         const glm::vec3& color,float alpha) override {
        float cursor=x;for(uint32_t cp:decodeUtf8(text)){
            if(cp=='\n'){cursor=x;y-=FONT_LINE_HEIGHT*scale;continue;}
            const Glyph* glyph=ensureGlyph(cp);if(!glyph)continue;
            const Glyph& g=*glyph;
            const float renderedHeight = g.height * FONT_RENDER_SCALE;
            if(g.width>0&&g.height>0)quad(
                cursor + g.xoff * FONT_RENDER_SCALE * scale,
                y + (3.0f + (g.yoff-g.height) * FONT_RENDER_SCALE) * scale,
                g.width * FONT_RENDER_SCALE * scale,
                renderedHeight * scale,{g.u0,g.v0,g.u1,g.v1},
                glm::vec4(color,std::clamp(alpha,0.0f,1.0f)),m_fontMaterial);
            cursor+=g.advance*FONT_RENDER_SCALE*scale;
        }
    }
    glm::vec2 measureText(const std::string& text,float scale)override{
        float width=0,line=0;int lines=1;for(uint32_t cp:decodeUtf8(text)){
            if(cp=='\n'){width=std::max(width,line);line=0;++lines;continue;}
            if(const Glyph* glyph=ensureGlyph(cp))
                line+=glyph->advance*FONT_RENDER_SCALE*scale;
        }return {std::max(width,line),FONT_LINE_HEIGHT*scale*lines};
    }
    void setLocalization(const Localization* value)override{m_localization=value;}
private:
    VulkanRenderer& m_renderer;const Localization* m_localization=nullptr;
    glm::vec2 m_canvasOrigin{0},m_canvasSize{0};glm::mat4 m_projection{1};
    std::vector<Batch> m_batches;
    float m_opacity = 1.0f;
    int m_uiWidth=1,m_uiHeight=1;
    RenderTextureHandle m_whiteTexture{},m_itemTexture{},m_fontTexture{};
    RenderMaterialHandle m_whiteMaterial{},m_blockMaterial{},m_itemMaterial{},m_fontMaterial{};
    int m_blockAtlasTilesPerSide=0,m_itemColumns=0,m_itemRows=0;
    std::unordered_map<std::string,int>m_itemIndices;
    std::unordered_map<uint32_t,Glyph>m_glyphs;
    std::vector<unsigned char> m_fontBytes,m_arabicBytes;
    stbtt_fontinfo m_font{},m_arabicFont{};
    bool m_hasArabic=false,m_fontDirty=false;
    TextureData m_fontPixels;
    int m_shelfX=1,m_shelfY=1,m_shelfHeight=0;
    std::vector<std::pair<RenderMaterialHandle,RenderTextureHandle>> m_retiredFonts;
    RenderMaterialHandle material(RenderTextureHandle texture){MaterialDesc d;
        d.pipeline=MaterialPipeline::UiTextured;d.baseColorTexture=texture;
        return m_renderer.createMaterial(d);}
    Batch& batch(RenderMaterialHandle value){
        if(m_batches.empty()||!(m_batches.back().material==value))m_batches.push_back({{}, {}, value});
        return m_batches.back();
    }
    void quad(float x,float y,float w,float h,const glm::vec4& uv,
              const glm::vec4& color,RenderMaterialHandle materialHandle){
        if (!(w>0 && h>0) || color.a<=0) return;
        const glm::vec4 tinted=uiLinearColor(color,m_opacity);
        Batch& b=batch(materialHandle);const uint32_t base=static_cast<uint32_t>(b.vertices.size());
        b.vertices.insert(b.vertices.end(),{{{x,y},{uv.x,uv.y},tinted},
            {{x+w,y},{uv.z,uv.y},tinted},{{x+w,y+h},{uv.z,uv.w},tinted},
            {{x,y+h},{uv.x,uv.w},tinted}});
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
        auto pixels=decodeTexture(root/"textures/generated/items_atlas.png");
        appendPluginItemIcons(pixels,m_itemIndices,m_itemColumns,m_itemRows);
        m_itemTexture=m_renderer.createTexture(pixels,sampler);
        m_itemMaterial=material(m_itemTexture);
    }
    void loadFont(const std::filesystem::path& root){
        m_fontBytes=AssetStore::readPath(root/"fonts/noto/NotoSansCJKsc-Regular.otf");
        if(m_fontBytes.empty()||!stbtt_InitFont(&m_font,m_fontBytes.data(),0))
            throw std::runtime_error("Cannot initialize UI font");
        try {
            m_arabicBytes=AssetStore::readPath(root/"fonts/noto/NotoNaskhArabic-Regular.ttf");
            m_hasArabic=!m_arabicBytes.empty()&&stbtt_InitFont(&m_arabicFont,m_arabicBytes.data(),0)!=0;
        }catch(const std::exception&){}
        m_fontPixels.format=TextureFormat::Rgba8Unorm;
        m_fontPixels.width=m_fontPixels.height=FONT_ATLAS_SIZE;
        m_fontPixels.pixels.resize(static_cast<size_t>(FONT_ATLAS_SIZE)*FONT_ATLAS_SIZE*4u,0);
        std::set<uint32_t> codepoints;for(uint32_t cp=32;cp<127;++cp)codepoints.insert(cp);
        for(const Language language:languagesByEnglishName()){
            try{const std::string text=AssetStore::readTextPath(root/"lang"/
                (std::string(languageCode(language))+".json"));
                for(uint32_t cp:decodeUtf8(text))if(cp>=32)codepoints.insert(cp);
            }catch(const std::exception&){}
            // The language option shows each language's native name, which
            // lives in code and never appears in the JSON files.
            for(uint32_t cp:decodeUtf8(languageNativeName(language)))
                if(cp>=32)codepoints.insert(cp);
        }
        // Active packages own translations outside the base language files.
        // Include every bundle so switching languages keeps all plugin glyphs.
        for(const auto& bundle:Plugins::content().extensions.translations)
            for(const auto& entry:bundle.second)
                for(uint32_t cp:decodeUtf8(entry.second))
                    if(cp>=32)codepoints.insert(cp);
        // Arabic presentation forms never appear in the JSON sources; they are
        // produced at runtime by the Arabic shaper, so their ranges are added
        // explicitly.
        for(uint32_t cp=0xFB50;cp<=0xFDFF;++cp)codepoints.insert(cp);
        for(uint32_t cp=0xFE70;cp<=0xFEFF;++cp)codepoints.insert(cp);
        // Runtime text (world names, chat, plugin metadata) can contain characters
        // absent from these dictionaries. Measure/render append them to this atlas.
        ensureGlyph(0xFFFD);
        for(uint32_t cp:codepoints)ensureGlyph(cp);
        uploadFont();
    }
    const Glyph* ensureGlyph(uint32_t cp) {
        if(cp<32)return nullptr;
        if(const auto found=m_glyphs.find(cp);found!=m_glyphs.end())return &found->second;
        const stbtt_fontinfo* face=nullptr;
        if(stbtt_FindGlyphIndex(&m_font,static_cast<int>(cp)))face=&m_font;
        else if(m_hasArabic&&stbtt_FindGlyphIndex(&m_arabicFont,static_cast<int>(cp)))face=&m_arabicFont;
        if(!face) {
            // Unsupported text must remain visible and take up layout space.
            const Glyph replacement=*ensureGlyph('?');
            return &m_glyphs.emplace(cp,replacement).first->second;
        }
        const float scale=stbtt_ScaleForPixelHeight(face,FONT_RASTER_HEIGHT);
        int advance=0,bearing=0,x0=0,y0=0,x1=0,y1=0;
        stbtt_GetCodepointHMetrics(face,static_cast<int>(cp),&advance,&bearing);
        stbtt_GetCodepointBitmapBox(face,static_cast<int>(cp),scale,scale,&x0,&y0,&x1,&y1);
        const int w=std::max(0,x1-x0),h=std::max(0,y1-y0);
        if(m_shelfX+w+2>=FONT_ATLAS_SIZE){m_shelfX=1;m_shelfY+=m_shelfHeight+2;m_shelfHeight=0;}
        if(m_shelfY+h+2>=FONT_ATLAS_SIZE) {
            const Glyph replacement=*ensureGlyph('?');
            return &m_glyphs.emplace(cp,replacement).first->second;
        }
        if(w&&h) {
            std::vector<uint8_t> bitmap(static_cast<size_t>(w)*h);
            stbtt_MakeCodepointBitmap(face,bitmap.data(),w,h,w,scale,scale,static_cast<int>(cp));
            for(int py=0;py<h;++py)for(int px=0;px<w;++px) {
                const size_t dst=(static_cast<size_t>(m_shelfY+py)*FONT_ATLAS_SIZE+m_shelfX+px)*4u;
                m_fontPixels.pixels[dst]=m_fontPixels.pixels[dst+1]=m_fontPixels.pixels[dst+2]=255;
                m_fontPixels.pixels[dst+3]=bitmap[py*w+px];
            }
        }
        constexpr float side=static_cast<float>(FONT_ATLAS_SIZE);
        const Glyph glyph{m_shelfX/side,(m_shelfY+h)/side,(m_shelfX+w)/side,m_shelfY/side,
            static_cast<float>(w),static_cast<float>(h),static_cast<float>(x0),static_cast<float>(-y0),advance*scale};
        m_shelfX+=w+2;m_shelfHeight=std::max(m_shelfHeight,h);m_fontDirty=true;
        return &m_glyphs.emplace(cp,glyph).first->second;
    }
    void uploadFont() {
        if(!m_fontDirty)return;
        TextureSamplerDesc sampler;sampler.addressU=sampler.addressV=TextureAddressMode::ClampToEdge;
        sampler.minFilter=sampler.magFilter=TextureFilter::Linear;
        const auto texture=m_renderer.createTexture(m_fontPixels,sampler);
        const auto nextMaterial=material(texture);
        if(m_fontMaterial) {
            for(auto& batch:m_batches)if(batch.material==m_fontMaterial)batch.material=nextMaterial;
            // Previous submissions can still reference the old descriptor/image.
            // Retire them after the next frame boundary and an idle wait.
            m_retiredFonts.emplace_back(m_fontMaterial,m_fontTexture);
        }
        m_fontTexture=texture;m_fontMaterial=nextMaterial;m_fontDirty=false;
    }
    void releaseRetiredFonts() {
        for(const auto& retired:m_retiredFonts) {
            m_renderer.destroyMaterial(retired.first);
            m_renderer.destroyTexture(retired.second);
        }
        m_retiredFonts.clear();
    }
};
}

std::unique_ptr<IUIRenderBackend> createVulkanUIBackend(
    IGameRenderer& renderer,RenderTextureHandle blocks,const std::filesystem::path& root){
    auto* vulkan=dynamic_cast<VulkanRenderer*>(&renderer);
    if(!vulkan)throw std::invalid_argument("Vulkan UI requires VulkanRenderer");
    return std::make_unique<VulkanUIBackend>(*vulkan,blocks,root);
}
