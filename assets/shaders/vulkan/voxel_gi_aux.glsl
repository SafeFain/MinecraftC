// std430 scalar layout: six uints, stride 24. Face order matches FaceDir.
struct GiAux { uint occupiedLo; uint occupiedHi; uint coverage;
               uint exposureLo; uint exposureHi; uint emission; };
vec3 giRgb(uint value){return vec3(value&255u,(value>>8)&255u,(value>>16)&255u)/255.0;}
float giExposure(GiAux a,int face){
    uint word=face<4?a.exposureLo:a.exposureHi;
    return float((word>>uint((face<4?face:face-4)*8))&255u)/255.0;
}
vec3 giFaceNormal(int face){
    if(face==0)return vec3(0,1,0); if(face==1)return vec3(0,-1,0);
    if(face==2)return vec3(0,0,-1); if(face==3)return vec3(0,0,1);
    if(face==4)return vec3(1,0,0); return vec3(-1,0,0);
}
bool giOccupied(GiAux a,int bit){
    return (((bit<32?a.occupiedLo:a.occupiedHi)>>uint(bit%32))&1u)!=0u;
}
float giDirectionalWeight(GiAux a,vec3 direction){
    float exposed=0.0;
    for(int f=0;f<6;++f)exposed+=giExposure(a,f)*max(dot(giFaceNormal(f),direction),0.0);
    vec3 d=abs(direction);
    float denominator=max(d.x+d.y+d.z,0.0001);
    return exposed/denominator*dot(giRgb(a.coverage),d)/denominator;
}
