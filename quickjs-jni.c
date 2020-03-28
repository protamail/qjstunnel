#include <jni.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <malloc.h>
#include "quickjs-libc.h"
#include <string.h>
#include <pthread.h>
#include <errno.h>

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

static pthread_mutex_t js_atomics_mutex = PTHREAD_MUTEX_INITIALIZER;
static int js_instance_count = 0;
static int inc_instance_count()
{
    pthread_mutex_lock(&js_atomics_mutex);
    int ret = ++js_instance_count;
    if (!(ret&7))
        fprintf(stdout, "quickjs: runtime alloc count %d\n", ret);
    pthread_mutex_unlock(&js_atomics_mutex);
    return ret;
}

static int dec_instance_count()
{
    pthread_mutex_lock(&js_atomics_mutex);
    int ret = --js_instance_count;
    fprintf(stdout, "quickjs: runtime alloc count %d\n", ret);
    pthread_mutex_unlock(&js_atomics_mutex);
    return ret;
}

/* Init JS runtime and load root module */
JNIEXPORT jbyteArray JNICALL Java_org_scriptable_QuickJSConnector_nativeNewQJSRuntime(
        JNIEnv *env, jclass cls, jstring filename, jstring mainFunc)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = NULL;
    jbyteArray ret = NULL;
    if (unlikely(!rt)) {
        fprintf(stdout, "Error: cannot allocate JS runtime\n");
        goto release_runtime;
    }
    ctx = JS_NewContext(rt); // includes intrinsic objects init
    if (unlikely(!ctx)) {
        fprintf(stdout, "Error: cannot allocate JS context\n");
        goto release_runtime;
    }

    JS_SetCanBlock(rt, 1);
    JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL); // loader for ES6 modules

    /* init console.log here rather than call js_std_add_helpers (thread safety concerns) */
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_DefinePropertyValueStr(ctx, console, "log",
                      JS_NewCFunction(ctx, js_print, "log", 1), 0);
    JS_DefinePropertyValueStr(ctx, global_obj, "console", console, 0);
    JS_DefinePropertyValueStr(ctx, global_obj, "callJava",
                      JS_NewCFunction(ctx, js_call_java, "callJava", 1/* at least one param */), 0);

    /* system modules */
    js_init_module_std(ctx, "std");
    js_init_module_os(ctx, "os");

    const char *_filename = (*env)->GetStringUTFChars(env, filename, NULL);
    const char *_main_func = (*env)->GetStringUTFChars(env, mainFunc, NULL);
    int eret = eval_module(ctx, _filename);
    JSValue main_func = eret < 0? JS_UNDEFINED : JS_GetPropertyStr(ctx, global_obj, _main_func);
    if (!eret && !JS_IsFunction(ctx, main_func))
        JS_ThrowInternalError(ctx, "globalThis.%s function undefined", _main_func);
    QJSHandle qjsCtx = { ctx, main_func };
    ret = (*env)->NewByteArray(env, sizeof(QJSHandle));
    (*env)->SetByteArrayRegion(env, ret, 0, sizeof(QJSHandle), (jbyte *)&qjsCtx);

    (*env)->ReleaseStringUTFChars(env, mainFunc, _main_func);
    (*env)->ReleaseStringUTFChars(env, filename, _filename);
    JS_FreeValue(ctx, global_obj);
    inc_instance_count();
    fflush(stdout); // stdout not buffered
    return ret;
release_runtime:
    if (ctx)
        JS_FreeContext(ctx);
    if (rt)
        JS_FreeRuntime(rt);
    return (*env)->NewByteArray(env, 0); // return zero-length array to indicate error
}

JNIEXPORT void JNICALL Java_org_scriptable_QuickJSConnector_nativeFreeQJSRuntime(
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
    dec_instance_count();
    fflush(stdout);
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
                fprintf(stdout, "newJSArray: too many nested arrays, circular ref?\n");
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

static int init_java_ctx(JNIEnv *env, jobject thisObject, JavaHandle *javaCtx)
{
    jclass cls = (*env)->GetObjectClass(env, thisObject);
    jmethodID callJava = (*env)->GetMethodID(env, cls, "callJava",
            "([Ljava/lang/Object;)[Ljava/lang/Object;");
    if (!callJava) {
        fprintf(stdout, "callQJS: method Object[] callJava(Object[]) undefined\n");
        return -1;
    }
    javaCtx->env = env;
    javaCtx->thisObject = thisObject;
    javaCtx->callJava = callJava;
    javaCtx->objectClass = (*env)->FindClass(env, "java/lang/Object");
    javaCtx->objectToString = (*env)->GetMethodID(env, javaCtx->objectClass, "toString",
            "()Ljava/lang/String;");
    javaCtx->integerClass = (*env)->FindClass(env, "java/lang/Integer");
    javaCtx->integerConstr = (*env)->GetMethodID(env, javaCtx->integerClass, "<init>", "(I)V");
    javaCtx->doubleClass = (*env)->FindClass(env, "java/lang/Double");
    javaCtx->doubleConstr = (*env)->GetMethodID(env, javaCtx->doubleClass, "<init>", "(D)V");
    javaCtx->numberClass = (*env)->FindClass(env, "java/lang/Number");
    javaCtx->numberDoubleValue = (*env)->GetMethodID(env, javaCtx->numberClass, "doubleValue", "()D");
    javaCtx->stringClass = (*env)->FindClass(env, "java/lang/String");
    javaCtx->objectArrayClass = (*env)->FindClass(env, "[Ljava/lang/Object;");
    return 0;
}

JNIEXPORT jint JNICALL Java_org_scriptable_QuickJSConnector_nativeCallQJS(
        JNIEnv *env, jobject thisObject, jbyteArray jctx, jobjectArray jarr)
{
    if (unlikely(!(*env)->GetArrayLength(env, jctx)))
        return -1;
    QJSHandle *qjs = (QJSHandle *)(*env)->GetByteArrayElements(env, jctx, NULL);
    if (!qjs)
        return -1;
    JSContext *ctx = qjs->ctx;
    int ret = 0;
    JavaHandle javaCtx;
    if (init_java_ctx(env, thisObject, &javaCtx) < 0)
        return -1;

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
    if (unlikely(JS_IsException(result)))
        ret = -1;
    if (JS_VALUE_GET_TAG(result) == JS_TAG_INT)
        ret = JS_VALUE_GET_INT(result);

    for (int i = 0; i < argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }
    JS_FreeValue(ctx, jsa);
    js_free(ctx, argv);
    JS_FreeValue(ctx, global_obj);
    JS_FreeValue(ctx, result);
    JS_SetContextOpaque(ctx, NULL);
    (*env)->ReleaseByteArrayElements(env, jctx, (signed char *)qjs, 0);
    fflush(stdout);
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
                fprintf(stdout, "newJavaObjectArray: too many nested arrays, circular ref?\n");
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
                        (*env)->NewObjectA(env, javaCtx->integerClass, javaCtx->integerConstr,
                            (jvalue *)&JS_VALUE_GET_INT(val)));
                    break;
                case JS_TAG_NULL:
                case JS_TAG_UNDEFINED:
                    (*env)->SetObjectArrayElement(env, ret, i, NULL);
                    break;
                case JS_TAG_FLOAT64:
                    (*env)->SetObjectArrayElement(env, ret, i,
                        (*env)->NewObjectA(env, javaCtx->doubleClass, javaCtx->doubleConstr,
                            (jvalue *)&JS_VALUE_GET_FLOAT64(val)));
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

JNIEXPORT jobjectArray JNICALL Java_org_scriptable_QuickJSConnector_nativeGetQJSException(
        JNIEnv *env, jobject thisObject, jbyteArray jctx) {
    if (unlikely(!(*env)->GetArrayLength(env, jctx)))
        return NULL;
    QJSHandle *qjs = (QJSHandle *)(*env)->GetByteArrayElements(env, jctx, NULL);
    JSContext *ctx = qjs->ctx;
    JavaHandle javaCtx;
    if (init_java_ctx(env, thisObject, &javaCtx) < 0)
        return NULL;
    JSValue exception_val = JS_GetException(ctx);
    jobjectArray jarr = NULL;
    int depth = 0;
    if (!JS_IsNull(exception_val) && !JS_IsUndefined(exception_val)) {
        JSValue stack = JS_GetPropertyStr(ctx, exception_val, "stack");
        if (!JS_IsNull(stack) && !JS_IsUndefined(stack)) {
            JSValue msgs[] = { exception_val, stack };
            jarr = newJavaObjectArray(ctx, env, &javaCtx, 2, msgs, &depth);
        } else
            jarr = newJavaObjectArray(ctx, env, &javaCtx, 1, &exception_val, &depth);
        JS_FreeValue(ctx, stack);
    }
    JS_FreeValue(ctx, exception_val);
    (*env)->ReleaseByteArrayElements(env, jctx, (signed char *)qjs, 0);
    return jarr;
}

static JSValue js_call_java(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    JavaHandle *javaCtx = (JavaHandle *)JS_GetContextOpaque(ctx);
    if (javaCtx == NULL) {
        fprintf(stdout, "js_call_java: JS_GetContextOpaque is NULL");
        return JS_UNDEFINED;
    }
    JNIEnv *env = javaCtx->env;
    int depth = 0;
    jobjectArray jarr = newJavaObjectArray(ctx, env, javaCtx, argc, argv, &depth);
    jarr = (jobjectArray)(*env)->CallObjectMethod(env, javaCtx->thisObject, javaCtx->callJava, jarr);
    JSValue ret = newJSArray(ctx, env, javaCtx, jarr, &depth);
    if (jarr && (*env)->GetArrayLength(env, jarr) == 2) { // did we get an exception back?
        JSValue val = JS_GetPropertyUint32(ctx, ret, 0);
        const char *str = JS_ToCString(ctx, val);
        if (unlikely(!str))
            str = "";
        if (!strcmp(str, "__error__")) {
            JSValue error = JS_GetPropertyUint32(ctx, ret, 1);
            const char *cerror = JS_ToCString(ctx, error);
            JS_FreeValue(ctx, ret);
            ret = JS_ThrowInternalError(ctx, cerror);
            JS_FreeCString(ctx, cerror);
            JS_FreeValue(ctx, error);
        }
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, val);
    }
    return ret;
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    int i;
    const char *str;

    for(i = 0; i < argc; i++) {
        if (i != 0)
            fputs(" ", stdout);
        str = JS_ToCString(ctx, argv[i]);
        if (!str)
            return JS_EXCEPTION;
        fputs(str, stdout);
        JS_FreeCString(ctx, str);
    }
    fputs("\n", stdout);
    return JS_UNDEFINED;
}

static int eval_module(JSContext *ctx, const char *filename)
{
    size_t buf_len;
    uint8_t *buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        JS_ThrowInternalError(ctx, strerror(errno));
        //perror(filename);
        return -1;
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
    }
    JS_FreeValue(ctx, val);
    return ret;
}


