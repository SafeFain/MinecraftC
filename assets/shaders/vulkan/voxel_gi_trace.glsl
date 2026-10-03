layout(set=0,binding=2) uniform sampler3D irradiance0;
layout(set=0,binding=3) uniform sampler3D irradiance1;
layout(set=0,binding=4) uniform sampler3D irradiance2;
layout(set=0,binding=5) uniform sampler3D irradiance3;
#include "voxel_gi_aux.glsl"
layout(set=0,binding=11,std430) readonly buffer Aux0 { GiAux cells[]; } aux0;
layout(set=0,binding=12,std430) readonly buffer Aux1 { GiAux cells[]; } aux1;
layout(set=0,binding=13,std430) readonly buffer Aux2 { GiAux cells[]; } aux2;
layout(set=0,binding=14,std430) readonly buffer Aux3 { GiAux cells[]; } aux3;
layout(set=0,binding=10) uniform GiScreenUniforms {
    mat4 inverseViewProjection;
    mat4 previousViewProjection;
    vec4 cameraWorld;
    vec4 previousCameraWorld;
    vec4 currentWorldOrigin;
    vec4 previousWorldOrigin;
    vec4 minimumCellAndSize[4];
    vec4 config;
    vec4 temporal;
    vec4 sampling;
    vec4 traversal;
} gi;
// Validity stays discrete at mip zero. Never interpolate the -1 sentinel
// with opacity, or wrap a filter footprint across a world-space clipmap edge.
vec4 fetchCell(int level,ivec3 cell){
    ivec3 minimum=ivec3(gi.minimumCellAndSize[level].xyz);
    int resolution=int(gi.config.x);
    if(any(lessThan(cell,minimum))||
       any(greaterThanEqual(cell,minimum+ivec3(resolution))))return vec4(0,0,0,-1);
    ivec3 ring=ivec3(mod(vec3(cell),gi.config.x));
    if(level==0)return texelFetch(irradiance0,ring,0);
    if(level==1)return texelFetch(irradiance1,ring,0);
    if(level==2)return texelFetch(irradiance2,ring,0);
    return texelFetch(irradiance3,ring,0);
}

GiAux fetchAux(int level,ivec3 cell){
    ivec3 ring=ivec3(mod(vec3(cell),gi.config.x));
    int r=int(gi.config.x),index=ring.x+r*(ring.y+r*ring.z);
    if(level==0)return aux0.cells[index]; if(level==1)return aux1.cells[index];
    if(level==2)return aux2.cells[index]; return aux3.cells[index];
}

float microSize(int level){return max(gi.minimumCellAndSize[level].w/4.0,1.0);}
vec4 sampleClipmap(vec3 world,out int selectedLevel){
    int count=int(gi.config.y+0.5);
    selectedLevel=-1;
    for(int level=0;level<4;++level){
        if(level>=count)break;
        float size=gi.minimumCellAndSize[level].w;
        ivec3 cell=ivec3(floor(world/size));
        vec4 value=fetchCell(level,cell);
        if(value.a>=0.0){
            selectedLevel=level;
            int n=int(min(size,4.0));
            ivec3 sub=clamp(ivec3(floor((world-vec3(cell)*size)/microSize(level))),ivec3(0),ivec3(n-1));
            GiAux a=fetchAux(level,cell);
            if(!giOccupied(a,sub.x+n*(sub.y+n*sub.z)))value.a=0.0;
            return value;
        }
    }
    return vec4(0,0,0,-1);
}

float cellCrossing(vec3 position,vec3 direction,float size){
    vec3 cell=floor(position/size);
    vec3 boundary=(cell+step(vec3(0),direction))*size;
    vec3 crossing=vec3(1e20);
    for(int axis=0;axis<3;++axis)
        if(abs(direction[axis])>0.00001)crossing[axis]=(boundary[axis]-position[axis])/direction[axis];
    return max(min(crossing.x,min(crossing.y,crossing.z)),0.0)+0.001;
}

bool visibleFilterTap(vec3 origin,vec3 target,int targetLevel){
    vec3 delta=target-origin;
    float distance=length(delta);
    if(distance<0.001)return true;
    vec3 direction=delta/distance;
    float travel=0.001;
    ivec3 destination=ivec3(floor(target/gi.minimumCellAndSize[targetLevel].w));
    for(int stepIndex=0;stepIndex<8;++stepIndex){
        vec3 p=origin+direction*travel;
        int level;
        vec4 value=sampleClipmap(p,level);
        if(level<0)return false;
        if(value.a>0.0){
            // Only the destination surface may end a filter segment.
            return all(equal(ivec3(floor(p/gi.minimumCellAndSize[targetLevel].w)),destination));
        }
        float size=microSize(level);
        GiAux a=fetchAux(level,ivec3(floor(p/gi.minimumCellAndSize[level].w)));
        if((a.occupiedLo|a.occupiedHi)==0u)size=gi.minimumCellAndSize[level].w;
        travel+=cellCrossing(p,direction,size);
        if(travel>=distance)return true;
    }
    return false;
}

vec3 radianceFootprint(int level,vec3 world,float diameter,vec3 direction){
    float size=gi.minimumCellAndSize[level].w;
    vec3 tangent=normalize(abs(direction.y)<0.9?cross(direction,vec3(0,1,0)):cross(direction,vec3(1,0,0)));
    vec3 bitangent=cross(direction,tangent);
    vec3 sum=vec3(0); float weight=0.0;
    ivec3 visited[4]; vec4 cachedValues[4]; vec3 cachedRadiance[4]; int visitedCount=0;
    for(int tap=0;tap<4;++tap){
        vec3 point=world+(tangent*(tap%2==0?-1.0:1.0)+bitangent*(tap<2?-1.0:1.0))*diameter*0.25;
        ivec3 cell=ivec3(floor(point/size));
        int index=-1;
        for(int i=0;i<visitedCount;++i)if(all(equal(visited[i],cell)))index=i;
        if(index<0){
            index=visitedCount++;
            visited[index]=cell; cachedValues[index]=fetchCell(level,cell);
            cachedRadiance[index]=vec3(0);
            if(cachedValues[index].a>=0.0){
                GiAux a=fetchAux(level,cell);
                cachedRadiance[index]=max(cachedValues[index].rgb-giRgb(a.emission)*1.35,vec3(0))*
                    giDirectionalWeight(a,-direction);
            }
        }
        // Fetches may be shared, but visibility belongs to each individual tap:
        // two points in the same coarse cell can lie on opposite sides of a wall.
        if(cachedValues[index].a<0.0||dot(point-world,direction)>size*0.01||
           !visibleFilterTap(world-direction*0.003,point,level))continue;
        sum+=cachedRadiance[index]; weight+=1.0;
    }
    if(weight>0.0)return sum/weight;
    ivec3 cell=ivec3(floor(world/size));
    vec4 value=fetchCell(level,cell);
    if(value.a<0.0)return vec3(0);
    GiAux a=fetchAux(level,cell);
    return max(value.rgb-giRgb(a.emission)*1.35,vec3(0))*giDirectionalWeight(a,-direction);
}

vec3 filteredRadiance(int geometryLevel,vec3 world,float diameter,vec3 direction){
    int count=int(gi.config.y+0.5),lower=geometryLevel,upper=geometryLevel;
    for(int i=0;i<4;++i){
        if(i<geometryLevel||i>=count)continue;
        ivec3 cell=ivec3(floor(world/gi.minimumCellAndSize[i].w));
        if(fetchCell(i,cell).a<0.0)break;
        upper=i;
        if(gi.minimumCellAndSize[i].w>=diameter)break;
        lower=i;
    }
    float a=gi.minimumCellAndSize[lower].w,b=gi.minimumCellAndSize[upper].w;
    float blend=b>a?clamp(log2(max(diameter/a,1.0))/log2(b/a),0.0,1.0):0.0;
    vec3 reflected=radianceFootprint(lower,world,diameter,direction);
    if(upper!=lower)reflected=mix(reflected,radianceFootprint(upper,world,diameter,direction),blend);
    // Emission is sampled at the actual geometry intersection, not imported
    // from a wider coarse footprint that could include a source behind a wall.
    GiAux hit=fetchAux(geometryLevel,ivec3(floor(world/gi.minimumCellAndSize[geometryLevel].w)));
    return reflected*gi.sampling.z+giRgb(hit.emission)*1.35*gi.sampling.w;
}

vec3 coneDirection(vec3 normal,int index,int count,float rotation){
    vec3 tangent=normalize(abs(normal.y)<0.9?cross(normal,vec3(0,1,0)):
        cross(normal,vec3(1,0,0)));
    vec3 bitangent=cross(normal,tangent);
    float angle=6.2831853*(float(index)/max(float(count),1.0)+rotation);
    return normalize(normal*0.72+(tangent*cos(angle)+bitangent*sin(angle))*0.69);
}

// A coarse empty cell is safe only with converged source data. Clip the
// crossing to every finer active window; never jump over a finer-only source.
float emptyCrossing(vec3 position,vec3 direction,int geometryLevel,float initial){
    if(gi.traversal.x<0.5)return initial;
    float distance=initial;
    for(int level=geometryLevel+1;level<int(gi.config.y);++level){
        float size=gi.minimumCellAndSize[level].w;
        ivec3 cell=ivec3(floor(position/size));
        vec4 value=fetchCell(level,cell);
        if(value.a<0.0)break;
        GiAux a=fetchAux(level,cell);
        if((a.occupiedLo|a.occupiedHi)!=0u)break;
        float candidate=cellCrossing(position,direction,size);
        distance=max(distance,candidate);
    }
    for(int finer=0;finer<int(gi.config.y);++finer){
        float fineSize=gi.minimumCellAndSize[finer].w;
        vec3 minimum=gi.minimumCellAndSize[finer].xyz*fineSize;
        vec3 maximum=minimum+vec3(gi.config.x*fineSize);
        for(int axis=0;axis<3;++axis){
            if(abs(direction[axis])<0.00001)continue;
            float enter=(minimum[axis]-position[axis])/direction[axis];
            float leave=(maximum[axis]-position[axis])/direction[axis];
            if(enter>0.0001)distance=min(distance,enter+0.001);
            if(leave>0.0001)distance=min(distance,leave+0.001);
        }
    }
    return distance;
}

vec3 traceCone(vec3 origin,vec3 direction,out float coverage){
    int budget=int(gi.config.w+0.5)*32;
    float maximumDistance=gi.temporal.w;
    float travel=0.02;
    float transmittance=1.0;
    vec3 accumulated=vec3(0.0);
    float knownDistance=0.0;
    for(int stepIndex=0;stepIndex<256;++stepIndex){
        if(stepIndex>=budget||travel>maximumDistance||transmittance<0.02)break;
        vec3 position=origin+direction*travel;
        int level;
        vec4 value=sampleClipmap(position,level);
        // Try the coarser valid levels first; unknown space then ends the ray.
        // It neither spends radiance samples nor creates a fictitious occluder.
        if(level<0)break;
        float cellSize=gi.minimumCellAndSize[level].w;
        float opacity=clamp(value.a,0.0,1.0);
        if(opacity>0.0){
            float diameter=max(cellSize,travel*0.58);
            accumulated+=filteredRadiance(level,position,diameter,direction)*
                transmittance*opacity;
            transmittance*=1.0-opacity;
        }
        // Empty parent cells can be skipped; occupied parents must visit
        // every conservative subcell, including oblique thin-wall crossings.
        GiAux a=fetchAux(level,ivec3(floor(position/cellSize)));
        float stepSize=(a.occupiedLo|a.occupiedHi)==0u?cellSize:microSize(level);
        float crossing=cellCrossing(position,direction,stepSize);
        if((a.occupiedLo|a.occupiedHi)==0u)
            crossing=emptyCrossing(position,direction,level,crossing);
        travel+=crossing;
        knownDistance=min(travel,maximumDistance);
    }
    coverage=1.0-transmittance*(1.0-knownDistance/maximumDistance);
    return accumulated;
}

vec3 traceGi(vec3 world,vec3 normal,out float validCoverage){
    int cones=int(gi.config.z+0.5);
    float rotation=fract(sin(dot(floor(world),vec3(12.9898,78.233,37.719)))*43758.5453);
    vec3 total=vec3(0); validCoverage=0.0;
    for(int cone=0;cone<6;++cone){
        if(cone>=cones)break;
        float coverage;
        total+=traceCone(world+normal*0.08,coneDirection(normal,cone,cones,rotation),coverage);
        validCoverage+=coverage;
    }
    validCoverage/=max(float(cones),1.0);
    return total/max(float(cones),1.0)*gi.temporal.x;
}
