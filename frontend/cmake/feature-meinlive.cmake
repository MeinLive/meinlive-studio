# MeinLive Studio: Konto, Live gehen, Einblendungen, Vorlagen, Docks, Updates
# (siehe frontend/meinlive/ und meinlive/studio-version.cmake)

target_sources(
  obs-studio
  PRIVATE
    meinlive/MeinLive.cpp
    meinlive/MeinLive.hpp
    meinlive/MeinLiveAccount.cpp
    meinlive/MeinLiveAccount.hpp
    meinlive/MeinLiveDocks.cpp
    meinlive/MeinLiveDocks.hpp
    meinlive/MeinLiveGoLive.cpp
    meinlive/MeinLiveGoLive.hpp
    meinlive/MeinLiveHttp.cpp
    meinlive/MeinLiveHttp.hpp
    meinlive/MeinLiveScenes.cpp
    meinlive/MeinLiveScenes.hpp
    meinlive/MeinLiveUpdate.cpp
    meinlive/MeinLiveUpdate.hpp
    meinlive/meinlive.qrc
)

target_compile_definitions(
  obs-studio
  PRIVATE
    MEINLIVE_STUDIO_VERSION="${MEINLIVE_STUDIO_VERSION}"
    MEINLIVE_STUDIO_VERSION_CODE=${MEINLIVE_STUDIO_VERSION_CODE}
)

if(NOT TARGET nlohmann_json::nlohmann_json)
  find_package(nlohmann_json 3.11 REQUIRED)
endif()
target_link_libraries(obs-studio PRIVATE nlohmann_json::nlohmann_json)

if(OS_WINDOWS)
  target_link_libraries(obs-studio PRIVATE crypt32)
endif()
