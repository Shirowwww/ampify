# Windows only: the shared helpers (ampify_win), the kernel DLL, the XAML backend, the launcher and the installer. The
# lists are always defined; CMakeLists.txt uses them only when targeting Windows.
list(APPEND AMPIFY_WIN_SOURCES src/win/win_common.cpp src/win/package.cpp src/win/win_host.cpp src/win/hook.cpp
    src/win/ui_thread.cpp src/win/kit_command.cpp src/win/wic_image.cpp src/win/video_gif.cpp src/win/background.cpp
    src/win/toast_hold.cpp src/win/package_debug.cpp src/win/network_fix.cpp src/win/audio/audio_sharing.cpp
    src/win/audio/audio_hooks.cpp src/win/audio/player_volume.cpp
    src/win/ui_watchdog.cpp src/win/reliability_watch.cpp)

list(APPEND AMPIFY_KERNEL_SOURCES src/kernel/kernel.cpp)

list(APPEND AMPIFY_XAML_SOURCES src/win/xaml_backend.cpp src/win/xaml_artwork.cpp src/win/xaml_decor.cpp src/win/xaml_reparent.cpp
    src/win/xaml_resources.cpp src/win/xaml_properties.cpp src/win/xaml_read.cpp src/win/xaml_motion.cpp src/win/xaml_states.cpp src/win/mini_player_theme.cpp
    src/win/xaml_values.cpp src/win/theme_animations.cpp src/win/theme_service.cpp src/win/xaml_host.cpp src/win/xaml_chrome.cpp src/win/ui_renderer.cpp src/win/ui_renderer_build.cpp src/win/ui_renderer_attributes.cpp src/win/ui_renderer_style.cpp src/win/ui_renderer_playing.cpp src/win/ui_service.cpp
    src/win/ui_service_settings.cpp src/win/ui_service_settings_nav.cpp src/win/ui_motion.cpp src/win/ui_service_mods.cpp src/win/ui_service_settings_io.cpp src/win/ui_service_panels.cpp src/win/ui_service_pages.cpp
    src/win/ui_service_sidebar.cpp src/win/ui_service_menus.cpp src/win/ui_service_playbar.cpp src/win/ui_service_mark.cpp src/win/lyrics_window.cpp
    src/win/apple_player.cpp src/win/apple_tweak.cpp src/win/player_controls.cpp src/win/notifications.cpp src/win/close_to_tray.cpp src/win/audio_bars.cpp
    src/win/xaml_watcher.cpp src/win/element_picker.cpp src/win/lcd_links.cpp src/win/window_backdrop.cpp
    src/win/sharp_artwork.cpp src/win/text_marquee.cpp src/win/sidebar_hover.cpp src/win/sidebar_pane_dress.cpp src/win/dialog_fonts.cpp
    src/win/artwork_fade.cpp src/win/artwork_large.cpp src/win/larger_cover.cpp src/win/moving_artwork.cpp src/win/artwork_cap.cpp src/win/artwork_passthrough.cpp src/win/apple_properties.cpp src/win/hosted_text_align.cpp
    src/win/topbar_search.cpp src/win/pane_corners.cpp src/win/gradient_fit.cpp
    src/win/element_blur.cpp src/win/image_effects.cpp src/win/glass_brush.cpp src/win/width_glide.cpp src/win/presence_watch.cpp)

list(APPEND AMPIFY_LAUNCHER_SOURCES
    src/launcher/launcher.cpp
    src/launcher/injection.cpp)

list(APPEND AMPIFY_SETUP_SOURCES src/setup/setup.cpp)
