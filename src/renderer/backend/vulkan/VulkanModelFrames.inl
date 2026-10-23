    template<typename Callback>
    void forEachModelPrimitive(const std::vector<ModelPassSubmission>& submissions,
                               bool blended, Callback&& callback) {
        for (const auto& submission : submissions) {
            const auto handle=submission.draw.model;
            if (!handle || handle.generation!=modelGeneration || handle.value>models.size()) continue;
            auto& resource=models[handle.value-1];
            for (const auto& binding:resource.bindings) {
                auto& primitive=resource.primitives[static_cast<size_t>(binding.primitive)];
                const model::Material fallback;
                const auto& material=primitive.material>=0 ? resource.asset->materials[primitive.material] : fallback;
                if ((material.alphaMode==model::AlphaMode::Blend)!=blended) continue;
                callback(submission,resource,primitive,material,binding);
            }
        }
    }

    void prepareModelBuffer() {
        modelBatches.clear();
        auto& frame=modelBuffers[currentFrame];
        for(auto& page:frame.pages) {
            page.environments.clear(); page.instances.clear(); page.palettes.clear();
        }
        struct Item {
            const ModelPassSubmission* submission;
            model::DrawBinding binding;
            uint32_t context;
            bool blend;
        };
        std::vector<Item> opaque,blend;
        std::vector<ModelEnvironment> contexts;
        const auto collect=[&](auto& target,bool blended) {
            forEachModelPrimitive(blended?submittedModelBlend:submittedModelOpaque,blended,
                [&](const auto& submission,auto&,auto&,const auto&,const auto& binding) {
                    ModelEnvironment environment;
                    environment.viewProjection=clipSpaceCorrection()*submission.viewProjection;
                    environment.cameraFogStart={submission.cameraPosition,submission.fogStart};
                    environment.fogColorEnd={submission.environment.fogColor,submission.fogEnd};
                    environment.lightDirection={submission.environment.lightDirection,0};
                    environment.directColor={submission.environment.directColor*submission.environment.directIntensity,0};
                    environment.ambientColor={submission.environment.ambientColor*submission.environment.ambientIntensity,0};
                    environment.options.x=enhancedVisualConfig(visualQuality,enhancedVisualSettings).atmosphereStrength;
                    uint32_t context=0;
                    while(context<contexts.size() && std::memcmp(&environment,&contexts[context],sizeof(environment))!=0) ++context;
                    if(context==contexts.size()) contexts.push_back(environment);
                    target.push_back({&submission,binding,context,blended});
                });
        };
        collect(opaque,false); collect(blend,true);
        std::stable_sort(opaque.begin(),opaque.end(),[](const auto& a,const auto& b) {
            return std::make_tuple(a.context,a.submission->draw.model.value,a.binding.primitive)<
                   std::make_tuple(b.context,b.submission->draw.model.value,b.binding.primitive);
        });
        // Blend was already stably sorted by distance at submission. Never sort
        // it by material; only consecutive compatible entries may coalesce.
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physicalDevice,&properties);
        const size_t limit=modelPageBytes ? std::min(modelPageBytes,static_cast<size_t>(properties.limits.maxStorageBufferRange))
                                         : static_cast<size_t>(properties.limits.maxStorageBufferRange);
        if(limit<std::max(sizeof(ModelEnvironment),model::MAX_JOINTS*sizeof(glm::mat4)))
            throw std::runtime_error("model storage page cannot hold one skin");
        uint32_t activePages=0,currentPalettePage=0;
        std::map<std::pair<const model::ModelInstance*,int>,std::pair<uint32_t,uint32_t>> paletteLocations;
        std::map<uint32_t,uint32_t> instancePages;
        auto addPage=[&](uint32_t paletteSource) {
            const uint32_t index=activePages++;
            if(index==frame.pages.size()) frame.pages.emplace_back();
            frame.pages[index].paletteSource=paletteSource;
            return index;
        };
        addPage(0);instancePages[0]=0;
        const auto append=[&](const Item& item) {
            const auto& draw=item.submission->draw;
            auto& resource=models[draw.model.value-1];
            const auto& binding=item.binding;
            const auto& snapshot=*(draw.snapshot ? draw.snapshot : resource.bindInstance);
            const std::vector<glm::mat4>* palette=nullptr;
            if(binding.skin>=0 && static_cast<size_t>(binding.skin)<snapshot.jointPalettes.size())
                palette=&snapshot.jointPalettes[binding.skin];
            const auto key=std::make_pair(&snapshot,binding.skin);
            uint32_t palettePage=0,paletteOffset=0;
            if(palette && !palette->empty()) {
                auto found=paletteLocations.find(key);
                if(found==paletteLocations.end()) {
                    if((frame.pages[currentPalettePage].palettes.size()+palette->size())*sizeof(glm::mat4)>limit) {
                        currentPalettePage=addPage(activePages);
                        instancePages[currentPalettePage]=currentPalettePage;
                    }
                    auto& owner=frame.pages[currentPalettePage];
                    palettePage=currentPalettePage;paletteOffset=static_cast<uint32_t>(owner.palettes.size());
                    owner.palettes.insert(owner.palettes.end(),palette->begin(),palette->end());
                    paletteLocations.emplace(key,std::make_pair(palettePage,paletteOffset));
                } else {palettePage=found->second.first;paletteOffset=found->second.second;}
            }
            uint32_t pageIndex=instancePages.at(palettePage);
            if((frame.pages[pageIndex].instances.size()+1)*sizeof(ModelGpuInstance)>limit ||
               (frame.pages[pageIndex].environments.size()+1)*sizeof(ModelEnvironment)>limit) {
                pageIndex=addPage(palettePage);instancePages[palettePage]=pageIndex;
            }
            auto& page=frame.pages[pageIndex];
            ModelGpuInstance instance;
            instance.transform=draw.transform;
            if(binding.skin<0 && binding.node>=0 && static_cast<size_t>(binding.node)<snapshot.pose.global.size())
                instance.transform*=snapshot.pose.global[binding.node];
            if(palette && !palette->empty()) {
                instance.indices.x=paletteOffset;
                instance.indices.y=static_cast<uint32_t>(palette->size());
            }
            const auto& context=contexts[item.context];
            uint32_t environmentIndex=0;
            while(environmentIndex<page.environments.size() && std::memcmp(&context,&page.environments[environmentIndex],sizeof(context))!=0) ++environmentIndex;
            instance.indices.z=environmentIndex;
            if(environmentIndex==page.environments.size()) page.environments.push_back(context);
            const auto& primitive=resource.primitives[binding.primitive];
            const model::Material fallback;
            const auto& material=primitive.material>=0 ? resource.asset->materials[primitive.material] : fallback;
            const int texture=material.texture>=0 ? material.texture : material.image;
            const bool textured=texture>=0 && static_cast<size_t>(texture)<resource.textureMaterials.size();
            instance.baseColor=material.baseColor*draw.tint;
            instance.params={material.alphaMode==model::AlphaMode::Mask ? material.alphaCutoff:0,
                             textured?1.0f:0.0f,0,item.blend?1.0f:0.0f};
            const uint32_t first=static_cast<uint32_t>(page.instances.size());
            bool merge=false;
            if(modelBatching && !modelBatches.empty()) {
                const auto& last=modelBatches.back();
                merge=last.model==draw.model.value && last.primitive==static_cast<uint32_t>(binding.primitive) &&
                    last.page==pageIndex && last.first+last.count==first && last.blended==item.blend &&
                    page.instances.back().indices.z==instance.indices.z;
            }
            if(merge) ++modelBatches.back().count;
            else modelBatches.push_back({draw.model.value,static_cast<uint32_t>(binding.primitive),pageIndex,first,1,item.blend});
            page.instances.push_back(instance);
        };
        for(const auto& item:opaque) append(item);
        for(const auto& item:blend) append(item);
        for(auto& page:frame.pages) {
            if(page.instances.empty()) continue;
            if(!page.descriptorSet) {
                VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocate.descriptorPool=descriptors.descriptorPool; allocate.descriptorSetCount=1;
                allocate.pSetLayouts=&descriptors.modelUniformDescriptorSetLayout;
                require(vkAllocateDescriptorSets(device,&allocate,&page.descriptorSet),"allocate model frame descriptors");
            }
            const std::array<size_t,3> sizes{page.environments.size()*sizeof(ModelEnvironment),
                page.instances.size()*sizeof(ModelGpuInstance),page.palettes.size()*sizeof(glm::mat4)};
            const std::array<const void*,3> sources{page.environments.data(),page.instances.data(),page.palettes.data()};
            for(uint32_t i=0;i<3;++i) {
                const auto needed=std::max(sizes[i],sizeof(glm::mat4));
                if(page.capacities[i]<needed) {
                    destroyBuffer(page.buffers[i]);
                    page.capacities[i]=std::min(limit,std::max(needed,page.capacities[i]?page.capacities[i]*2:size_t{4096}));
                    page.buffers[i]=createBuffer(page.capacities[i],VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VMA_MEMORY_USAGE_AUTO,
                        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT);
                }
                if(sizes[i]) std::memcpy(page.buffers[i].mapped,sources[i],sizes[i]);
                require(vmaFlushAllocation(allocator,page.buffers[i].allocation,0,needed),"flush model frame");
                const auto& descriptorPage=i==2?frame.pages[page.paletteSource]:page;
                const VkDescriptorBufferInfo bufferInfo{descriptorPage.buffers[i].handle,0,descriptorPage.capacities[i]};
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet=page.descriptorSet; write.dstBinding=i; write.descriptorCount=1;
                write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo=&bufferInfo;
                vkUpdateDescriptorSets(device,1,&write,0,nullptr);
            }
            performance.modelPaletteBytes+=sizes[2]; performance.modelInstanceBytes+=sizes[1];
            performance.modelInstances+=static_cast<uint32_t>(page.instances.size());
        }
    }
