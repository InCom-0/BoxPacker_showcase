#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>

#define _EM_JS_INLINE(ret, c_name, js_name, params, code)                                              \
    extern "C" {                                                                                        \
    ret c_name params EM_IMPORT(js_name);                                                               \
    EMSCRIPTEN_KEEPALIVE                                                                                \
    __attribute__((section("em_js"), aligned(1))) inline char __em_js__##js_name[] = #params "<::>" \
                                                                                              code;      \
    }

#define EM_JS_INLINE(ret, name, params, ...) _EM_JS_INLINE(ret, name, name, params, #__VA_ARGS__)
#endif

namespace emscripten_file_picker_async {

using upload_handler = void (*)(std::string const &, std::string const &, std::string_view, void *);

#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE inline int bp_async_upload_file_return(char const *filename, char const *mime_type,
                                                            char *buffer, size_t buffer_size,
                                                            upload_handler callback, void *callback_data);
}

EM_JS_INLINE(void, bp_pick_file_async_js_impl,
             (char const *accept_types, upload_handler callback, void *callback_data), {
    try {
    const accept_ptr = (typeof accept_types === "bigint") ? Number(accept_types) : accept_types;
    const accept = UTF8ToString(accept_ptr);

        let input = Module.__bp_async_file_input;
        if (!input) {
            input = document.createElement("input");
            input.type = "file";
            input.style.display = "none";
            document.body.appendChild(input);
            Module.__bp_async_file_input = input;
        }

        input.accept = accept;
        input.value = "";

        input.onchange = async () => {
            let dataPtr = 0;
            try {
                const f = input.files && input.files[0];
                if (!f) {
                    Module.ccall("bp_async_upload_file_return", "number",
                                ["string", "string", "pointer", "number", "pointer", "pointer"],
                                ["", "", 0, 0, callback, callback_data]);
                    return;
                }

                const ab = await f.arrayBuffer();
                const bytes = new Uint8Array(ab);

                dataPtr = Module._malloc(bytes.length);
                Module.HEAPU8.set(bytes, dataPtr);

                Module.ccall("bp_async_upload_file_return", "number",
                            ["string", "string", "pointer", "number", "pointer", "pointer"],
                            [f.name || "", f.type || "", dataPtr, bytes.length, callback, callback_data]);
            } catch (e) {
                console.error("bp async picker onchange failed:", e);
            } finally {
                if (dataPtr) {
                    Module._free(dataPtr);
                }
                input.value = "";
            }
        };

        try {
            input.click();
        } catch (e) {
            console.error("bp async picker click failed:", e);
        }
    } catch (e) {
        console.error("bp async picker setup failed:", e);
    }
});

inline int bp_async_upload_file_return(char const *filename, char const *mime_type, char *buffer, size_t buffer_size,
                                       upload_handler callback, void *callback_data) {
    if (!callback) {
        return 0;
    }
    if (!buffer || buffer_size == 0) {
        callback(filename ? filename : "", mime_type ? mime_type : "", std::string_view{}, callback_data);
        return 1;
    }

    callback(filename ? filename : "", mime_type ? mime_type : "", std::string_view{buffer, buffer_size},
             callback_data);
    return 1;
}

inline void upload(std::string const &accept_types, upload_handler callback, void *callback_data = nullptr) {
#if defined(__EMSCRIPTEN_PTHREADS__)
    if (!emscripten_is_main_runtime_thread()) {
        size_t const accept_size = accept_types.size() + 1;
        char *       accept_copy = static_cast<char *>(std::malloc(accept_size));
        if (!accept_copy) {
            return;
        }
        std::memcpy(accept_copy, accept_types.c_str(), accept_size);

        MAIN_THREAD_ASYNC_EM_ASM(
            {
                try {
                    bp_pick_file_async_js_impl($0, $1, $2);
                } catch (e) {
                    console.error("bp async picker dispatch failed:", e);
                }
                _free($0);
            },
            accept_copy, callback, callback_data);
        return;
    }
#endif

    bp_pick_file_async_js_impl(accept_types.c_str(), callback, callback_data);
}
#else
inline void upload(std::string const &, upload_handler, void * = nullptr) {}
#endif

} // namespace emscripten_file_picker_async
