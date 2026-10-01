#include <filesystem>

#include "bodyharvest_config.h"
#include "recompui/config.h"
#include "util/file.h"

void bodyharvest::init_config() {
    std::filesystem::create_directories(recompui::file::get_app_folder_path());

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = false;
    general_options.has_mouse_sensitivity = false;
    recompui::config::create_general_tab(general_options);

    recompui::config::create_graphics_tab();
    recompui::config::create_controls_tab();
    recompui::config::create_sound_tab();

    recompui::config::finalize();
}
