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
