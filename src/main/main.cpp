#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "nfd.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#define SDL_MAIN_HANDLED
#ifdef _WIN32
#include "SDL.h"
#else
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
// Undefine x11 macros that get included by SDL_syswm.h.
#undef None
#undef Status
#undef LockMask
#undef ControlMask
#undef Success
#undef Always
#endif

#include "recompui/recompui.h"
#include "recompui/program_config.h"
#include "recompui/renderer.h"
#include "util/file.h"
#include "recompinput/input_events.h"
#include "recompinput/recompinput.h"
#include "recompinput/profiles.h"
#include "bodyharvest_config.h"
#include "bodyharvest_game.h"
#include "recomp_data.h"
#include "audio.h"
#include "theme.h"
#include "librecomp/game.hpp"
#include "librecomp/mods.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>
#include "SDL_syswm.h"
#endif

const std::string version_string = "1.0.0";

template<typename... Ts>
void exit_error(const char* str, Ts ...args) {
    ((void)fprintf(stderr, str, args), ...);
    assert(false);

    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) < 0) {
        exit_error("Failed to initialize SDL2: %s\n", SDL_GetError());
    }

    fprintf(stdout, "SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());

    return {};
}

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    if (recompinput::players::is_single_player_mode() || recompinput::players::get_player_is_assigned(controller_num)) {
        return ultramodern::input::connected_device_info_t{
            .connected_device = ultramodern::input::Device::Controller,
            .connected_pak = ultramodern::input::Pak::RumblePak,
        };
    }

    return ultramodern::input::connected_device_info_t{
        .connected_device = ultramodern::input::Device::None,
        .connected_pak = ultramodern::input::Pak::None,
    };
}

SDL_Window* window;

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    uint32_t flags = SDL_WINDOW_RESIZABLE;

#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#elif defined(RT64_SDL_WINDOW_VULKAN)
    flags |= SDL_WINDOW_VULKAN;
#endif

    window = SDL_CreateWindow(bodyharvest::program_name.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 900, flags);

    if (window == nullptr) {
        exit_error("Failed to create window: %s\n", SDL_GetError());
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);

#if defined(_WIN32)
    return ultramodern::renderer::WindowHandle{ wmInfo.info.win.window, GetCurrentThreadId() };
#elif defined(__linux__) || defined(__ANDROID__)
    return ultramodern::renderer::WindowHandle{ window };
#elif defined(__APPLE__)
    SDL_MetalView view = SDL_Metal_CreateView(window);
    return ultramodern::renderer::WindowHandle{ wmInfo.info.cocoa.window, SDL_Metal_GetLayer(view) };
#else
    static_assert(false && "Unimplemented");
#endif
}

void update_gfx(void*) {
    recompinput::handle_events();
}

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();

// Referenced by RecompFrontend's default launcher.
std::vector<recomp::GameEntry> supported_games = {
    {
        .rom_hash = 0x9949AA9BC66F33CBULL,
        .internal_name = "BODY HARVEST",
        .display_name = "Body Harvest",
        .game_id = u8"bh.us",
        .mod_game_id = "bh",
        .save_type = recomp::SaveType::AllowAll,
        .is_enabled = true,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
        .thread_create_callback = bodyharvest::thread_create_callback,
    },
};

#ifdef _WIN32

struct PreloadContext {
    HANDLE handle;
    HANDLE mapping_handle;
    SIZE_T size;
    PVOID view;
};

// Maps this executable into memory and locks it, so that running new code pages doesn't stutter on page faults.
bool preload_executable(PreloadContext& context) {
    wchar_t module_name[MAX_PATH];
    GetModuleFileNameW(NULL, module_name, MAX_PATH);

    context.handle = CreateFileW(module_name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (context.handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to load executable into memory!");
        context = {};
        return false;
    }

    LARGE_INTEGER module_size;
    if (!GetFileSizeEx(context.handle, &module_size)) {
        fprintf(stderr, "Failed to get size of executable!");
        CloseHandle(context.handle);
        context = {};
        return false;
    }

    context.size = module_size.QuadPart;

    context.mapping_handle = CreateFileMappingW(context.handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (context.mapping_handle == nullptr) {
        fprintf(stderr, "Failed to create file mapping of executable!");
        CloseHandle(context.handle);
        context = {};
        return false;
    }

    context.view = MapViewOfFile(context.mapping_handle, FILE_MAP_READ, 0, 0, 0);
    if (context.view == nullptr) {
        fprintf(stderr, "Failed to map view of of executable!");
        CloseHandle(context.mapping_handle);
        CloseHandle(context.handle);
        context = {};
        return false;
    }

    DWORD pid = GetCurrentProcessId();
    HANDLE process_handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process_handle == nullptr) {
        fprintf(stderr, "Failed to open own process!");
        CloseHandle(context.mapping_handle);
        CloseHandle(context.handle);
        context = {};
        return false;
    }

    SIZE_T minimum_set_size, maximum_set_size;
    if (!GetProcessWorkingSetSize(process_handle, &minimum_set_size, &maximum_set_size)) {
        fprintf(stderr, "Failed to get working set size!");
        CloseHandle(context.mapping_handle);
        CloseHandle(context.handle);
        context = {};
        return false;
    }

    if (!SetProcessWorkingSetSize(process_handle, minimum_set_size + context.size, maximum_set_size + context.size)) {
        fprintf(stderr, "Failed to set working set size!");
        CloseHandle(context.mapping_handle);
        CloseHandle(context.handle);
        context = {};
        return false;
    }

    if (VirtualLock(context.view, context.size) == 0) {
        fprintf(stderr, "Failed to lock view of executable! (Error: %08lx)\n", GetLastError());
        CloseHandle(context.mapping_handle);
        CloseHandle(context.handle);
        context = {};
        return false;
    }

    return true;
}

void release_preload(PreloadContext& context) {
    VirtualUnlock(context.view, context.size);
    CloseHandle(context.mapping_handle);
    CloseHandle(context.handle);
    context = {};
}

#else

struct PreloadContext {};

// The OS already preloads the executable on Linux and MacOS.
bool preload_executable(PreloadContext&) {
    return true;
}

void release_preload(PreloadContext&) {
}

#endif

void enable_texture_pack(recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
    recompui::renderer::enable_texture_pack(context, mod);
}

void disable_texture_pack(recomp::mods::ModContext&, const recomp::mods::ModHandle& mod) {
    recompui::renderer::disable_texture_pack(mod);
}

void reorder_texture_pack(recomp::mods::ModContext&) {
    recompui::renderer::trigger_texture_pack_update();
}

int main(int argc, char** argv) {
    recomp::Version project_version{};
    if (!recomp::Version::from_string(version_string, project_version)) {
        ultramodern::error_handling::message_box(("Invalid version string: " + version_string).c_str());
        return EXIT_FAILURE;
    }

    PreloadContext preload_context;
    bool preloaded = preload_executable(preload_context);

    if (!preloaded) {
        fprintf(stderr, "Failed to preload executable!\n");
    }

#ifdef _WIN32
    // Set up high resolution timing period.
    timeBeginPeriod(1);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show-console") == 0) {
            if (GetConsoleWindow() == nullptr) {
                AllocConsole();
                freopen("CONIN$", "r", stdin);
                freopen("CONOUT$", "w", stderr);
                freopen("CONOUT$", "w", stdout);
            }
            break;
        }
    }

    // Set up console output to accept UTF-8 on windows
    SetConsoleOutputCP(CP_UTF8);

    // Force wasapi on Windows, as there seems to be some issue with sample queueing with directsound currently.
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif

#if defined(__linux__) && defined(RECOMP_FLATPAK)
    // When using Flatpak, applications tend to launch from the home directory by default.
    // Mods might use the current working directory to store the data, so we switch it to a directory
    // with persistent data storage and write permissions under Flatpak to ensure it works.
    std::error_code ec;
    std::filesystem::current_path("/var/data", ec);
#endif

    // Initialize native file dialogs.
    NFD_Init();

    recompui::programconfig::set_program_name(bodyharvest::program_name);
    recompui::programconfig::set_program_id(bodyharvest::program_id);

    // Initialize SDL audio and set the output frequency.
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!bodyharvest::audio::reset_audio(48000)) {
        // It is not possible to initialize without an audio device.
        return EXIT_FAILURE;
    }

    std::u8string controller_db_path = (recompui::file::get_program_path() / "recompcontrollerdb.txt").u8string();
    if (SDL_GameControllerAddMappingsFromFile(reinterpret_cast<const char *>(controller_db_path.c_str())) < 0) {
        fprintf(stderr, "Failed to load controller mappings: %s\n", SDL_GetError());
    }

    recompui::register_primary_font("InterVariable.ttf", "Inter Variable");

    recomp::register_config_path(recompui::file::get_app_folder_path());

    for (const auto& game : supported_games) {
        recomp::register_game(game);
    }

    recompui::register_ui_exports();
    recomputil::register_data_api_exports();
    bodyharvest::register_print_exports();
    recomptheme::set_custom_theme();

    // The default launcher without the Mods option: mod support isn't public yet.
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        const recomp::GameEntry& game = supported_games[0];
        recompui::GameOptionsMenu* options = menu->init_game_options_menu(game.game_id, game.mod_game_id, game.display_name, game.thumbnail_bytes);
        recompui::update_game_mod_id(game.mod_game_id);
        options->add_start_game_or_load_rom_option();
        options->add_setup_controls_option();
        options->add_settings_option();
        options->add_exit_option();
    });

    bodyharvest::register_overlays();
    bodyharvest::register_patches();

    recompinput::players::set_single_player_mode(true);

    bodyharvest::init_config();

    recomp::mods::ModContentType texture_pack_content_type{
        .content_filename = "rt64.json",
        .allow_runtime_toggle = true,
        .on_enabled = enable_texture_pack,
        .on_disabled = disable_texture_pack,
        .on_reordered = reorder_texture_pack,
    };
    auto texture_pack_content_type_id = recomp::mods::register_mod_content_type(texture_pack_content_type);

    // Register the .rtz texture pack file format with the previous content type as its only allowed content type.
    recomp::mods::register_mod_container_type("rtz", std::vector{ texture_pack_content_type_id }, false);

    recomp::Configuration cfg{
        .argc = argc,
        .argv = argv,
        .project_version = project_version,
        .rsp_callbacks = {
            .get_rsp_microcode = bodyharvest::get_rsp_microcode,
        },
        .renderer_callbacks = {
            .create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
                auto presentation_mode = ultramodern::renderer::PresentationMode::PresentEarly;
                return recompui::renderer::create_render_context(rdram, window_handle, presentation_mode, developer_mode);
            },
        },
        .audio_callbacks = {
            .queue_samples = bodyharvest::audio::queue_samples,
            .get_frames_remaining = bodyharvest::audio::get_frames_remaining,
            .set_frequency = bodyharvest::audio::set_frequency,
        },
        .input_callbacks = {
            .poll_input = recompinput::poll_inputs,
            .get_input = recompinput::profiles::get_n64_input,
            .set_rumble = recompinput::set_rumble,
            .get_connected_device_info = get_connected_device_info,
        },
        .gfx_callbacks = {
            .create_gfx = create_gfx,
            .create_window = create_window,
            .update_gfx = update_gfx,
        },
        .events_callbacks = {
            .vi_callback = recompinput::update_rumble,
            .gfx_init_callback = nullptr,
        },
        .error_handling_callbacks = {
            .message_box = recompui::message_box,
        },
        .threads_callbacks = {
            .get_game_thread_name = bodyharvest::get_game_thread_name,
        },
    };

    recomp::start(cfg);

    NFD_Quit();

    if (preloaded) {
        release_preload(preload_context);
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    return EXIT_SUCCESS;
}
