# Production targets may only depend on targets from earlier layers.
set(module_layers_0 minecraftc_sdk minecraftc_json minecraftc_engine_policy
    minecraftc_engine_link_options minecraftc_assets minecraftc_platform)
set(module_layers_1 minecraftc_log minecraftc_image_decoder minecraftc_model_animation
    minecraftc_content_registry minecraftc_plugin_runtime minecraftc_atlas_bindings)
set(module_layers_2 minecraftc_core minecraftc_threading minecraftc_animation_graph
    minecraftc_gltf minecraftc_terrain)
set(module_layers_3 minecraftc_simulation_common minecraftc_platform_sdl)
set(module_layers_4 minecraftc_simulation minecraftc_heaven minecraftc_network
    minecraftc_text minecraftc_render_data)
set(module_layers_5 minecraftc_plugins minecraftc_scene minecraftc_render_vulkan)
set(module_layers_6 minecraftc_ui)
set(module_layers_7 minecraftc_app_session)
set(module_layers_8 ${MINECRAFTC_APP_TARGET})

set(module_manifest "")
foreach(layer RANGE 0 8)
    foreach(target IN LISTS module_layers_${layer})
        set(module_layer_${target} ${layer})
    endforeach()
endforeach()
get_property(production_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS production_targets)
    foreach(property IN ITEMS SOURCES LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_property(module_${property} TARGET ${target} PROPERTY ${property})
    endforeach()
    string(APPEND module_manifest
        "${target}\t${module_layer_${target}}\t${module_SOURCES}\t${module_LINK_LIBRARIES};${module_INTERFACE_LINK_LIBRARIES}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/minecraftc-modules.tsv" "${module_manifest}")
