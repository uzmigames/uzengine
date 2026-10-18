-- Stage-1 authoring: Hair uses RGB reflectance, roughness_multiplier=beta_m,
-- metallic_multiplier=beta_n. The normal PBR meanings remain unchanged otherwise.
function on_start(entity)
    uz.scene.set_material(entity, {
        shading_model = "hair",
        base_color_r = 1, base_color_g = 1, base_color_b = 1,
        roughness_multiplier = 0.07, metallic_multiplier = 0.18,
        two_sided = true
    })
    uz.log("HairStage1: tagged swatch Hair (sigma_a=0, beta_m=.07, beta_n=.18)")
end
