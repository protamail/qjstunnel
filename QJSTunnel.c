#include <jni.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <malloc.h>
#include "quickjs-libc.h"
#include <string.h>

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags);
static int eval_module(JSContext *ctx, const char *filename);
static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);
static JSValue js_java_call(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv);

typedef struct QJSHandle {
    JSContext *ctx;
    JSValue main_func;
} QJSHandle;

/* Init JS runtime and load root module */
JNIEXPORT jbyteArray JNICALL Java_QJSTunnel_newQJSRuntime(JNIEnv *env,
        jobject thisObject, jstring filename, jstring mainFunc)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    jbyteArray ret = NULL;
    if (!rt) {
        printf("Error: cannot allocate JS runtime\n");
        goto release_runtime;
    }
    ctx = JS_NewContext(rt); // includes intrinsic objects init
    if (!ctx) {
        printf("Error: cannot allocate JS context\n");
        goto release_runtime;
    }

    printf("qjs: Creating new QJS runtime...\n");
    JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL); // loader for ES6 modules

    /* init console.log here rather than call js_std_add_helpers (thread safety concerns) */
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",
                      JS_NewCFunction(ctx, js_print, "log", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console);
    JS_SetPropertyStr(ctx, global_obj, "callJava",
                      JS_NewCFunction(ctx, js_java_call, "callJava", 1/* at least one param */));

    /* system modules */
    js_init_module_std(ctx, "std");
    js_init_module_os(ctx, "os");

    const char *_filename = (*env)->GetStringUTFChars(env, filename, NULL);
    const char *_main_func = (*env)->GetStringUTFChars(env, mainFunc, NULL);
    eval_module(ctx, _filename);
    JSValue main_func = JS_GetPropertyStr(ctx, global_obj, _main_func);
    if (!JS_IsFunction(ctx, main_func)) {
        printf("newQJSRuntime: globalThis.%s function undefined\n", _main_func);
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
    if (ret)
        return ret;
release_runtime:
    if (ctx)
        JS_FreeContext(ctx);
    if (rt)
        JS_FreeRuntime(rt);
    printf("qjs: released QJS runtime\n");
    return (*env)->NewByteArray(env, 0); // return zero-length array to indicate error
}

JNIEXPORT void JNICALL Java_QJSTunnel_freeQJSRuntime(
        JNIEnv *env, jobject thisObject, jbyteArray jctx)
{
    if (!(*env)->GetArrayLength(env, jctx))
        return;
    QJSHandle *qjs = (QJSHandle *)(*env)->GetByteArrayElements(env, jctx, NULL);
    JSRuntime *rt = JS_GetRuntime(qjs->ctx);
    JS_FreeValue(qjs->ctx, qjs->main_func);
    JS_FreeContext(qjs->ctx);
    JS_FreeRuntime(rt);
    (*env)->ReleaseByteArrayElements(env, jctx, (signed char *)qjs, 0);
    printf("qjs: released QJS runtime\n");
}

static inline JSValue newJSString(JSContext *ctx, JNIEnv *env, jstring jarg)
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

JNIEXPORT jint JNICALL Java_QJSTunnel_callQJS(
        JNIEnv *env, jobject thisObject, jbyteArray jctx, jobjectArray stringArray)
{
    if (!(*env)->GetArrayLength(env, jctx))
        return -1;
    QJSHandle *qjs = (QJSHandle *)(*env)->GetByteArrayElements(env, jctx, NULL);
    JSContext *ctx = qjs->ctx;
    int ret = 0;
    jclass cls = (*env)->GetObjectClass(env, thisObject);
    jmethodID callJava = (*env)->GetMethodID(env, cls, "callJava", "([Ljava/lang/Object;)[Ljava/lang/Object;");
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
    JavaHandle javaCtx = { env, thisObject, callJava, objectClass, objectToString, integerClass, integerConstr,
                           doubleClass, doubleConstr, numberClass, numberDoubleValue, stringClass, objectArrayClass };

    JS_SetContextOpaque(ctx, &javaCtx);
    JSValue global_obj = JS_GetGlobalObject(ctx);
    int argc = (*env)->GetArrayLength(env, stringArray);
    JSValue *argv = (JSValue *)(js_malloc(ctx, argc * sizeof(JSValue)));
    for (int i = 0; i < argc; i++) {
        jstring jarg = (*env)->GetObjectArrayElement(env, stringArray, i);
        argv[i] = newJSString(ctx, env, jarg);
    }
    JSValue result = JS_Call(ctx, qjs->main_func, global_obj, argc, argv);
    if (JS_IsException(result)) {
        ret = -1;
        js_std_dump_error(ctx);
        goto finalize;
    }

finalize:
    for (int i = 0; i < argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }
    js_free(ctx, argv);
    JS_FreeValue(ctx, global_obj);
    JS_FreeValue(ctx, result);
    JS_SetContextOpaque(ctx, NULL);
    (*env)->ReleaseByteArrayElements(env, jctx, (signed char *)qjs, 0);
    return ret;
}

static JSValue newJSArray(JSContext *ctx, JNIEnv *env, JavaHandle *javaCtx, jobjectArray jarr, int *depth)
{
    int len = jarr? (*env)->GetArrayLength(env, jarr) : 0;
    JSValue ret = JS_NewArray(ctx);
    for (int i = 0; i < len; i++) {
        jobject jobj = (*env)->GetObjectArrayElement(env, jarr, i);
        if ((*env)->IsInstanceOf(env, jobj, javaCtx->objectArrayClass)) {
            if (*depth > 100)
                printf("newJSArray: too many nested arrays, circular ref?\n");
            else {
                ++*depth;
                JS_SetPropertyUint32(ctx, ret, i, newJSArray(ctx, env, javaCtx, (jobjectArray)jobj, depth));
            }
            --*depth;
        }
        else if ((*env)->IsInstanceOf(env, jobj, javaCtx->numberClass)) {
            jdouble jdbl = (*env)->CallDoubleMethod(env, jobj, javaCtx->numberDoubleValue);
            JS_SetPropertyUint32(ctx, ret, i, JS_NewFloat64(ctx, jdbl));
        }
        else {
            jobject jstr = (*env)->IsInstanceOf(env, jobj, javaCtx->stringClass)? jobj :
                    (*env)->CallObjectMethod(env, jobj, javaCtx->objectToString);
            JS_SetPropertyUint32(ctx, ret, i, newJSString(ctx, env, (jstring)jstr));
        }
    }
    return ret;
}

static jobjectArray newJavaObjectArray(JSContext *ctx, JNIEnv *env, JavaHandle *javaCtx, int argc, JSValueConst *argv, int *depth)
{
    jobjectArray ret = (*env)->NewObjectArray(env, argc, javaCtx->objectClass, NULL);
    for (int i = 0; i < argc; i++) {
        JSValueConst val = argv[i];
        if (JS_IsArray(ctx, val)) {
            int len = 0;
            JSValue jslen = JS_GetPropertyStr(ctx, val, "length");
            JS_ToInt32(ctx, &len, jslen);
            JS_FreeValue(ctx, jslen);
            JSValue *jsa = (JSValue *)(js_malloc(ctx, len * sizeof(JSValue)));
            for (int j = 0; j < len; j++)
                jsa[j] = JS_GetPropertyUint32(ctx, val, j);
            if (*depth > 100)
                printf("newJavaObjectArray: too many nested arrays, circular ref?\n");
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
                    if (!str)
                        str = "";
                    (*env)->SetObjectArrayElement(env, ret, i, (*env)->NewStringUTF(env, str));
                    JS_FreeCString(ctx, str);
            }
        }
    }
    return ret;
}

static JSValue js_java_call(JSContext *ctx, JSValueConst this_val,
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


