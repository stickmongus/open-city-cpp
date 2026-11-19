#!/usr/bin/env python3
"""
Idempotent patch for raylib 5.5 on Android to enable native OpenGL ES 3.0,
24-bit depth buffer, and Khronos-compliant GL_DEPTH_COMPONENT24 shadow maps.
"""
import pathlib
import sys


def patch_file(path: pathlib.Path, replacements: list[tuple[str, str]]) -> None:
    text = path.read_text(encoding="utf-8")
    changed = False
    for old, new in replacements:
        if new in text:
            continue
        if old not in text:
            raise RuntimeError(f"Expected snippet not found in {path}:\n{old}")
        text = text.replace(old, new, 1)
        changed = True
    if changed:
        path.write_text(text, encoding="utf-8")
        print(f"[patch_raylib_android] Patched {path}")
    else:
        print(f"[patch_raylib_android] Already patched {path}")


def main() -> int:
    raylib_root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()

    # 1) cmake/LibraryConfigurations.cmake: enable GRAPHICS_API_OPENGL_ES3 & link GLESv3 on Android
    patch_file(
        raylib_root / "cmake" / "LibraryConfigurations.cmake",
        [
            (
                'elseif (${PLATFORM} MATCHES "Android")\n'
                '    set(PLATFORM_CPP "PLATFORM_ANDROID")\n'
                '    set(GRAPHICS "GRAPHICS_API_OPENGL_ES2")',
                'elseif (${PLATFORM} MATCHES "Android")\n'
                '    set(PLATFORM_CPP "PLATFORM_ANDROID")\n'
                '    set(GRAPHICS "GRAPHICS_API_OPENGL_ES3")',
            ),
            (
                "    set(LIBS_PRIVATE m log android EGL GLESv2 OpenSLES atomic c)",
                "    set(LIBS_PRIVATE m log android EGL GLESv3 OpenSLES atomic c)",
            ),
        ],
    )

    # 2) src/platforms/rcore_android.c: request EGL ES 3.0 context & 24-bit depth buffer
    patch_file(
        raylib_root / "src" / "platforms" / "rcore_android.c",
        [
            (
                "        EGL_DEPTH_SIZE, 16,         // Depth buffer size (Required to use Depth testing!)",
                "        EGL_DEPTH_SIZE, 24,         // 24-bit depth buffer for large open-world draw distance",
            ),
            (
                "        EGL_CONTEXT_CLIENT_VERSION, 2,",
                "        EGL_CONTEXT_CLIENT_VERSION, (rlGetVersion() == RL_OPENGL_ES_30)? 3 : 2,",
            ),
        ],
    )

    # 3) src/rlgl.h:
    #    - use sized GL_DEPTH_COMPONENT24_OES instead of unsized WebGL GL_DEPTH_COMPONENT on Android ES3
    #    - call core OpenGL ES 3.0 glDrawBuffers() instead of ES2 extension glDrawBuffersEXT()
    patch_file(
        raylib_root / "src" / "rlgl.h",
        [
            (
                "    RLGL.ExtSupported.texDepth = true;\n"
                "    RLGL.ExtSupported.texDepthWebGL = true;\n"
                "    RLGL.ExtSupported.maxDepthBits = 24;",
                "    RLGL.ExtSupported.texDepth = true;\n"
                "#if defined(PLATFORM_ANDROID)\n"
                "    RLGL.ExtSupported.texDepthWebGL = false;\n"
                "#else\n"
                "    RLGL.ExtSupported.texDepthWebGL = true;\n"
                "#endif\n"
                "    RLGL.ExtSupported.maxDepthBits = 24;",
            ),
            (
                "#if defined(GRAPHICS_API_OPENGL_ES3)\n"
                "            glDrawBuffersEXT(count, buffers);\n"
                "#else",
                "#if defined(GRAPHICS_API_OPENGL_ES3)\n"
                "            glDrawBuffers(count, buffers);\n"
                "#else",
            ),
        ],
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
