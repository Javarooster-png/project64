cmake_minimum_required(VERSION 3.16)

project(Project64Linux LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(LIBPNG REQUIRED libpng)
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
find_package(OpenGL REQUIRED)
find_library(OPENGL_LEGACY_GL_LIBRARY GL)
if(NOT OPENGL_LEGACY_GL_LIBRARY)
    message(FATAL_ERROR "Could not find legacy OpenGL libGL")
endif()

set(PJ64_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(PJ64_SOURCE "${PJ64_ROOT}/Source")

configure_file(
    "${PJ64_SOURCE}/Project64-core/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-core/Version.h"
    COPYONLY)
configure_file(
    "${PJ64_SOURCE}/Project64-video/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-video/Version.h"
    COPYONLY)
configure_file(
    "${PJ64_SOURCE}/Project64-input/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-input/Version.h"
    COPYONLY)
configure_file(
    "${PJ64_SOURCE}/Project64-rsp-core/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-rsp-core/Version.h"
    COPYONLY)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/GLideN64")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/GLideN64/Revision.h"
    "#pragma once\n#define PLUGIN_REVISION \"project64-linux\"\n")

file(GLOB_RECURSE ASMJIT_SOURCES CONFIGURE_DEPENDS
    "${PJ64_SOURCE}/3rdParty/asmjit/src/asmjit/*.cpp")
add_library(asmjit STATIC ${ASMJIT_SOURCES})
target_compile_definitions(asmjit PUBLIC ASMJIT_STATIC)
target_include_directories(asmjit PUBLIC "${PJ64_SOURCE}/3rdParty/asmjit/src")

add_library(Common STATIC
    Source/Common/CriticalSection.cpp
    Source/Common/DateTime.cpp
    Source/Common/DynamicLibrary.cpp
    Source/Common/File.cpp
    Source/Common/HighResTimeStamp.cpp
    Source/Common/IniFile.cpp
    Source/Common/Log.cpp
    Source/Common/md5.cpp
    Source/Common/MemoryManagement.cpp
    Source/Common/path.cpp
    Source/Common/Platform.cpp
    Source/Common/Random.cpp
    Source/Common/StdString.cpp
    Source/Common/SyncEvent.cpp
    Source/Common/Thread.cpp
    Source/Common/Trace.cpp
    Source/Common/Util.cpp)
target_include_directories(Common PUBLIC "${PJ64_SOURCE}")
target_compile_options(Common PRIVATE
    "SHELL:-include cstring"
    "SHELL:-include ctime"
    "SHELL:-include strings.h")
target_link_libraries(Common PUBLIC dl pthread)

add_library(settings STATIC
    Source/Settings/Settings.cpp)
target_include_directories(settings PUBLIC "${PJ64_SOURCE}")
target_compile_options(settings PRIVATE
    "SHELL:-include cstring"
    "SHELL:-include ctime"
    "SHELL:-include strings.h")

add_library(zlib STATIC
    Source/3rdParty/zlib/adler32.c
    Source/3rdParty/zlib/compress.c
    Source/3rdParty/zlib/crc32.c
    Source/3rdParty/zlib/deflate.c
    Source/3rdParty/zlib/infback.c
    Source/3rdParty/zlib/inffast.c
    Source/3rdParty/zlib/inflate.c
    Source/3rdParty/zlib/inftrees.c
    Source/3rdParty/zlib/trees.c
    Source/3rdParty/zlib/uncompr.c
    Source/3rdParty/zlib/zutil.c)
target_include_directories(zlib PUBLIC "${PJ64_SOURCE}/3rdParty/zlib")

add_library(minizip STATIC
    Source/3rdParty/zlib/contrib/minizip/ioapi.c
    Source/3rdParty/zlib/contrib/minizip/unzip.c
    Source/3rdParty/zlib/contrib/minizip/zip.c)
target_include_directories(minizip PUBLIC
    "${PJ64_SOURCE}/3rdParty/zlib"
    "${PJ64_SOURCE}/3rdParty/zlib/contrib/minizip")
target_link_libraries(minizip PUBLIC zlib)

execute_process(
    COMMAND awk "/^OBJS_/ {collect=1} collect { line=$0; sub(/#.*/, \"\", line); while (match(line, /[A-Za-z0-9_]+\\$\\(OBJ\\)/)) { name=substr(line, RSTART, RLENGTH-6); print name; line=substr(line, RSTART+RLENGTH) } } collect && /^$/ {collect=0}"
    "${PJ64_SOURCE}/3rdParty/softfloat-3e/build/Linux-x86_64-GCC/Makefile"
    OUTPUT_VARIABLE SOFTFLOAT_OBJECT_NAMES
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "\n" ";" SOFTFLOAT_OBJECT_NAMES "${SOFTFLOAT_OBJECT_NAMES}")
set(SOFTFLOAT_SOURCES)
foreach(SOFTFLOAT_OBJECT_NAME IN LISTS SOFTFLOAT_OBJECT_NAMES)
    if(EXISTS "${PJ64_SOURCE}/3rdParty/softfloat-3e/source/8086-SSE/${SOFTFLOAT_OBJECT_NAME}.c")
        list(APPEND SOFTFLOAT_SOURCES "${PJ64_SOURCE}/3rdParty/softfloat-3e/source/8086-SSE/${SOFTFLOAT_OBJECT_NAME}.c")
    else()
        list(APPEND SOFTFLOAT_SOURCES "${PJ64_SOURCE}/3rdParty/softfloat-3e/source/${SOFTFLOAT_OBJECT_NAME}.c")
    endif()
endforeach()
add_library(softfloat STATIC ${SOFTFLOAT_SOURCES})
target_sources(softfloat PRIVATE Source/Project64-linux/softfloat_primitives.c)
target_compile_definitions(softfloat PRIVATE
    SOFTFLOAT_FAST_INT64
    SOFTFLOAT_ROUND_ODD
    INLINE_LEVEL=5
    SOFTFLOAT_FAST_DIV32TO16
    SOFTFLOAT_FAST_DIV64TO32)
target_compile_options(softfloat PRIVATE -fgnu89-inline)
target_include_directories(softfloat PUBLIC
    "${PJ64_SOURCE}/3rdParty/softfloat-3e/source/8086-SSE"
    "${PJ64_SOURCE}/3rdParty/softfloat-3e/build/Linux-x86_64-GCC"
    "${PJ64_SOURCE}/3rdParty/softfloat-3e/source/include")

add_library(Project64-core STATIC
    Source/Project64-core/AppInit.cpp
    Source/Project64-core/Logging.cpp
    Source/Project64-core/Multilanguage/Language.cpp
    Source/Project64-core/N64System/Enhancement/Enhancement.cpp
    Source/Project64-core/N64System/Enhancement/EnhancementFile.cpp
    Source/Project64-core/N64System/Enhancement/EnhancementList.cpp
    Source/Project64-core/N64System/Enhancement/Enhancements.cpp
    Source/Project64-core/N64System/EmulationThread.cpp
    Source/Project64-core/N64System/FramePerSecond.cpp
    Source/Project64-core/N64System/Interpreter/InterpreterOps.cpp
    Source/Project64-core/N64System/MemoryHandler/AudioInterfaceHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/CartridgeDomain1Address1Handler.cpp
    Source/Project64-core/N64System/MemoryHandler/CartridgeDomain1Address3Handler.cpp
    Source/Project64-core/N64System/MemoryHandler/CartridgeDomain2Address1Handler.cpp
    Source/Project64-core/N64System/MemoryHandler/CartridgeDomain2Address2Handler.cpp
    Source/Project64-core/N64System/MemoryHandler/DisplayControlRegHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/ISViewerHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/MIPSInterfaceHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/PeripheralInterfaceHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/PifRamHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/RDRAMInterfaceHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/RDRAMRegistersHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/RomMemoryHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/SerialInterfaceHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/SPRegistersHandler.cpp
    Source/Project64-core/N64System/MemoryHandler/VideoInterfaceHandler.cpp
    Source/Project64-core/N64System/Mips/Disk.cpp
    Source/Project64-core/N64System/Mips/GBCart.cpp
    Source/Project64-core/N64System/Mips/Mempak.cpp
    Source/Project64-core/N64System/Mips/MemoryVirtualMem.cpp
    Source/Project64-core/N64System/Mips/R4300iInstruction.cpp
    Source/Project64-core/N64System/Mips/Register.cpp
    Source/Project64-core/N64System/Mips/Rumblepak.cpp
    Source/Project64-core/N64System/Mips/SystemEvents.cpp
    Source/Project64-core/N64System/Mips/SystemTiming.cpp
    Source/Project64-core/N64System/Mips/TLB.cpp
    Source/Project64-core/N64System/Mips/Transferpak.cpp
    Source/Project64-core/N64System/N64Disk.cpp
    Source/Project64-core/N64System/N64Rom.cpp
    Source/Project64-core/N64System/N64System.cpp
    Source/Project64-core/N64System/Profiling.cpp
    Source/Project64-core/N64System/Recompiler/CodeBlock.cpp
    Source/Project64-core/N64System/Recompiler/CodeSection.cpp
    Source/Project64-core/N64System/Recompiler/ExitInfo.cpp
    Source/Project64-core/N64System/Recompiler/FunctionInfo.cpp
    Source/Project64-core/N64System/Recompiler/FunctionMap.cpp
    Source/Project64-core/N64System/Recompiler/JumpInfo.cpp
    Source/Project64-core/N64System/Recompiler/LoopAnalysis.cpp
    Source/Project64-core/N64System/Recompiler/RegBase.cpp
    Source/Project64-core/N64System/Recompiler/Recompiler.cpp
    Source/Project64-core/N64System/Recompiler/RecompilerMemory.cpp
    Source/Project64-core/N64System/Recompiler/RecompilerOps.cpp
    Source/Project64-core/N64System/Recompiler/x64-86/x64ops.cpp
    Source/Project64-core/N64System/Recompiler/x64-86/x64RecompilerOps.cpp
    Source/Project64-core/N64System/Recompiler/x64-86/x64RegInfo.cpp
    Source/Project64-core/N64System/Recompiler/x86/x86ops.cpp
    Source/Project64-core/N64System/Recompiler/x86/x86RecompilerOps.cpp
    Source/Project64-core/N64System/Recompiler/x86/x86RegInfo.cpp
    Source/Project64-core/N64System/SaveType/Eeprom.cpp
    Source/Project64-core/N64System/SaveType/FlashRam.cpp
    Source/Project64-core/N64System/SaveType/Sram.cpp
    Source/Project64-core/N64System/SpeedLimiter.cpp
    Source/Project64-core/N64System/SystemGlobals.cpp
    Source/Project64-core/Plugins/AudioPlugin.cpp
    Source/Project64-core/Plugins/ControllerPlugin.cpp
    Source/Project64-core/Plugins/GFXPlugin.cpp
    Source/Project64-core/Plugins/Plugin.cpp
    Source/Project64-core/Plugins/PluginBase.cpp
    Source/Project64-core/Plugins/RSPPlugin.cpp
    Source/Project64-core/RomList/RomList.cpp
    Source/Project64-core/Settings.cpp
    Source/Project64-core/Settings/DebugSettings.cpp
    Source/Project64-core/Settings/GameSettings.cpp
    Source/Project64-core/Settings/LoggingSettings.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-Application.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-ApplicationIndex.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-ApplicationPath.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-GameSetting.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-GameSettingIndex.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RDB.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RDBCpuType.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RDBLinking.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RDBOnOff.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RDBRamSize.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RDBSaveChip.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RDBUser.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RelativePath.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RomDatabase.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RomDatabaseIndex.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-RomDatabaseSetting.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-SelectedDirectory.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-TempBool.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-TempNumber.cpp
    Source/Project64-core/Settings/SettingType/SettingsType-TempString.cpp
    Source/Project64-core/Settings/SystemSettings.cpp
    Source/Project64-rsp-core/cpu/RSPRegisterHandler.cpp)
target_include_directories(Project64-core PUBLIC
    "${PJ64_SOURCE}"
    "${PJ64_SOURCE}/3rdParty/asmjit/src"
    "${PJ64_SOURCE}/3rdParty/softfloat-3e/source/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-core"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(Project64-core PRIVATE
    -fpermissive
    "SHELL:-include cstring"
    "SHELL:-include ctime"
    "SHELL:-include strings.h")
target_compile_definitions(Project64-core PRIVATE
    PJ64_CORE_RSP_REGISTER_HANDLER_ONLY
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:__debugbreak=__builtin_trap>)
target_link_libraries(Project64-core PUBLIC Common settings asmjit softfloat minizip)

file(GLOB PROJECT64_VIDEO_SOURCES CONFIGURE_DEPENDS
    "${PJ64_SOURCE}/Project64-video/*.cpp"
    "${PJ64_SOURCE}/Project64-video/Renderer/OGL*.cpp"
    "${PJ64_SOURCE}/Project64-video/Renderer/Renderer.cpp"
    "${PJ64_SOURCE}/Project64-video/TextureEnhancer/*.cpp"
    "${PJ64_SOURCE}/Project64-video/TextureEnhancer/tc-1.1+/*.c")
list(REMOVE_ITEM PROJECT64_VIDEO_SOURCES
    "${PJ64_SOURCE}/Project64-video/Android.cpp"
    "${PJ64_SOURCE}/Project64-video/Config.cpp"
    "${PJ64_SOURCE}/Project64-video/Renderer/OGLEScombiner.cpp"
    "${PJ64_SOURCE}/Project64-video/Renderer/OGLESgeometry.cpp"
    "${PJ64_SOURCE}/Project64-video/Renderer/OGLESglitchmain.cpp"
    "${PJ64_SOURCE}/Project64-video/Renderer/OGLEStextures.cpp"
    "${PJ64_SOURCE}/Project64-video/Renderer/OGLESwrappers.cpp")
add_library(Project64-video SHARED ${PROJECT64_VIDEO_SOURCES})
set_target_properties(Project64-video PROPERTIES
    OUTPUT_NAME Project64-video
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(Project64-video PRIVATE
    "${PJ64_SOURCE}"
    "${PJ64_SOURCE}/Project64-video"
    "${PJ64_SOURCE}/3rdParty"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-video"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${SDL2_INCLUDE_DIRS}
    ${LIBPNG_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIRS})
target_compile_definitions(Project64-video PRIVATE
    NO_ASM)
target_compile_options(Project64-video PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:${PJ64_SOURCE}/Project64-linux/linux_force_include.h>
    $<$<COMPILE_LANGUAGE:C>:-include>
    $<$<COMPILE_LANGUAGE:C>:string.h>
    ${SDL2_CFLAGS_OTHER}
    ${LIBPNG_CFLAGS_OTHER})
target_link_options(Project64-video PRIVATE -Wl,-Bsymbolic)
target_link_libraries(Project64-video PRIVATE
    Common
    settings
    ${LIBPNG_LIBRARIES}
    OpenGL::GL
    ${OPENGL_LEGACY_GL_LIBRARY})

set(GLIDEN64_SRC "${PJ64_SOURCE}/GLideN64/Source/src")
set(GLIDEN64_SOURCES
    ${GLIDEN64_SRC}/3DMath.cpp
    ${GLIDEN64_SRC}/Combiner.cpp
    ${GLIDEN64_SRC}/CombinerKey.cpp
    ${GLIDEN64_SRC}/CommonPluginAPI.cpp
    ${GLIDEN64_SRC}/Config.cpp
    ${GLIDEN64_SRC}/convert.cpp
    ${GLIDEN64_SRC}/CRC32.cpp
    ${GLIDEN64_SRC}/DebugDump.cpp
    ${GLIDEN64_SRC}/Debugger.cpp
    ${GLIDEN64_SRC}/DepthBuffer.cpp
    ${GLIDEN64_SRC}/DisplayWindow.cpp
    ${GLIDEN64_SRC}/DisplayLoadProgress.cpp
    ${GLIDEN64_SRC}/FrameBuffer.cpp
    ${GLIDEN64_SRC}/FrameBufferInfo.cpp
    ${GLIDEN64_SRC}/GBI.cpp
    ${GLIDEN64_SRC}/gDP.cpp
    ${GLIDEN64_SRC}/GLideN64.cpp
    ${GLIDEN64_SRC}/GraphicsDrawer.cpp
    ${GLIDEN64_SRC}/gSP.cpp
    ${GLIDEN64_SRC}/Log.cpp
    ${GLIDEN64_SRC}/N64.cpp
    ${GLIDEN64_SRC}/PaletteTexture.cpp
    ${GLIDEN64_SRC}/Performance.cpp
    ${GLIDEN64_SRC}/PostProcessor.cpp
    ${GLIDEN64_SRC}/RDP.cpp
    ${GLIDEN64_SRC}/RSP.cpp
    ${GLIDEN64_SRC}/RSP_LoadMatrix.cpp
    ${GLIDEN64_SRC}/SoftwareRender.cpp
    ${GLIDEN64_SRC}/TexrectDrawer.cpp
    ${GLIDEN64_SRC}/TextDrawerStub.cpp
    ${GLIDEN64_SRC}/TextureFilterHandler.cpp
    ${GLIDEN64_SRC}/Textures.cpp
    ${GLIDEN64_SRC}/TxFilterStub.cpp
    ${GLIDEN64_SRC}/VI.cpp
    ${GLIDEN64_SRC}/ZilmarPluginAPI.cpp
    ${GLIDEN64_SRC}/ZlutTexture.cpp
    ${GLIDEN64_SRC}/BufferCopy/BlueNoiseTexture.cpp
    ${GLIDEN64_SRC}/BufferCopy/ColorBufferToRDRAM.cpp
    ${GLIDEN64_SRC}/BufferCopy/DepthBufferToRDRAM.cpp
    ${GLIDEN64_SRC}/BufferCopy/RDRAMtoColorBuffer.cpp
    ${GLIDEN64_SRC}/DepthBufferRender/ClipPolygon.cpp
    ${GLIDEN64_SRC}/DepthBufferRender/DepthBufferRender.cpp
    ${GLIDEN64_SRC}/common/CommonAPIImpl_common.cpp
    ${GLIDEN64_SRC}/Graphics/Context.cpp
    ${GLIDEN64_SRC}/Graphics/ColorBufferReader.cpp
    ${GLIDEN64_SRC}/Graphics/CombinerProgram.cpp
    ${GLIDEN64_SRC}/Graphics/ObjectHandle.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLFunctions.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_Command.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_ObjectPool.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_Wrapper.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_WrappedFunctions.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/ThreadedOpenGl/RingBufferPool.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_Attributes.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_BufferedDrawer.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_CachedFunctions.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_ColorBufferReaderWithBufferStorage.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_ColorBufferReaderWithPixelBuffer.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_ColorBufferReaderWithReadPixels.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_ContextImpl.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_GLInfo.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_Parameters.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_UnbufferedDrawer.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/opengl_Utils.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerInputs.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilder.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderCommon.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderAccurate.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderFast.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactory.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryCommon.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryAccurate.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryFast.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_FXAA.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_ShaderStorage.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_SpecialShadersFactory.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/GLSL/glsl_Utils.cpp
    ${GLIDEN64_SRC}/Graphics/OpenGLContext/project64linux/project64linux_DisplayWindow.cpp
    ${GLIDEN64_SRC}/project64linux/CommonAPIImpl_project64linux.cpp
    ${GLIDEN64_SRC}/project64linux/Config_project64linux.cpp
    ${GLIDEN64_SRC}/project64linux/ZilmarAPIImpl_project64linux.cpp
    ${GLIDEN64_SRC}/mupenplus/MemoryStatus_mupenplus.cpp
    ${GLIDEN64_SRC}/osal/osal_files_unix.c
    ${GLIDEN64_SRC}/osal/osal_keys_linux.c)
file(GLOB GLIDEN64_UCODE_SOURCES CONFIGURE_DEPENDS "${GLIDEN64_SRC}/uCodes/*.cpp")
list(APPEND GLIDEN64_SOURCES ${GLIDEN64_UCODE_SOURCES})
add_library(GLideN64 SHARED ${GLIDEN64_SOURCES})
set_target_properties(GLideN64 PROPERTIES
    OUTPUT_NAME GLideN64
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
target_include_directories(GLideN64 PRIVATE
    "${GLIDEN64_SRC}"
    "${GLIDEN64_SRC}/inc"
    "${GLIDEN64_SRC}/osal"
    "${GLIDEN64_SRC}/GLideNHQ"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/GLideN64")
target_compile_definitions(GLideN64 PRIVATE
    OS_LINUX
    PROJECT64_LINUX
    NO_ASM
    _GNU_SOURCE)
target_compile_options(GLideN64 PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:${PJ64_SOURCE}/Project64-linux/linux_force_include.h>)
target_link_options(GLideN64 PRIVATE -Wl,-Bsymbolic)
target_link_libraries(GLideN64 PRIVATE
    Common
    dl
    pthread
    OpenGL::GL
    ${OPENGL_LEGACY_GL_LIBRARY})

add_library(Project64-input-linux SHARED
    Source/Project64-input/CProject64Input.cpp
    Source/Project64-input/InputMain.cpp
    Source/Project64-input/InputSettings.cpp
    Source/Project64-input/SdlInputBackend.cpp
    Source/Project64-input/Win32KeyboardScan.cpp)
set_target_properties(Project64-input-linux PROPERTIES
    OUTPUT_NAME Project64-input-linux
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(Project64-input-linux PRIVATE
    "${PJ64_SOURCE}"
    "${PJ64_SOURCE}/Project64-input"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-input"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${SDL2_INCLUDE_DIRS})
target_compile_options(Project64-input-linux PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:${PJ64_SOURCE}/Project64-linux/linux_force_include.h>
    ${SDL2_CFLAGS_OTHER})
target_link_options(Project64-input-linux PRIVATE -Wl,-Bsymbolic)
target_link_libraries(Project64-input-linux PRIVATE
    Common
    settings
    ${SDL2_LIBRARIES})

add_library(Project64-audio-linux SHARED
    Source/Project64-linux/stub_audio.cpp)
set_target_properties(Project64-audio-linux PROPERTIES
    OUTPUT_NAME Project64-audio-linux
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(Project64-audio-linux PRIVATE
    "${PJ64_SOURCE}"
    ${SDL2_INCLUDE_DIRS})
target_compile_options(Project64-audio-linux PRIVATE ${SDL2_CFLAGS_OTHER})
target_link_libraries(Project64-audio-linux PRIVATE ${SDL2_LIBRARIES})

file(GLOB_RECURSE PROJECT64_RSP_CORE_SOURCES CONFIGURE_DEPENDS
    "${PJ64_SOURCE}/Project64-rsp-core/*.cpp")
list(FILTER PROJECT64_RSP_CORE_SOURCES EXCLUDE REGEX ".*/Recompiler/.*x86\\.cpp$")
list(FILTER PROJECT64_RSP_CORE_SOURCES EXCLUDE REGEX ".*/Recompiler/.*x64\\.cpp$")
list(FILTER PROJECT64_RSP_CORE_SOURCES EXCLUDE REGEX ".*/Recompiler/(Mmx|Sse|X86)\\.cpp$")
list(FILTER PROJECT64_RSP_CORE_SOURCES EXCLUDE REGEX ".*/Recompiler/(RspAssembler|RspCodeBlock|RspRegState)\\.cpp$")
list(FILTER PROJECT64_RSP_CORE_SOURCES EXCLUDE REGEX ".*/cpu/RSPInstruction-x86\\.cpp$")
add_library(Plugin-rsp SHARED
    Source/Project64-linux/linux_rsp_exports.cpp
    ${PROJECT64_RSP_CORE_SOURCES})
set_target_properties(Plugin-rsp PROPERTIES
    OUTPUT_NAME Plugin-rsp
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(Plugin-rsp PRIVATE
    "${PJ64_SOURCE}"
    "${PJ64_SOURCE}/Project64-rsp-core"
    "${PJ64_SOURCE}/3rdParty"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-rsp-core"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(Plugin-rsp PRIVATE NO_ASM PJ64_RSP_DISABLE_RECOMPILER)
target_compile_options(Plugin-rsp PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:${PJ64_SOURCE}/Project64-linux/linux_force_include.h>)
target_link_libraries(Plugin-rsp PRIVATE
    Common
    settings
    asmjit
    zlib)

add_executable(project64-linux
    Source/Project64-linux/main.cpp)
target_include_directories(project64-linux PRIVATE
    "${PJ64_SOURCE}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${SDL2_INCLUDE_DIRS}
    ${wxWidgets_INCLUDE_DIRS})
target_compile_options(project64-linux PRIVATE ${SDL2_CFLAGS_OTHER} ${wxWidgets_CXX_FLAGS})
target_link_options(project64-linux PRIVATE -rdynamic)
target_link_libraries(project64-linux PRIVATE Project64-core ${SDL2_LIBRARIES} ${OPENGL_LEGACY_GL_LIBRARY} ${wxWidgets_LIBRARIES})
