#pragma once
#include "FrozenHeavenV9.h"
#include "minecraftc/extensions.h"
// Independent adaptation of the frozen 988fbd4 approximate LOD cell path.
namespace FrozenHeavenReference {
inline std::vector<MC_LodSpan> approximateLod(const FrozenHeavenV9& generator,int originX,int originZ,int cellSize) {
    const int centerX=originX+cellSize/2,centerZ=originZ+cellSize/2;
    struct Column {std::vector<MC_LodSpan> spans;} column;
    for(const auto& island:generator.sampleHeavenLayers(centerX,centerZ))if(island.present)
        column.spans.push_back({island.bottom,island.top,static_cast<uint16_t>(FrozenHeavenV9::heavenSurfaceBlock(island.biome))});
    if(cellSize<=8) {
        const int sampleCount=std::min(cellSize,4);
                const auto centerLayers = generator.sampleHeavenLayers(centerX, centerZ);
                std::array<std::vector<FrozenHeavenV9::HeavenLodFeature>,
                           5> selected;
                for (int sampleZ = 0; sampleZ < sampleCount; ++sampleZ) {
                    for (int sampleX = 0; sampleX < sampleCount; ++sampleX) {
                        const int wx = originX +
                            (sampleX * cellSize + cellSize / 2) / sampleCount;
                        const int wz = originZ +
                            (sampleZ * cellSize + cellSize / 2) / sampleCount;
                        std::array<std::vector<FrozenHeavenV9::HeavenLodFeature>,
                                   5> sampled;
                        for (const auto& feature :
                             generator.sampleHeavenLodFeatures(wx, wz)) {
                            const auto& centerLayer = centerLayers[
                                static_cast<size_t>(feature.layer)];
                            if (centerLayer.biome == feature.biome)
                                sampled[static_cast<size_t>(feature.layer)].push_back(
                                    feature);
                        }
                        for (int layer = 0;
                             layer < 5; ++layer) {
                            if (!centerLayers[static_cast<size_t>(layer)].present ||
                                !selected[static_cast<size_t>(layer)].empty() ||
                                sampled[static_cast<size_t>(layer)].empty())
                                continue;
                            selected[static_cast<size_t>(layer)] =
                                std::move(sampled[static_cast<size_t>(layer)]);
                        }
                    }
                }


                for (int layer = 0; layer < 5;
                     ++layer) {
                    const auto& features = selected[static_cast<size_t>(layer)];
                    if (features.empty()) continue;
                    const int surfaceTop = centerLayers[static_cast<size_t>(layer)].top;
                    for (const auto& feature : features) {
                        if (feature.replacesSurface) {
                            const auto found = std::find_if(
                                column.spans.begin(), column.spans.end(),
                                [surfaceTop](const MC_LodSpan& span) {
                                    return span.top == surfaceTop;
                                });
                            if (found != column.spans.end()) {
                                if (found->bottom < surfaceTop) {
                                    found->top = static_cast<int16_t>(surfaceTop-1);
                                    column.spans.push_back({static_cast<int16_t>(surfaceTop),
                                        static_cast<int16_t>(surfaceTop),static_cast<uint16_t>(feature.block)});
                                } else found->block=static_cast<uint16_t>(feature.block);
                            }
                            continue;
                        }
                        const int bottom = surfaceTop + feature.bottomOffset;
                        const int top = std::min(Config::WORLD_MAX_Y - 1,
                            surfaceTop + feature.topOffset);
                        if (bottom <= top)
                            column.spans.push_back({static_cast<int16_t>(bottom),
                                static_cast<int16_t>(top), static_cast<uint16_t>(feature.block)});
                    }
                }
                std::sort(column.spans.begin(), column.spans.end(),
                    [](const MC_LodSpan& a, const MC_LodSpan& b) {
                        return a.bottom < b.bottom;
                    });
    }
    return column.spans;
}
}
