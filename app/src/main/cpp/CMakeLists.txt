# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.22.1)

project("byplanes")

# Creates your game shared library. The name must be the same as the
# one used for loading in your Kotlin/Java or AndroidManifest.txt files.
add_library(byplanes SHARED
        main.cpp
        AndroidOut.cpp
        Renderer.cpp
        Shader.cpp
        TextureAsset.cpp
        Utility.cpp
        Game.cpp
        GameSession.cpp
        InputManager.cpp
        Background.cpp
        Plane.cpp
        Projectile.cpp
        Sprite.cpp
        HUD.cpp
        AIController.cpp
        BluetoothBridge.cpp
        BluetoothVirtualServer.cpp
        SoundEngine.cpp)

if(ANDROID)
    target_link_options(byplanes PRIVATE
            "-Wl,-z,max-page-size=16384"
            "-Wl,-z,common-page-size=16384")
endif()

# Searches for a package provided by the game activity dependency
find_package(game-activity REQUIRED CONFIG)

# Configure libraries CMake uses to link your target library.
target_link_libraries(byplanes
        # The game activity
        game-activity::game-activity

        # EGL and other dependent libraries required for drawing
        # and interacting with Android system
        EGL
        GLESv3
        jnigraphics
        android
        OpenSLES
        log)
