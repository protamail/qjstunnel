#include <jni.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <malloc.h>
#include "quickjs-libc.h"
#include <string.h>
//#include <stdio.h>
//#include <stdarg.h>
//#include <inttypes.h>
//#include <assert.h>
//#include <errno.h>
//#include <fcntl.h>
//#include <time.h>

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags);
static int eval_module(JSContext *ctx, const char *filename);
static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);

JNIEXPORT void JNICALL Java_Test_sayHello(JNIEnv* env, jobject thisObject) {
    printf("Hello from C !!\n");
}

typedef struct QJSRuntime {
    JSRuntime *rt;
    JSContext *ctx;
} QJSRuntime;

/* Init JS runtime and load root module */
JNIEXPORT jbyteArray JNICALL Java_QJSTunnel_newQJSRuntime(JNIEnv *env,
        jobject thisObject, jstring filename)
{
    JSRuntime *rt;
    JSContext *ctx;
    rt = JS_NewRuntime();
    if (!rt) {
        fprintf(stderr, "qjs: cannot allocate JS runtime\n");
        exit(2);
    }
    ctx = JS_NewContext(rt); // includes intrinsic objects init
    if (!ctx) {
        fprintf(stderr, "qjs: cannot allocate JS context\n");
        exit(2);
    }

    JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL); // loader for ES6 modules

    /* init console.log here rather than call js_std_add_helpers (thread safety concerns) */
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",
                      JS_NewCFunction(ctx, js_print, "log", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console);
    JS_FreeValue(ctx, global_obj);

    /* system modules */
    js_init_module_std(ctx, "std");
    js_init_module_os(ctx, "os");

    const char *str = "import * as std from 'std';\n"
                    "import * as os from 'os';\n"
                    "globalThis.std = std;\n"
                    "globalThis.os = os;\n";
    eval_buf(ctx, str, strlen(str), "<input>", JS_EVAL_TYPE_MODULE);

    QJSRuntime qjs = { rt, ctx };
    jbyteArray jba = (*env)->NewByteArray(env, sizeof(QJSRuntime));
    (*env)->SetByteArrayRegion(env, jba, 0, sizeof(QJSRuntime), (jbyte *)&qjs);
    printf("qjs: created new QJS runtime\n");

    const char *_filename = (*env)->GetStringUTFChars(env, filename, NULL);
    eval_module(ctx, _filename);
    (*env)->ReleaseStringUTFChars(env, filename, _filename); // don't leak

    return jba;
}

JNIEXPORT void JNICALL Java_QJSTunnel_freeQJSRuntime(
        JNIEnv *env, jobject thisObject, jbyteArray jba)
{
    signed char *jbytes = (*env)->GetByteArrayElements(env, jba, NULL);
//    js_std_free_handlers(((QJSRuntime *)jbytes)->rt);
    JS_FreeContext(((QJSRuntime *)jbytes)->ctx);
    JS_FreeRuntime(((QJSRuntime *)jbytes)->rt);
    (*env)->ReleaseByteArrayElements(env, jba, jbytes, 0);
    printf("qjs: released QJS runtime\n");
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    int i;
    const char *str;

    for(i = 0; i < argc; i++) {
        if (i != 0)
            putchar(' ');
        str = JS_ToCString(ctx, argv[i]);
        if (!str)
            return JS_EXCEPTION;
        fputs(str, stdout);
        JS_FreeCString(ctx, str);
    }
    putchar('\n');
    return JS_UNDEFINED;
}

static int eval_module(JSContext *ctx, const char *filename)
{
    size_t buf_len;
    uint8_t *buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        perror(filename);
        exit(1);
    }

    int ret = eval_buf(ctx, buf, buf_len, filename, JS_EVAL_TYPE_MODULE); // vs JS_EVAL_TYPE_GLOBAL
    js_free(ctx, buf);
    return ret;
}

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags)
{
    JSValue val;
    int ret;

    if ((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
        /* for the modules, we compile then run to be able to set
           import.meta */
        val = JS_Eval(ctx, buf, buf_len, filename,
                      eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(val)) {
            js_module_set_import_meta(ctx, val, 1, 1);
            val = JS_EvalFunction(ctx, val);
        }
    } else {
        val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
    }
    if (JS_IsException(val)) {
        js_std_dump_error(ctx);
        ret = -1;
    } else {
        ret = 0;
    }
    JS_FreeValue(ctx, val);
    return ret;
}

JNIEXPORT jint JNICALL Java_Test_exec_1cmd(JNIEnv *env, jobject this, jobjectArray stringArray)
{
    int pid, status, ret;
    char **argv = malloc(sizeof(char *)), *arg0;
    jstring name;

    int len = (*env)->GetArrayLength(env, stringArray);

    if (len < 1)
        return -1;

    name = (*env)->GetObjectArrayElement(env, stringArray, 0);
    arg0 = (char *)(*env)->GetStringUTFChars(env, name, NULL);
    argv[0] = arg0;
    argv[1] = 0;

    pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        exit(1);
    } 

    for(;;) {
        ret = waitpid(pid, &status, 0);
        if (ret == pid && WIFEXITED(status))
            break;
    }

    (*env)->ReleaseStringUTFChars(env, name, arg0); // don't leak
    free(argv);
    return WEXITSTATUS(status);
}


