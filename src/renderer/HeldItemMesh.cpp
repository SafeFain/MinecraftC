#include "renderer/HeldItemMesh.h"

namespace {
void quad(MeshData& mesh, const glm::vec3& a, const glm::vec3& b,
          const glm::vec3& c, const glm::vec3& d,
          const glm::vec2& ta, const glm::vec2& tb,
          const glm::vec2& tc, const glm::vec2& td) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {{a,ta},{b,tb},{c,tc},{d,td}});
    mesh.indices.insert(mesh.indices.end(), {base,base+1,base+2,base,base+2,base+3});
}
}

MeshData buildHeldCubeMesh(const std::array<int, 6>& tiles, int tilesPerSide,
                           bool atlasTilesAreVerticallyFlipped) {
    MeshData mesh;
    const glm::vec3 p[]={{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
                         {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
    const int faces[][4]={{0,3,2,1},{5,6,7,4},{4,7,3,0},{1,2,6,5},{3,7,6,2},{4,0,1,5}};
    for(int f=0;f<6;++f){
        const int tile=tiles[static_cast<size_t>(f)];
        const float side=static_cast<float>(tilesPerSide);
        const float u0=(tile%tilesPerSide)/side,v0=(tile/tilesPerSide)/side;
        const float u1=(tile%tilesPerSide+1)/side,v1=(tile/tilesPerSide+1)/side;
        // The shared block atlas flips pixels inside each tile for bottom-left
        // coordinates. Raw player-skin tiles do not, so their V order stays opposite.
        if (atlasTilesAreVerticallyFlipped) {
            quad(mesh,p[faces[f][0]],p[faces[f][1]],p[faces[f][2]],p[faces[f][3]],
                 {u0,v0},{u0,v1},{u1,v1},{u1,v0});
        } else {
            quad(mesh,p[faces[f][0]],p[faces[f][1]],p[faces[f][2]],p[faces[f][3]],
                 {u0,v1},{u0,v0},{u1,v0},{u1,v1});
        }
    }
    return mesh;
}

MeshData buildHeldBlockShapeMesh(BlockId block,int tilesPerSide) {
    MeshData mesh;
    const auto cube=buildHeldCubeMesh({static_cast<int>(getFaceTextureIndex(block,FaceDir::FRONT)),static_cast<int>(getFaceTextureIndex(block,FaceDir::BACK)),static_cast<int>(getFaceTextureIndex(block,FaceDir::LEFT)),static_cast<int>(getFaceTextureIndex(block,FaceDir::RIGHT)),static_cast<int>(getFaceTextureIndex(block,FaceDir::TOP)),static_cast<int>(getFaceTextureIndex(block,FaceDir::BOTTOM))},tilesPerSide,true);
    for(const auto& surface:blockModelFaces(block)) {
        const int face=surface.face==FaceDir::FRONT?0:surface.face==FaceDir::BACK?1:surface.face==FaceDir::LEFT?2:surface.face==FaceDir::RIGHT?3:surface.face==FaceDir::TOP?4:5;
        const auto& box=surface.box;const auto offset=static_cast<uint32_t>(mesh.vertices.size());
        for(int i=0;i<4;++i){auto vertex=cube.vertices[face*4+i];vertex.position=(vertex.position+glm::vec3(.5f))*(box.max-box.min)+box.min-glm::vec3(.5f);mesh.vertices.push_back(vertex);}
        for(uint32_t index:{0u,1u,2u,0u,2u,3u})mesh.indices.push_back(offset+index);
    }
    return mesh;
}
