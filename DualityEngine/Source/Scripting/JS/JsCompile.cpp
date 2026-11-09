#include "JsCompile.h"

#include <cstring>

#include <quickjs.h>

namespace Duality::Js {

    bool CompileScriptToBytecode(const std::string& source, const std::string& fileName,
                                 std::vector<unsigned char>& outBytecode, std::string* outError) {
        JSRuntime* runtime = JS_NewRuntime();
        if (!runtime) {
            if (outError) *outError = "JS_NewRuntime failed";
            return false;
        }
        JSContext* context = JS_NewContext(runtime);
        if (!context) {
            JS_FreeRuntime(runtime);
            if (outError) *outError = "JS_NewContext failed";
            return false;
        }

        bool ok = false;
        JSValue compiled = JS_Eval(context, source.c_str(), source.size(), fileName.c_str(),
                                   JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            JSValue exception = JS_GetException(context);
            const char* message = JS_ToCString(context, exception);
            if (outError) *outError = message ? message : "compile error";
            if (message) JS_FreeCString(context, message);
            JSValue stack = JS_IsError(exception) ? JS_GetPropertyStr(context, exception, "stack") : JS_UNDEFINED;
            if (JS_IsString(stack) && outError) {
                const char* stackText = JS_ToCString(context, stack);
                if (stackText) {
                    *outError += "\n";
                    *outError += stackText;
                    JS_FreeCString(context, stackText);
                }
            }
            JS_FreeValue(context, stack);
            JS_FreeValue(context, exception);
        } else {
            size_t size = 0;
            // STRIP_SOURCE drops the embedded function source text (only Function.prototype.toString
            // needs it) but keeps line/column tables, so errors still map back to TypeScript.
            uint8_t* bytes = JS_WriteObject(context, &size, compiled, JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE);
            if (bytes) {
                outBytecode.assign(bytes, bytes + size);
                js_free(context, bytes);
                ok = true;
            } else if (outError) {
                *outError = "JS_WriteObject failed";
            }
            JS_FreeValue(context, compiled);
        }

        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return ok;
    }

}
