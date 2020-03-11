#include <jni.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <malloc.h>
#include "quickjs-libc.h"
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define likely(x)          __builtin_expect(!!(x), 1)
#define unlikely(x)        __builtin_expect(!!(x), 0)
#define force_inline       inline __attribute__((always_inline))
#else
#define likely(x)     (x)
#define unlikely(x)   (x)
#define force_inline  inline
#endif

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags);
static int eval_module(JSContext *ctx, const char *filename);
static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);
static JSValue js_call_java(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);

typedef struct QJSHandle {
    JSContext *ctx;
    JSValue main_func;
} QJSHandle;

/* Init JS runtime and load root module */
JNIEXPORT jbyteArray JNICALL Java_org_scriptable_QJSConnector_newQJSRuntime(
        JNIEnv *env, jclass cls, jstring filename, jstring mainFunc)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = NULL;
    jbyteArray ret = NULL;
    if (unlikely(!rt)) {
        fprintf(stderr, "Error: cannot allocate JS runtime\n");
        goto release_runtime;
    }
    ctx = JS_NewContext(rt); // includes intrinsic objects init
    if (unlikely(!ctx)) {
        fprintf(stderr, "Error: cannot allocate JS context\n");
        goto release_runtime;
    }

    fprintf(stderr, "qjs: Creating new QJS runtime...\n");
    JS_SetCanBlock(rt, 1);
    JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL); // loader for ES6 modules

    /* init console.log here rather than call js_std_add_helpers (thread safety concerns) */
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",
                      JS_NewCFunction(ctx, js_print, "log", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console);
    JS_SetPropertyStr(ctx, global_obj, "callJava",
                      JS_NewCFunction(ctx, js_call_java, "callJava", 1/* at least one param */));

    /* system modules */
    js_init_module_std(ctx, "std");
    js_init_module_os(ctx, "os");

    const char *_filename = (*env)->GetStringUTFChars(env, filename, NULL);
    const char *_main_func = (*env)->GetStringUTFChars(env, mainFunc, NULL);
    eval_module(ctx, _filename);
    JSValue main_func = JS_GetPropertyStr(ctx, global_obj, _main_func);
    if (unlikely(!JS_IsFunction(ctx, main_func))) {
        fprintf(stderr, "newQJSRuntime: globalThis.%s function undefined\n", _main_func);
        JS_FreeValue(ctx, main_func);
        goto fail;
    }
    QJSHandle qjsCtx = { ctx, main_func };
    ret = (*env)->NewByteArray(env, sizeof(QJSHandle));
    (*env)->SetByteArrayRegion(env, ret, 0, sizeof(QJSHandle), (jbyte *)&qjsCtx);

fail:
    (*env)->ReleaseStringUTFChars(env, mainFunc, _main_func);
    (*env)->ReleaseStringUTFChars(env, filename, _filename);
    JS_FreeValue(ctx, global_obj);
    if (likely(ret))
        return ret;
release_runtime:
    if (ctx)
        JS_FreeContext(ctx);
    if (rt)
        JS_FreeRuntime(rt);
    fprintf(stderr, "qjs: released QJS runtime\n");
//    fflush(stdout); // stderr not buffered
    return (*env)->NewByteArray(env, 0); // return zero-length array to indicate error
}

JNIEXPORT void JNICALL Java_org_scriptable_QJSConnector_freeQJSRuntime(
        JNIEnv *env, jclass cls, jbyteArray jctx)
{
    if (unlikely(!(*env)->GetArrayLength(env, jctx)))
        return;
    QJSHandle *qjs = (QJSHandle *)(*env)->GetByteArrayElements(env, jctx, NULL);
    JSRuntime *rt = JS_GetRuntime(qjs->ctx);
    JS_FreeValue(qjs->ctx, qjs->main_func);
    JS_FreeContext(qjs->ctx);
    JS_FreeRuntime(rt);
    (*env)->ReleaseByteArrayElements(env, jctx, (signed char *)qjs, 0);
    fprintf(stderr, "qjs: released QJS runtime\n");
//    fflush(stdout);
}

static force_inline JSValue newJSString(JSContext *ctx, JNIEnv *env, jstring jarg)
{
    char *carg = (char *)(*env)->GetStringUTFChars(env, jarg, NULL);
    JSValue val = JS_NewStringLen(ctx, carg, (*env)->GetStringUTFLength(env, jarg));
    (*env)->ReleaseStringUTFChars(env, jarg, carg);
    return val;
}

typedef struct JavaHandle {
    JNIEnv *env;
    jobject thisObject;
    jmethodID callJava;
    jclass objectClass;
    jmethodID objectToString;
    jclass integerClass;
    jmethodID integerConstr;
    jclass doubleClass;
    jmethodID doubleConstr;
    jclass numberClass;
    jmethodID numberDoubleValue;
    jclass stringClass;
    jclass objectArrayClass;
} JavaHandle;

static JSValue newJSArray(JSContext *ctx, JNIEnv *env, JavaHandle *javaCtx, jobjectArray jarr, int *depth)
{
    int len = jarr? (*env)->GetArrayLength(env, jarr) : 0;
    JSValue ret = JS_NewArray(ctx);
    for (int i = 0; i < len; i++) {
        jobject jobj = (*env)->GetObjectArrayElement(env, jarr, i);
        if (likely((*env)->IsInstanceOf(env, jobj, javaCtx->stringClass))) {
            JS_SetPropertyUint32(ctx, ret, i, newJSString(ctx, env, (jstring)jobj));
        }
        else if ((*env)->IsInstanceOf(env, jobj, javaCtx->numberClass)) {
            jdouble jdbl = (*env)->CallDoubleMethod(env, jobj, javaCtx->numberDoubleValue);
            JS_SetPropertyUint32(ctx, ret, i, JS_NewFloat64(ctx, jdbl));
        }
        else if ((*env)->IsInstanceOf(env, jobj, javaCtx->objectArrayClass)) {
            if (unlikely(*depth > 100))
                fprintf(stderr, "newJSArray: too many nested arrays, circular ref?\n");
            else {
                ++*depth;
                JS_SetPropertyUint32(ctx, ret, i, newJSArray(ctx, env, javaCtx, (jobjectArray)jobj, depth));
            }
            --*depth;
        }
        else {
            jobject jstr = (*env)->CallObjectMethod(env, jobj, javaCtx->objectToString);
            JS_SetPropertyUint32(ctx, ret, i, newJSString(ctx, env, (jstring)jstr));
        }
    }
    return ret;
}

JNIEXPORT jint JNICALL Java_org_scriptable_QJSConnector_callQJS(
        JNIEnv *env, jobject thisObject, jbyteArray jctx, jobjectArray jarr)
{
    if (unlikely(!(*env)->GetArrayLength(env, jctx)))
        return -1;
    QJSHandle *qjs = (QJSHandle *)(*env)->GetByteArrayElements(env, jctx, NULL);
    JSContext *ctx = qjs->ctx;
    int ret = 0;
    jclass cls = (*env)->GetObjectClass(env, thisObject);
    jmethodID callJava = (*env)->GetMethodID(env, cls, "callJava",
            "([Ljava/lang/Object;)[Ljava/lang/Object;");
    if (!callJava) {
        fprintf(stderr, "callQJS: method Object[] callJava(Object[]) undefined\n");
        return -1;
    }
    jclass objectClass = (*env)->FindClass(env, "java/lang/Object");
    jmethodID objectToString = (*env)->GetMethodID(env, objectClass, "toString", "()Ljava/lang/String;");
    jclass integerClass = (*env)->FindClass(env, "java/lang/Integer");
    jmethodID integerConstr = (*env)->GetMethodID(env, integerClass, "<init>", "(I)V");
    jclass doubleClass = (*env)->FindClass(env, "java/lang/Double");
    jmethodID doubleConstr = (*env)->GetMethodID(env, doubleClass, "<init>", "(D)V");
    jclass numberClass = (*env)->FindClass(env, "java/lang/Number");
    jmethodID numberDoubleValue = (*env)->GetMethodID(env, numberClass, "doubleValue", "()D");
    jclass stringClass = (*env)->FindClass(env, "java/lang/String");
    jclass objectArrayClass = (*env)->FindClass(env, "[Ljava/lang/Object;");
    JavaHandle javaCtx = { env, thisObject, callJava, objectClass, objectToString, integerClass,
            integerConstr, doubleClass, doubleConstr, numberClass, numberDoubleValue, stringClass,
            objectArrayClass };

    JS_SetContextOpaque(ctx, &javaCtx);
    JSValue global_obj = JS_GetGlobalObject(ctx);

    int depth = 0;
    int argc = (*env)->GetArrayLength(env, jarr);
    JSValue jsa = newJSArray(ctx, env, &javaCtx, jarr, &depth);
    JSValue *argv = (JSValue *)(js_malloc(ctx, argc * sizeof(JSValue)));
    if (!argv)
        return -1;
    for (int i = 0; i < argc; i++)
        argv[i] = JS_GetPropertyUint32(ctx, jsa, i);
    JSValue result = JS_Call(ctx, qjs->main_func, global_obj, argc, argv);
    if (unlikely(JS_IsException(result))) {
        ret = -1;
        js_std_dump_error(ctx);
    }

    for (int i = 0; i < argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }
    JS_FreeValue(ctx, jsa);
    js_free(ctx, argv);
    JS_FreeValue(ctx, global_obj);
    JS_FreeValue(ctx, result);
    JS_SetContextOpaque(ctx, NULL);
    (*env)->ReleaseByteArrayElements(env, jctx, (signed char *)qjs, 0);
//    fflush(stdout);
    return ret;
}

static jobjectArray newJavaObjectArray(JSContext *ctx, JNIEnv *env, JavaHandle *javaCtx,
        int argc, JSValueConst *argv, int *depth)
{
    jobjectArray ret = (*env)->NewObjectArray(env, argc, javaCtx->objectClass, NULL);
    for (int i = 0; i < argc; i++) {
        JSValueConst val = argv[i];
        if (unlikely(JS_IsArray(ctx, val))) {
            int len = 0;
            JSValue jslen = JS_GetPropertyStr(ctx, val, "length");
            JS_ToInt32(ctx, &len, jslen);
            JS_FreeValue(ctx, jslen);
            JSValue *jsa = (JSValue *)(js_malloc(ctx, len * sizeof(JSValue)));
            if (!jsa)
                return NULL;
            for (int j = 0; j < len; j++)
                jsa[j] = JS_GetPropertyUint32(ctx, val, j);
            if (unlikely(*depth > 100))
                fprintf(stderr, "newJavaObjectArray: too many nested arrays, circular ref?\n");
            else {
                ++*depth;
                (*env)->SetObjectArrayElement(env, ret, i,
                    newJavaObjectArray(ctx, env, javaCtx, len, jsa, depth));
            }
            --*depth;
            for (int j = 0; j < len; j++)
                JS_FreeValue(ctx, jsa[j]);
            js_free(ctx, jsa);
        }
        else {
            int tag = JS_VALUE_GET_TAG(val);
            const char *str;
            switch(tag) {
                case JS_TAG_INT:
                case JS_TAG_BOOL:
                    (*env)->SetObjectArrayElement(env, ret, i,
                        (*env)->NewObjectA(env, javaCtx->integerClass, javaCtx->integerConstr, (jvalue *)&JS_VALUE_GET_INT(val)));
                    break;
                case JS_TAG_NULL:
                case JS_TAG_UNDEFINED:
                    (*env)->SetObjectArrayElement(env, ret, i, NULL);
                    break;
                case JS_TAG_FLOAT64:
                    (*env)->SetObjectArrayElement(env, ret, i,
                        (*env)->NewObjectA(env, javaCtx->doubleClass, javaCtx->doubleConstr, (jvalue *)&JS_VALUE_GET_FLOAT64(val)));
                    break;
                default:
                    str = JS_ToCString(ctx, val);
                    if (unlikely(!str))
                        str = "";
                    (*env)->SetObjectArrayElement(env, ret, i, (*env)->NewStringUTF(env, str));
                    JS_FreeCString(ctx, str);
            }
        }
    }
    return ret;
}

static JSValue js_call_java(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    JavaHandle *javaCtx = (JavaHandle *)JS_GetContextOpaque(ctx);
    JNIEnv *env = javaCtx->env;
    int depth = 0;
    jobjectArray jarr = newJavaObjectArray(ctx, env, javaCtx, argc, argv, &depth);
    jarr = (jobjectArray)(*env)->CallObjectMethod(env, javaCtx->thisObject, javaCtx->callJava, jarr);
    return newJSArray(ctx, env, javaCtx, jarr, &depth);
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    int i;
    const char *str;

    for(i = 0; i < argc; i++) {
        if (i != 0)
            fputs(" ", stderr);
        str = JS_ToCString(ctx, argv[i]);
        if (!str)
            return JS_EXCEPTION;
        fputs(str, stderr);
        JS_FreeCString(ctx, str);
    }
    fputs("\n", stderr);
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
    int ret = 0;
    JSValue val;

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
        ret = -1;
        js_std_dump_error(ctx);
    }
    JS_FreeValue(ctx, val);
    return ret;
}

JNIEXPORT jint JNICALL Java_org_scriptable_QJSConnector_exec_1cmd(
        JNIEnv *env, jobject this, jobjectArray stringArray)
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


