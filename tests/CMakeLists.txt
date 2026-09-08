ecm_add_tests(osdactions.cpp LINK_LIBRARIES Qt6::Test Qt6::DBus KF6::Screen kscreen_common)
target_compile_definitions(osdactions PRIVATE "-DTEST_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/\"")

add_executable(presentationmodedbustest
    presentationmodedbustest.cpp
    ../plasmoid/kscreenapplet.cpp
)
target_include_directories(presentationmodedbustest PRIVATE ../plasmoid)
target_compile_definitions(presentationmodedbustest PRIVATE TRANSLATION_DOMAIN="plasma_applet_org.kde.kscreen")
target_link_libraries(presentationmodedbustest PRIVATE
    Qt6::Test
    Qt6::DBus
    Qt6::Qml
    KF6::I18n
    KF6::Service
    KF6::Screen
    Plasma::Plasma
    kscreen_common
)
add_test(NAME presentationmodedbustest COMMAND dbus-run-session $<TARGET_FILE:presentationmodedbustest>)
set_tests_properties(presentationmodedbustest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# kcmshell6 < 6.27.0 doesn't know the --smoke-test param, so skip this test for
# now.
# TODO: Drop this when KF6_MIN_VERSION >= 6.27.0
if(KF6KCMUtils_VERSION VERSION_GREATER_EQUAL "6.27.0")
    add_test(NAME kcm_smoketest COMMAND kcmshell6 --smoke-test kcm_kscreen)
    set_tests_properties(kcm_smoketest PROPERTIES
        ENVIRONMENT_MODIFICATION QT_PLUGIN_PATH=path_list_prepend:${CMAKE_BINARY_DIR}/bin
    )
endif()
