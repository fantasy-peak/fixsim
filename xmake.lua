add_rules("mode.release", "mode.debug")

set_project("fixsim")
set_version("1.0.0", {build = "%Y%m%d%H%M"})
set_xmakever("2.9.9")

add_defines("NOT_USE_YCS_INIT_VALUE", "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG")

add_repositories("my_private_repo https://github.com/fantasy-peak/xmake-repo.git")

add_requires("asio asio-1-34-2", "cpp-httplib v0.18.0")
add_requires("spdlog", {configs={std_format=true}})
add_requires("yaml_cpp_struct", "nlohmann_json", "quickfix", "uuid", "pugixml")

set_languages("c++23")
add_includedirs("include")

target("fixsim")
    set_kind("binary")
    add_files("src/*.cpp")
    add_ldflags("-static-libstdc++", "-static-libgcc", {force = true})
    add_packages("yaml_cpp_struct", "nlohmann_json", "spdlog", "quickfix", "asio", "uuid", "pugixml", "cpp-httplib")
target_end()
