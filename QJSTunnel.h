/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class QJSTunnel */

#ifndef _Included_QJSTunnel
#define _Included_QJSTunnel
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     QJSTunnel
 * Method:    newQJSRuntime
 * Signature: (Ljava/lang/String;Ljava/lang/String;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_QJSTunnel_newQJSRuntime
  (JNIEnv *, jobject, jstring, jstring);

/*
 * Class:     QJSTunnel
 * Method:    callQJS
 * Signature: ([B[Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_QJSTunnel_callQJS
  (JNIEnv *, jobject, jbyteArray, jobjectArray);

/*
 * Class:     QJSTunnel
 * Method:    freeQJSRuntime
 * Signature: ([B)V
 */
JNIEXPORT void JNICALL Java_QJSTunnel_freeQJSRuntime
  (JNIEnv *, jobject, jbyteArray);

/*
 * Class:     QJSTunnel
 * Method:    exec_cmd
 * Signature: ([Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_QJSTunnel_exec_1cmd
  (JNIEnv *, jobject, jobjectArray);

#ifdef __cplusplus
}
#endif
#endif
